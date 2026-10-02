/*
 * The control panels' settings that the compositor applies: mouse
 * tracking speed, double-click speed, key repeat, and the screen's size
 * and scale. They live in ~/.config/platinum/desktop.conf (lib/settings);
 * we watch that folder and apply changes as they happen.
 *
 * For the Monitors panel we also publish each screen's state and modes
 * in $XDG_RUNTIME_DIR/platinum-outputs-$WAYLAND_DISPLAY:
 *   output NAME WxH scale S nested 0|1
 *   mode WxH@mHz
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wlr/backend/wayland.h>
#include <wlr/backend/x11.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_compositor.h>

#include "server.h"
#include "settings.h"

/* Mouse tracking, Very Slow .. Fast (the Mouse panel's slider). */
static const double tracking[] = { 0.4, 0.6, 0.8, 1.0, 1.3, 1.7, 2.2 };
#define TRACKING_DEFAULT 3
/* Key repeat rate (keys per second; 0 = off) and delay until repeat
 * (ms; 0 = off), as the Keyboard panel's sliders. */
static const int repeat_rates[] = { 0, 2, 4, 8, 15, 25, 35 };
#define REPEAT_DEFAULT 5
static const int repeat_delays[] = { 0, 1100, 800, 600, 400, 250 };
#define DELAY_DEFAULT 3

static int setting_int(const char *key, int fallback, int min, int max) {
	char v[32];
	if (!pl_setting(key, v, sizeof(v))) {
		return fallback;
	}
	char *end;
	long n = strtol(v, &end, 10);
	return *end || n < min || n > max ? fallback : (int)n;
}

int prefs_repeat_rate(void) {
	const int rate = repeat_rates[setting_int("key-repeat", REPEAT_DEFAULT, 0, 6)];
	const int delay = repeat_delays[setting_int("key-delay", DELAY_DEFAULT, 0, 5)];
	return delay ? rate : 0;
}

int prefs_repeat_delay(void) {
	const int delay = repeat_delays[setting_int("key-delay", DELAY_DEFAULT, 0, 5)];
	return delay ? delay : 600;
}

static bool nested(struct wlr_output *o) {
	return wlr_output_is_wl(o) || wlr_output_is_x11(o);
}

void prefs_write_outputs(struct plat_server *server) {
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	const char *display = getenv("WAYLAND_DISPLAY");
	if (!runtime || !display) {
		return;
	}
	char path[1024], tmp[1100];
	snprintf(path, sizeof(path), "%s/platinum-outputs-%s", runtime, display);
	snprintf(tmp, sizeof(tmp), "%s.new", path);
	FILE *f = fopen(tmp, "w");
	if (!f) {
		return;
	}
	struct plat_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		struct wlr_output *o = output->wlr_output;
		fprintf(f, "output %s %dx%d scale %d nested %d\n", o->name, o->width, o->height,
			(int)o->scale, nested(o) ? 1 : 0);
		struct wlr_output_mode *mode;
		wl_list_for_each(mode, &o->modes, link) {
			fprintf(f, "mode %dx%d@%d\n", mode->width, mode->height, mode->refresh);
		}
	}
	fclose(f);
	rename(tmp, path);
}

static void prefer_scale(struct wlr_scene_buffer *buffer, int sx, int sy, void *data) {
	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(buffer);
	if (scene_surface) {
		wlr_surface_set_preferred_buffer_scale(scene_surface->surface, *(int *)data);
	}
}

/* wlroots tells a surface its preferred buffer scale when it enters an
 * output, not when the output's scale changes; Qt then draws at the new
 * scale but labels its buffers with the old one. Tell everyone. */
static void refresh_surface_scales(struct plat_server *server, int scale) {
	wlr_scene_node_for_each_buffer(&server->scene->tree.node, prefer_scale, &scale);
}

/* A screen's scale until one is chosen: 2x when it is dense enough
 * (180 dpi and up, such as a Retina laptop) that 1x pixels would be too
 * small to use, so it shows a classic-sized desktop at double size. */
static int automatic_scale(struct plat_server *server, struct wlr_output *o) {
	if (server->scale_explicit || nested(o) || o->phys_width <= 0) {
		return server->default_scale;
	}
	const double dpi = o->width * 25.4 / o->phys_width;
	return dpi >= 180 ? 2 : 1;
}

/* Apply the chosen scale and resolution to one output. */
static void apply_output(struct plat_server *server, struct plat_output *output) {
	struct wlr_output *o = output->wlr_output;
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	bool changed = false;

	const int scale = setting_int("scale", automatic_scale(server, o), 1, 3);
	if ((int)o->scale != scale) {
		wlr_output_state_set_scale(&state, scale);
		changed = true;
	}
	char res[32];
	int w, h;
	if (pl_setting("resolution", res, sizeof(res)) && sscanf(res, "%dx%d", &w, &h) == 2 &&
			w >= 640 && h >= 480 && (w != o->width || h != o->height)) {
		struct wlr_output_mode *mode, *best = NULL;
		wl_list_for_each(mode, &o->modes, link) {
			if (mode->width == w && mode->height == h &&
					(!best || mode->refresh > best->refresh)) {
				best = mode;
			}
		}
		if (best) {
			wlr_output_state_set_mode(&state, best);
			changed = true;
		} else if (nested(o)) {
			/* A nested session's screen is a window: any size will do. */
			wlr_output_state_set_custom_mode(&state, w, h, 0);
			changed = true;
		}
	}
	if (changed && wlr_output_commit_state(o, &state)) {
		server->output_scale = scale;
		wlr_xcursor_manager_load(server->cursor_mgr, scale);
		layers_arrange(output);
		refresh_surface_scales(server, scale);
	}
	wlr_output_state_finish(&state);
}

void prefs_apply(struct plat_server *server) {
	server->pointer_speed = tracking[setting_int("mouse-speed", TRACKING_DEFAULT, 0, 6)];
	server->double_click_ms = setting_int("double-click", PLAT_DOUBLE_CLICK_MS, 150, 2000);

	struct plat_keyboard *kb;
	wl_list_for_each(kb, &server->keyboards, link) {
		if (!kb->is_virtual) {
			wlr_keyboard_set_repeat_info(kb->wlr_keyboard, prefs_repeat_rate(),
				prefs_repeat_delay());
		}
	}
	struct plat_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		apply_output(server, output);
	}
	prefs_write_outputs(server);
}

static int settings_changed(int fd, uint32_t mask, void *data) {
	char events[4096];
	while (read(fd, events, sizeof(events)) > 0) {
	}
	prefs_apply(data);
	return 0;
}

void prefs_init(struct plat_server *server) {
	server->default_scale = server->output_scale;
	server->pointer_speed = 1.0;
	server->double_click_ms = PLAT_DOUBLE_CLICK_MS;

	int fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
	if (fd < 0) {
		return;
	}
	const char *config = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	char dir[1024];
	if (config && *config) {
		snprintf(dir, sizeof(dir), "%s/platinum", config);
	} else {
		snprintf(dir, sizeof(dir), "%s/.config/platinum", home ? home : "");
	}
	mkdir(dir, 0755);
	inotify_add_watch(fd, dir, IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE);
	wl_event_loop_add_fd(wl_display_get_event_loop(server->display), fd, WL_EVENT_READABLE,
		settings_changed, server);
}
