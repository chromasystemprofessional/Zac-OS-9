/*
 * The control panels' settings that the compositor applies: mouse
 * tracking speed, double-click speed, key repeat, and the screen's size
 * and scale. They live in ~/.config/zacos9/desktop.conf (lib/settings);
 * we watch that folder and apply changes as they happen.
 *
 * For the Monitors panel we also publish each screen's state and modes
 * in $XDG_RUNTIME_DIR/zacos9-outputs-$WAYLAND_DISPLAY:
 *   output NAME WxH scale S nested 0|1 x X y Y main 0|1
 *   mode WxH@mHz
 * and, once, "mirror 0|1". X and Y are the screen's place in the layout
 * (logical pixels: the size divided by the scale).
 *
 * Several screens: "main-display=NAME" has the menu bar and desktop icons;
 * "display.NAME.resolution=WxH" and "display.NAME.x|y=N" set a screen's
 * size and place; "mirror=1" puts every screen at the same place.
 */
#define _GNU_SOURCE
#include <stdint.h>
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
	snprintf(path, sizeof(path), "%s/zacos9-outputs-%s", runtime, display);
	snprintf(tmp, sizeof(tmp), "%s.new", path);
	FILE *f = fopen(tmp, "w");
	if (!f) {
		return;
	}
	struct plat_output *output;
	struct plat_output *main = output_main(server);
	fprintf(f, "mirror %d\n", setting_int("mirror", 0, 0, 1));
	wl_list_for_each_reverse(output, &server->outputs, link) {
		struct wlr_output *o = output->wlr_output;
		struct wlr_box box = { 0 };
		wlr_output_layout_get_box(server->output_layout, o, &box);
		fprintf(f, "output %s %dx%d scale %d nested %d x %d y %d main %d\n", o->name, o->width,
			o->height, (int)o->scale, nested(o) ? 1 : 0, box.x, box.y, output == main ? 1 : 0);
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
	char res[32], key[96];
	int w, h;
	snprintf(key, sizeof(key), "display.%s.resolution", o->name);
	if ((pl_setting(key, res, sizeof(res)) || pl_setting("resolution", res, sizeof(res))) && sscanf(res, "%dx%d", &w, &h) == 2 &&
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

static int display_pos(struct wlr_output *o, const char *axis, int fallback) {
	char key[96];
	snprintf(key, sizeof(key), "display.%s.%s", o->name, axis);
	return setting_int(key, fallback, -100000, 100000);
}

static void logical_size(struct wlr_output *o, int *w, int *h) {
	int ow, oh;
	wlr_output_effective_resolution(o, &ow, &oh);
	*w = ow;
	*h = oh;
}

/* Where each screen sits: all at the origin when mirrored; else where the
 * Monitors panel put them, if that is a layout without overlaps; else side
 * by side, left to right, in the order they were connected. */
static void apply_layout(struct plat_server *server) {
	struct plat_output *output;
	struct wlr_output *outs[16];
	int n = 0;
	wl_list_for_each_reverse(output, &server->outputs, link) {
		if (n < 16) {
			outs[n++] = output->wlr_output;
		}
	}
	if (n == 0) {
		return;
	}
	int xs[16], ys[16];
	bool saved = true;
	for (int i = 0; i < n; i++) {
		xs[i] = display_pos(outs[i], "x", INT32_MIN);
		ys[i] = display_pos(outs[i], "y", INT32_MIN);
		saved = saved && xs[i] != INT32_MIN && ys[i] != INT32_MIN;
	}
	if (setting_int("mirror", 0, 0, 1)) {
		for (int i = 0; i < n; i++) {
			xs[i] = ys[i] = 0;
		}
	} else {
		bool overlap = false;
		for (int i = 0; saved && i < n; i++) {
			int wi, hi;
			logical_size(outs[i], &wi, &hi);
			for (int j = i + 1; j < n; j++) {
				int wj, hj;
				logical_size(outs[j], &wj, &hj);
				if (xs[i] < xs[j] + wj && xs[j] < xs[i] + wi &&
						ys[i] < ys[j] + hj && ys[j] < ys[i] + hi) {
					overlap = true;
				}
			}
		}
		if (!saved || overlap) {
			/* Left to right, by the saved place where there is one. */
			int order[16];
			for (int i = 0; i < n; i++) {
				order[i] = i;
			}
			for (int i = 1; i < n; i++) {
				for (int j = i; j > 0 && saved && xs[order[j]] < xs[order[j - 1]]; j--) {
					int t = order[j];
					order[j] = order[j - 1];
					order[j - 1] = t;
				}
			}
			int x = 0;
			for (int k = 0; k < n; k++) {
				int w, h;
				logical_size(outs[order[k]], &w, &h);
				xs[order[k]] = x;
				ys[order[k]] = 0;
				x += w;
			}
		}
	}
	for (int i = 0; i < n; i++) {
		wlr_output_layout_add(server->output_layout, outs[i], xs[i], ys[i]);
	}
	wl_list_for_each(output, &server->outputs, link) {
		layers_arrange(output);
	}
	layers_pin_to_main(server);
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
	apply_layout(server);
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
		snprintf(dir, sizeof(dir), "%s/zacos9", config);
	} else {
		snprintf(dir, sizeof(dir), "%s/.config/zacos9", home ? home : "");
	}
	mkdir(dir, 0755);
	inotify_add_watch(fd, dir, IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE);
	wl_event_loop_add_fd(wl_display_get_event_loop(server->display), fd, WL_EVENT_READABLE,
		settings_changed, server);
}
