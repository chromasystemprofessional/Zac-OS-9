/*
 * Screen saver, display sleep and system sleep (the Energy Saver panel's
 * settings, applied with idle_policy.c's rules).
 *
 * Settings, in minutes (0 is never), in ~/.config/zacos9/desktop.conf:
 *   screensaver-after=N  the screen saver's turn (default 5)
 *   display-sleep=N      the display goes dark (default 10)
 *   system-sleep=N       systemctl suspend (default never)
 * and "screensaver=ID" (lib/savers.c; "none" for no screen saver).
 *
 * zacos9-screensaver covers each screen in its turn; it is started when a
 * screen reaches it and leaves by itself, and its surfaces (namespace
 * "screensaver") are hidden here the moment their screen wakes.
 *
 * Any input wakes every screen. A screen holding a full-screen window, or
 * a visible surface with an idle inhibitor (idle-inhibit-unstable-v1: a
 * video playing), stays awake while the others sleep; the system sleeps
 * only when no screen is kept awake. Only real screens (DRM) are turned
 * off, and only a real session (or $ZACOS9_SUSPEND_COMMAND) suspends:
 * nested or headless, the states are only reported.
 *
 * Apps that ask over D-Bus instead (org.freedesktop.ScreenSaver, the
 * portal's Inhibit) reach us through zacos9-inhibit and platinum-shell's
 * keep_awake: the screens showing that app's windows stay awake (every
 * screen, if none is found), and the system doesn't sleep.
 *
 * Each screen's state is published for the screen saver and tests in
 * $XDG_RUNTIME_DIR/zacos9-idle-$WAYLAND_DISPLAY:
 *   output NAME awake|saver|asleep
 * $ZACOS9_IDLE_MINUTE_MS makes a "minute" shorter, for tests.
 */
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <wlr/backend/drm.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#include "idle_policy.h"
#include "platinum-shell-v1-protocol.h"
#include "server.h"
#include "settings.h"

/* How often to look again while an app keeps a screen awake: its surface
 * can move to another screen, or be covered, with no event of our own. */
#define KEPT_RECHECK_MS 10000

/* An app's D-Bus request to stay awake (platinum_keep_awake_v1). */
struct keep_awake {
	struct wl_list link; /* plat_server.keep_awake */
	struct plat_server *server;
	uint32_t pid;
	char *app_id;
	uint32_t what; /* enum platinum_shell_v1_keep_awake_what */
};

struct idle_inhibitor {
	struct plat_server *server;
	struct wl_listener destroy;
};

static long now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static long minutes(const char *key, long fallback) {
	static long minute_ms;
	if (!minute_ms) {
		const char *env = getenv("ZACOS9_IDLE_MINUTE_MS");
		minute_ms = env && atol(env) > 0 ? atol(env) : 60000;
	}
	char v[32];
	long n = fallback;
	if (pl_setting(key, v, sizeof(v))) {
		char *end;
		const long set = strtol(v, &end, 10);
		if (!*end && set >= 0 && set <= 24 * 60) {
			n = set;
		}
	}
	return n * minute_ms;
}

static bool saver_chosen(void) {
	char id[64];
	return !pl_setting("screensaver", id, sizeof(id)) || strcmp(id, "none") != 0;
}

static struct idle_timeouts timeouts(void) {
	return (struct idle_timeouts){
		.saver_ms = saver_chosen() ? minutes("screensaver-after", 5) : 0,
		.display_ms = minutes("display-sleep", 10),
		.system_ms = minutes("system-sleep", 0),
	};
}

/* The process and up to 7 of its parents: an app's D-Bus requests can come
 * from a helper process of the one that owns its windows. */
static int pid_and_parents(uint32_t pid, uint32_t out[8]) {
	int n = 0;
	while (pid > 1 && n < 8) {
		out[n++] = pid;
		char path[64], line[512];
		snprintf(path, sizeof(path), "/proc/%u/stat", pid);
		FILE *f = fopen(path, "r");
		if (!f) {
			break;
		}
		const bool read = fgets(line, sizeof(line), f) != NULL;
		fclose(f);
		/* "PID (COMM) STATE PPID ...": COMM may hold spaces and brackets. */
		const char *close = read ? strrchr(line, ')') : NULL;
		unsigned parent = 0;
		if (!close || sscanf(close + 1, " %*c %u", &parent) != 1) {
			break;
		}
		pid = parent;
	}
	return n;
}

static bool view_is_app(struct plat_view *view, const struct keep_awake *k,
		const uint32_t *pids, int n) {
	if (k->app_id[0] && view->app_id && strcmp(view->app_id, k->app_id) == 0) {
		return true;
	}
	const uint32_t pid = view_pid(view);
	for (int i = 0; pid && i < n; i++) {
		if (pids[i] == pid) {
			return true;
		}
	}
	return false;
}

/* An app's request covers this screen: one of its windows shows there, or
 * none of them is found anywhere. */
static bool app_keeps(struct plat_server *server, const struct keep_awake *k, struct wlr_output *o) {
	uint32_t pids[8];
	const int n = pid_and_parents(k->pid, pids);
	struct wlr_box screen;
	wlr_output_layout_get_box(server->output_layout, o, &screen);
	bool found = false;
	struct plat_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (!view->mapped || view->hidden || !view_is_app(view, k, pids, n)) {
			continue;
		}
		found = true;
		struct wlr_box frame = view_frame_box(view), on;
		if (wlr_box_intersection(&on, &frame, &screen)) {
			return true;
		}
	}
	return !found;
}

/* A window fills this screen, or an app asking to stay awake shows on it. */
static bool kept_awake(struct plat_server *server, struct wlr_output *o) {
	struct plat_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view->mapped && !view->hidden && view->fullscreen && view->fullscreen_output == o) {
			return true;
		}
	}
	struct wlr_idle_inhibitor_v1 *inhibitor;
	wl_list_for_each(inhibitor, &server->idle_inhibit->inhibitors, link) {
		struct wlr_surface_output *on;
		wl_list_for_each(on, &inhibitor->surface->current_outputs, link) {
			if (on->output == o) {
				return true;
			}
		}
	}
	struct keep_awake *k;
	wl_list_for_each(k, &server->keep_awake, link) {
		if ((k->what & PLATINUM_SHELL_V1_KEEP_AWAKE_WHAT_SCREENS) && app_keeps(server, k, o)) {
			return true;
		}
	}
	return false;
}

static void set_power(struct plat_output *output, bool on) {
	struct wlr_output *o = output->wlr_output;
	if (!wlr_output_is_drm(o) || o->enabled == on) {
		return;
	}
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, on);
	if (!wlr_output_commit_state(o, &state)) {
		wlr_log(WLR_ERROR, "could not turn %s %s", o->name, on ? "on" : "off");
	} else if (on) {
		wlr_output_schedule_frame(o);
	}
	wlr_output_state_finish(&state);
}

static void write_states(struct plat_server *server) {
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	if (!runtime || !server->socket) {
		return;
	}
	char path[1024], tmp[1100];
	snprintf(path, sizeof(path), "%s/zacos9-idle-%s", runtime, server->socket);
	snprintf(tmp, sizeof(tmp), "%s.new", path);
	FILE *f = fopen(tmp, "w");
	if (!f) {
		return;
	}
	static const char *names[] = { "awake", "saver", "asleep" };
	struct plat_output *output;
	wl_list_for_each_reverse(output, &server->outputs, link) {
		fprintf(f, "output %s %s\n", output->wlr_output->name, names[output->idle_state]);
	}
	fclose(f);
	rename(tmp, path);
}

/* zacos9-screensaver's surfaces show only on screens in its turn: hidden
 * at once when one wakes, before the program itself has heard. */
void idle_show_savers(struct plat_server *server) {
	struct plat_layer_surface *ls;
	wl_list_for_each(ls, &server->layer_surfaces, link) {
		const char *ns = ls->layer_surface->namespace;
		if (!ns || strcmp(ns, "screensaver") != 0) {
			continue;
		}
		bool shown = false;
		struct plat_output *output;
		wl_list_for_each(output, &server->outputs, link) {
			shown |= output->wlr_output == ls->layer_surface->output &&
				output->idle_state == IDLE_SAVER;
		}
		wlr_scene_node_set_enabled(&ls->scene->tree->node, shown);
	}
}

static void suspend(struct plat_server *server) {
	const char *command = getenv("ZACOS9_SUSPEND_COMMAND");
	if (!command) {
		if (!server->session) {
			return; /* nested or headless: never the host's own sleep */
		}
		command = "systemctl suspend";
	}
	if (*command) {
		wlr_log(WLR_INFO, "idle: system sleep");
		spawn(command);
	}
}

void idle_update(struct plat_server *server) {
	if (!server->idle_timer) {
		return; /* outputs can appear before idle_init */
	}
	const struct idle_timeouts t = timeouts();
	const long now = now_ms();
	const long idle = now - server->idle_last_input;
	long next = -1;
	bool any_kept = false, changed = false, saver_due = false;
	if (server->idle_saver_pid > 0 &&
			waitpid(server->idle_saver_pid, NULL, WNOHANG) == server->idle_saver_pid) {
		server->idle_saver_pid = 0; /* it left: no screen needed it */
	}
	struct plat_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		const bool kept = kept_awake(server, output->wlr_output);
		any_kept |= kept;
		const enum idle_state state = idle_output_state(&t, idle, kept);
		if ((int)state != output->idle_state) {
			set_power(output, state != IDLE_ASLEEP);
			saver_due |= state == IDLE_SAVER;
			output->idle_state = state;
			changed = true;
		}
		const long n = idle_output_next(&t, idle, kept);
		if (n >= 0 && (next < 0 || n < next)) {
			next = n;
		}
	}
	/* The system's own clock restarts when it wakes from sleep. */
	const long since = now - (server->idle_slept_at > server->idle_last_input ?
		server->idle_slept_at : server->idle_last_input);
	/* Any app's request keeps the system awake, wherever its windows are. */
	const bool system_kept = any_kept || !wl_list_empty(&server->keep_awake);
	if (idle_system_due(&t, since, system_kept)) {
		server->idle_slept_at = now;
		suspend(server);
	} else {
		const long n = idle_system_next(&t, since, system_kept);
		if (n >= 0 && (next < 0 || n < next)) {
			next = n;
		}
	}
	if ((!wl_list_empty(&server->idle_inhibit->inhibitors) || !wl_list_empty(&server->keep_awake)) &&
			(next < 0 || next > KEPT_RECHECK_MS)) {
		next = KEPT_RECHECK_MS;
	}
	wlr_idle_notifier_v1_set_inhibited(server->idle_notifier, any_kept);
	wl_event_source_timer_update(server->idle_timer, next < 0 ? 0 : next > 0 ? (int)next : 1);
	/* Written when a state changes, and when a screen comes or goes. */
	const int count = wl_list_length(&server->outputs);
	if (changed || count != server->idle_reported) {
		server->idle_reported = count;
		write_states(server);
		idle_show_savers(server);
	}
	/* A screen's turn for the screen saver began: it reads the file just
	 * written. One that died meanwhile isn't restarted until the next. */
	if (saver_due && server->idle_saver_pid <= 0) {
		server->idle_saver_pid = spawn_component("ZACOS9_SCREENSAVER", "zacos9-screensaver", "");
	}
}

bool idle_input(struct plat_server *server, enum idle_input kind) {
	server->idle_last_input = now_ms();
	if (server->idle_notifier) {
		wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
	}
	bool any_dark = false, any_awake = false;
	struct plat_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		any_dark |= output->idle_state != IDLE_AWAKE;
		any_awake |= output->idle_state == IDLE_AWAKE;
	}
	if (!any_dark) {
		return false; /* the usual case: nothing to wake */
	}
	/* A press that wakes a dark screen goes no further: nobody clicks or
	 * types into what they can't see. A click on a screen that was awake,
	 * or a key while any screen was, still counts. */
	bool swallow = false;
	if (kind == IDLE_INPUT_BUTTON) {
		struct plat_output *under = output_at(server, server->cursor->x, server->cursor->y);
		swallow = under && under->idle_state != IDLE_AWAKE;
	} else if (kind == IDLE_INPUT_KEY) {
		swallow = !any_awake;
	}
	idle_update(server);
	return swallow;
}

static int idle_timer_fired(void *data) {
	idle_update(data);
	return 0;
}

static void idle_deferred(void *data) {
	idle_update(data);
}

static void inhibitor_destroy(struct wl_listener *listener, void *data) {
	struct idle_inhibitor *inhibitor = wl_container_of(listener, inhibitor, destroy);
	struct plat_server *server = inhibitor->server;
	wl_list_remove(&inhibitor->destroy.link);
	free(inhibitor);
	/* It is still on the manager's list until this signal returns. */
	wl_event_source_timer_update(server->idle_timer, 1);
}

static void new_inhibitor(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, new_idle_inhibitor);
	struct wlr_idle_inhibitor_v1 *wlr_inhibitor = data;
	struct idle_inhibitor *inhibitor = calloc(1, sizeof(*inhibitor));
	inhibitor->server = server;
	inhibitor->destroy.notify = inhibitor_destroy;
	wl_signal_add(&wlr_inhibitor->events.destroy, &inhibitor->destroy);
	/* Its surface's screens are known once it has been drawn. */
	wl_event_loop_add_idle(wl_display_get_event_loop(server->display), idle_deferred, server);
}

static void keep_awake_destroy(struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct platinum_keep_awake_v1_interface keep_awake_impl = {
	.destroy = keep_awake_destroy,
};

static void keep_awake_gone(struct wl_resource *resource) {
	struct keep_awake *k = wl_resource_get_user_data(resource);
	struct plat_server *server = k->server;
	wl_list_remove(&k->link);
	free(k->app_id);
	free(k);
	/* Not from inside the client's teardown: look again in a moment. */
	wl_event_source_timer_update(server->idle_timer, 1);
}

void idle_keep_awake(struct plat_server *server, struct wl_client *client, uint32_t id,
		uint32_t pid, const char *app_id, uint32_t what) {
	struct wl_resource *resource = wl_resource_create(client, &platinum_keep_awake_v1_interface, 1, id);
	struct keep_awake *k = calloc(1, sizeof(*k));
	if (!resource || !k || !(k->app_id = strdup(app_id ? app_id : ""))) {
		free(k);
		wl_client_post_no_memory(client);
		return;
	}
	k->server = server;
	k->pid = pid;
	k->what = what;
	wl_list_insert(&server->keep_awake, &k->link);
	wl_resource_set_implementation(resource, &keep_awake_impl, k, keep_awake_gone);
	wlr_log(WLR_DEBUG, "idle: kept awake for pid %u app \"%s\"", pid, k->app_id);
	idle_update(server);
}

void idle_init(struct plat_server *server) {
	wl_list_init(&server->keep_awake);
	server->idle_last_input = now_ms();
	server->idle_notifier = wlr_idle_notifier_v1_create(server->display);
	server->idle_inhibit = wlr_idle_inhibit_v1_create(server->display);
	server->new_idle_inhibitor.notify = new_inhibitor;
	wl_signal_add(&server->idle_inhibit->events.new_inhibitor, &server->new_idle_inhibitor);
	server->idle_timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->display),
		idle_timer_fired, server);
	idle_update(server);
}
