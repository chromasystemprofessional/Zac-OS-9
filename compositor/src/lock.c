/*
 * Locking the screens (ext-session-lock-v1): "Ask for my password to wake"
 * in the Screen Saver panel ("require-password=1" in desktop.conf).
 *
 * zacos9-wm starts zacos9-lock (shell/lock/) when every screen has gone to
 * the screen saver or to sleep, before the system sleeps, and when asked
 * (platinum_shell_v1.lock: logind's Lock and PrepareForSleep, the D-Bus
 * ScreenSaver's Lock, through zacos9-inhibit). While locked:
 *
 * - everything but the lock is hidden (windows, desktop, menu bar, drag
 *   icons), over a black background, so nothing shows before the lock
 *   draws; the screen saver still plays above it on screens in its turn;
 * - only the lock's surfaces get the keyboard and the pointer; keyboard
 *   shortcuts are off but for switching to a text console;
 * - if zacos9-lock dies without unlocking, the screens stay locked and
 *   blank, and it is started again.
 *
 * Only the zacos9-lock zacos9-wm started may lock (and so unlock): any
 * other client asking is refused, so nothing else in the session can take
 * the lock's place, while it starts or after it died, and unlock at will.
 *
 * See docs/energy-saver.md.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/util/log.h>

#include "server.h"
#include "settings.h"

#define RESTART_MS 1000

struct lock_surface {
	struct wl_list link; /* plat_server.lock_surfaces */
	struct plat_server *server;
	struct wlr_session_lock_surface_v1 *lock_surface;
	struct wlr_scene_tree *tree;
	struct wl_listener map;
	struct wl_listener destroy;
};

/* Everything a lock hides, bottom to top. */
static void show_session(struct plat_server *server, bool shown) {
	wlr_scene_node_set_enabled(&server->desktop->node, shown);
	for (int i = 0; i < 4; i++) {
		wlr_scene_node_set_enabled(&server->shell_layers[i]->node, shown);
	}
	wlr_scene_node_set_enabled(&server->view_layer->node, shown);
	wlr_scene_node_set_enabled(&server->popup_layer->node, shown);
	wlr_scene_node_set_enabled(&server->fullscreen_layer->node, shown);
	wlr_scene_node_set_enabled(&server->dialog_layer->node, shown);
	wlr_scene_node_set_enabled(&server->unmanaged_layer->node, shown);
	wlr_scene_node_set_enabled(&server->overlay_layer->node, shown);
	wlr_scene_node_set_enabled(&server->lock_layer->node, !shown);
}

bool lock_active(struct plat_server *server) {
	return server->locked;
}

bool lock_owns(struct plat_server *server, struct wlr_surface *surface) {
	return surface && wlr_session_lock_surface_v1_try_from_wlr_surface(surface) != NULL;
}

struct wlr_surface *lock_surface_at(struct plat_server *server, double lx, double ly,
		double *sx, double *sy) {
	struct wlr_scene_node *node = wlr_scene_node_at(&server->lock_layer->node, lx, ly, sx, sy);
	if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}
	struct wlr_scene_surface *scene_surface =
		wlr_scene_surface_try_from_buffer(wlr_scene_buffer_from_node(node));
	return scene_surface && lock_owns(server, scene_surface->surface) ? scene_surface->surface : NULL;
}

/* The keyboard to the lock on the screen under the pointer, else any. */
void lock_focus(struct plat_server *server) {
	struct plat_output *under = output_at(server, server->cursor->x, server->cursor->y);
	struct lock_surface *ls, *chosen = NULL;
	wl_list_for_each(ls, &server->lock_surfaces, link) {
		if (!ls->lock_surface->surface->mapped) {
			continue;
		}
		if (!chosen || (under && ls->lock_surface->output == under->wlr_output)) {
			chosen = ls;
		}
	}
	if (chosen) {
		input_keyboard_enter(server, chosen->lock_surface->surface);
	} else {
		wlr_seat_keyboard_clear_focus(server->seat);
	}
}

/* Requests waiting for the screens to be locked (platinum_shell_v1.lock). */
static void finish_waiters(struct plat_server *server) {
	struct wl_resource *callback, *tmp;
	wl_resource_for_each_safe(callback, tmp, &server->lock_waiters) {
		wl_callback_send_done(callback, 0);
		wl_resource_destroy(callback);
	}
}

static void start_client(struct plat_server *server) {
	if (server->lock_pid > 0 && waitpid(server->lock_pid, NULL, WNOHANG) == 0) {
		return; /* still starting */
	}
	server->lock_pid = spawn_component("ZACOS9_LOCK", "zacos9-lock", "");
	if (server->lock_pid <= 0) {
		wlr_log(WLR_ERROR, "lock: couldn't start zacos9-lock");
	}
}

static int restart_client(void *data) {
	struct plat_server *server = data;
	if (server->locked && !server->lock) {
		start_client(server);
	}
	return 0;
}

static void surface_map(struct wl_listener *listener, void *data) {
	struct lock_surface *ls = wl_container_of(listener, ls, map);
	lock_focus(ls->server);
}

static void surface_destroy(struct wl_listener *listener, void *data) {
	struct lock_surface *ls = wl_container_of(listener, ls, destroy);
	struct plat_server *server = ls->server;
	wl_list_remove(&ls->link);
	wl_list_remove(&ls->map.link);
	wl_list_remove(&ls->destroy.link);
	wlr_scene_node_destroy(&ls->tree->node);
	free(ls);
	if (server->locked) {
		lock_focus(server);
	}
}

static void new_surface(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, lock_new_surface);
	struct wlr_session_lock_surface_v1 *lock_surface = data;
	struct lock_surface *ls = calloc(1, sizeof(*ls));
	if (!ls) {
		/* Still locked, and blank there: the safe way to fail. */
		wlr_log(WLR_ERROR, "lock: out of memory for a lock surface");
		return;
	}
	ls->server = server;
	ls->lock_surface = lock_surface;
	ls->tree = wlr_scene_subsurface_tree_create(server->lock_layer, lock_surface->surface);
	struct wlr_box box;
	wlr_output_layout_get_box(server->output_layout, lock_surface->output, &box);
	wlr_scene_node_set_position(&ls->tree->node, box.x, box.y);
	wlr_session_lock_surface_v1_configure(lock_surface, box.width, box.height);
	ls->map.notify = surface_map;
	wl_signal_add(&lock_surface->surface->events.map, &ls->map);
	ls->destroy.notify = surface_destroy;
	wl_signal_add(&lock_surface->events.destroy, &ls->destroy);
	wl_list_insert(&server->lock_surfaces, &ls->link);
}

static void forget_lock(struct plat_server *server) {
	wl_list_remove(&server->lock_new_surface.link);
	wl_list_remove(&server->lock_unlock.link);
	wl_list_remove(&server->lock_destroy.link);
	server->lock = NULL;
}

static void lock_unlock(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, lock_unlock);
	forget_lock(server);
	server->locked = false;
	show_session(server, true);
	wlr_log(WLR_INFO, "lock: unlocked");
	/* The keyboard back where it was. */
	wlr_seat_keyboard_clear_focus(server->seat);
	if (server->focused_layer) {
		layers_focus(server->focused_layer);
	} else if (server->focused_view) {
		input_keyboard_enter(server, server->focused_view->impl->get_surface(server->focused_view));
	}
	idle_lock_changed(server);
	input_pointer_refocus(server);
}

static void lock_destroy(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, lock_destroy);
	forget_lock(server);
	if (server->locked) {
		/* Gone without unlocking (it crashed, or was killed): stay locked,
		 * blank, and bring it back. */
		wlr_log(WLR_ERROR, "lock: zacos9-lock went away while locked; starting it again");
		wl_event_source_timer_update(server->lock_restart, RESTART_MS);
	}
}

/* Whether a client is the zacos9-lock start_client() started: that very
 * process, or, when /bin/sh -c didn't exec it ($ZACOS9_LOCK), its child. */
static bool is_our_lock(struct plat_server *server, struct wl_client *client) {
	pid_t pid = 0;
	wl_client_get_credentials(client, &pid, NULL, NULL);
	if (server->lock_pid <= 0 || pid <= 0) {
		return false;
	}
	if (pid == server->lock_pid) {
		return true;
	}
	char path[64];
	snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
	FILE *f = fopen(path, "r");
	if (!f) {
		return false;
	}
	char buf[512];
	const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = '\0';
	/* "pid (comm) state ppid ...": comm may hold spaces and parentheses. */
	const char *end = strrchr(buf, ')');
	int ppid = 0;
	return end && sscanf(end + 1, " %*c %d", &ppid) == 1 && ppid == server->lock_pid;
}

static void new_lock(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, lock_new);
	struct wlr_session_lock_v1 *lock = data;
	if (server->lock) {
		wlr_session_lock_v1_destroy(lock); /* one lock at a time */
		return;
	}
	struct wl_client *client = wl_resource_get_client(lock->resource);
	if (!is_our_lock(server, client)) {
		pid_t pid = 0;
		wl_client_get_credentials(client, &pid, NULL, NULL);
		wlr_log(WLR_ERROR, "lock: refused a lock from process %d, not zacos9-lock", (int)pid);
		wlr_session_lock_v1_destroy(lock);
		return;
	}
	server->lock = lock;
	server->lock_new_surface.notify = new_surface;
	wl_signal_add(&lock->events.new_surface, &server->lock_new_surface);
	server->lock_unlock.notify = lock_unlock;
	wl_signal_add(&lock->events.unlock, &server->lock_unlock);
	server->lock_destroy.notify = lock_destroy;
	wl_signal_add(&lock->events.destroy, &server->lock_destroy);
	if (!server->locked) {
		input_cancel_grab(server);
		server->locked = true;
		show_session(server, false);
		wlr_seat_keyboard_clear_focus(server->seat);
		wlr_seat_pointer_clear_focus(server->seat);
		wlr_log(WLR_INFO, "lock: locked");
	}
	/* Nothing but black and the lock shows from here: locked. */
	wlr_session_lock_v1_send_locked(lock);
	finish_waiters(server);
	idle_lock_changed(server);
	input_pointer_refocus(server);
}

bool lock_required(void) {
	char v[16];
	return pl_setting("require-password", v, sizeof(v)) && strcmp(v, "1") == 0;
}

void lock_start(struct plat_server *server) {
	if (!server->locked) {
		start_client(server);
	}
}

static void waiter_gone(struct wl_resource *callback) {
	wl_list_remove(wl_resource_get_link(callback));
}

void lock_request(struct plat_server *server, struct wl_client *client, uint32_t id, bool always) {
	struct wl_resource *callback = wl_resource_create(client, &wl_callback_interface, 1, id);
	if (!callback) {
		wl_client_post_no_memory(client);
		return;
	}
	if (server->locked || !(always || lock_required())) {
		/* Locked already, or not to be: nothing to wait for. */
		wl_callback_send_done(callback, 0);
		wl_resource_destroy(callback);
		return;
	}
	wl_list_insert(&server->lock_waiters, wl_resource_get_link(callback));
	wl_resource_set_implementation(callback, NULL, NULL, waiter_gone);
	lock_start(server);
}

void lock_init(struct plat_server *server) {
	wl_list_init(&server->lock_surfaces);
	wl_list_init(&server->lock_waiters);
	server->lock_mgr = wlr_session_lock_manager_v1_create(server->display);
	server->lock_new.notify = new_lock;
	wl_signal_add(&server->lock_mgr->events.new_lock, &server->lock_new);
	server->lock_restart = wl_event_loop_add_timer(wl_display_get_event_loop(server->display),
		restart_client, server);
	/* Black under the lock, so nothing shows before it draws. */
	wlr_scene_rect_create(server->lock_layer, 16384, 16384, (float[4]){ 0, 0, 0, 1 });
	wlr_scene_node_set_enabled(&server->lock_layer->node, false);
}
