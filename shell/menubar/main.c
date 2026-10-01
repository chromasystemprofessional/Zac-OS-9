/*
 * platinum-menubar: the Mac OS 8/9 menu bar for platinum-wm.
 *
 * The bar is a wlr-layer-shell surface at the top of the screen with a
 * 20 px exclusive zone. While a menu is open, a transparent full-screen
 * overlay surface takes the pointer and keyboard (classic menus are modal);
 * the menu itself is a small subsurface of it, so redraws stay cheap.
 *
 * Tracking follows Mac OS 8: press a title and drag to an item; or click a
 * title and the menu stays open ("sticky") until the next click.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#include <wayland-cursor.h>

#include "menubar.h"
#include "viewporter-client-protocol.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

/* Chosen items blink before the menu closes (General Controls default).
 * TODO: verify count and timing on a real Mac. */
#define BLINK_COUNT 1
#define BLINK_MS 60

enum side { SIDE_LEFT, SIDE_RIGHT };

static struct {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_subcompositor *subcompositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_output *output;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct zwlr_foreign_toplevel_manager_v1 *toplevel_mgr;
	struct wp_viewporter *viewporter;
	int scale;

	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;
	struct wl_cursor_theme *cursor_theme;
	struct wl_surface *cursor_surface;
	double px, py; /* pointer, in screen (= surface) coordinates */

	/* The bar. */
	struct wl_surface *bar_surface;
	struct zwlr_layer_surface_v1 *bar_layer;
	int width; /* logical screen width */
	bool bar_configured;

	/* The overlay that exists while a menu is open. */
	struct wl_surface *overlay;
	struct zwlr_layer_surface_v1 *overlay_layer;
	struct wp_viewport *overlay_viewport;
	struct wl_surface *menu_surface;
	struct wl_subsurface *menu_sub;
	bool overlay_configured;
	int menu_x, menu_w, menu_h;

	struct mb_bar model;
	bool rebuild_pending;

	/* Tracking. */
	enum side open_side;
	int open_index; /* -1 when closed */
	int selected;   /* item index, or -1 */
	bool button_down;
	int blinks_left;
	bool blink_on;
	struct mb_item chosen; /* copy of the item being performed */

	int clock_fd, blink_fd;
	bool running;
} g = { .scale = 1, .open_index = -1, .selected = -1, .clock_fd = -1, .blink_fd = -1 };

struct wl_seat *menubar_seat(void) {
	return g.seat;
}

/* ---- shm buffers --------------------------------------------------------- */

struct buffer {
	struct wl_buffer *wl;
	void *data;
	size_t size;
};

static void buffer_release(void *data, struct wl_buffer *wl_buffer) {
	struct buffer *b = data;
	wl_buffer_destroy(b->wl);
	munmap(b->data, b->size);
	free(b);
}

static const struct wl_buffer_listener buffer_listener = { .release = buffer_release };

/* Upscale a logical 1x ARGB image into a new shm buffer at the output
 * scale (nearest neighbour: pixel art stays crisp). */
static struct wl_buffer *make_buffer(const uint32_t *src, int w, int h) {
	int s = g.scale, bw = w * s, bh = h * s;
	size_t size = (size_t)bw * bh * 4;
	int fd = memfd_create("platinum-menubar", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, size) < 0) {
		if (fd >= 0) {
			close(fd);
		}
		return NULL;
	}
	uint32_t *dst = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (dst == MAP_FAILED) {
		close(fd);
		return NULL;
	}
	for (int y = 0; y < bh; y++) {
		const uint32_t *row = src + (y / s) * w;
		for (int x = 0; x < bw; x++) {
			dst[y * bw + x] = row[x / s];
		}
	}
	struct wl_shm_pool *pool = wl_shm_create_pool(g.shm, fd, size);
	struct buffer *b = calloc(1, sizeof(*b));
	b->wl = wl_shm_pool_create_buffer(pool, 0, bw, bh, bw * 4, WL_SHM_FORMAT_ARGB8888);
	b->data = dst;
	b->size = size;
	wl_shm_pool_destroy(pool);
	close(fd);
	wl_buffer_add_listener(b->wl, &buffer_listener, b);
	return b->wl;
}

static void present(struct wl_surface *surface, const uint32_t *px, int w, int h) {
	struct wl_buffer *buf = make_buffer(px, w, h);
	if (!buf) {
		return;
	}
	wl_surface_set_buffer_scale(surface, g.scale);
	wl_surface_attach(surface, buf, 0, 0);
	wl_surface_damage_buffer(surface, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_commit(surface);
}

/* ---- drawing ------------------------------------------------------------- */

static struct mb_menu *open_menu_ptr(void) {
	if (g.open_index < 0) {
		return NULL;
	}
	return g.open_side == SIDE_LEFT ? &g.model.left[g.open_index]
		: &g.model.right[g.open_index];
}

static struct mbar_title *open_title_ptr(void) {
	if (g.open_index < 0) {
		return NULL;
	}
	return g.open_side == SIDE_LEFT ? &g.model.left_titles[g.open_index]
		: &g.model.right_titles[g.open_index];
}

static void draw_bar(void) {
	if (!g.bar_configured || g.width <= 0) {
		return;
	}
	uint32_t *px = calloc((size_t)g.width * MBAR_HEIGHT, sizeof(*px));
	struct pl_canvas c = { .px = px, .stride = g.width, .width = g.width, .height = MBAR_HEIGHT };
	mbar_paint_background(&c, g.width);
	int hl = g.open_side == SIDE_LEFT ? g.open_index : -1;
	int hr = g.open_side == SIDE_RIGHT ? g.open_index : -1;
	mbar_paint_titles(&c, g.model.left_titles, g.model.n_left, hl, PL_ACCENT_DEFAULT);
	mbar_paint_titles(&c, g.model.right_titles, g.model.n_right, hr, PL_ACCENT_DEFAULT);
	present(g.bar_surface, px, g.width, MBAR_HEIGHT);
	free(px);
}

static void to_menu_items(const struct mb_menu *m, struct menu_item *out) {
	for (int i = 0; i < m->n; i++) {
		out[i] = (struct menu_item){
			.label = m->items[i].label_text,
			.key = m->items[i].key_text,
			.enabled = m->items[i].enabled,
			.checked = m->items[i].checked,
		};
	}
}

static void draw_menu(void) {
	struct mb_menu *m = open_menu_ptr();
	if (!m || !g.overlay_configured) {
		return;
	}
	struct menu_item items[MAX_ITEMS];
	to_menu_items(m, items);
	menu_measure(items, m->n, &g.menu_w, &g.menu_h);

	/* The menu hangs from its title, kept on screen at the right edge. */
	g.menu_x = open_title_ptr()->hi_l;
	if (g.menu_x + g.menu_w + MENU_SHADOW > g.width) {
		g.menu_x = g.width - g.menu_w - MENU_SHADOW;
	}
	if (g.menu_x < 0) {
		g.menu_x = 0;
	}

	int w = g.menu_w + MENU_SHADOW, h = g.menu_h + MENU_SHADOW;
	uint32_t *px = calloc((size_t)w * h, sizeof(*px));
	struct pl_canvas c = { .px = px, .stride = w, .width = w, .height = h };
	int sel = g.blinks_left > 0 && !g.blink_on ? -1 : g.selected;
	menu_paint(&c, items, m->n, g.menu_w, g.menu_h, sel, PL_ACCENT_DEFAULT);

	wl_subsurface_set_position(g.menu_sub, g.menu_x, MBAR_HEIGHT - 1);
	present(g.menu_surface, px, w, h);
	free(px);
	wl_surface_commit(g.overlay); /* applies the subsurface position */
}

/* ---- the overlay --------------------------------------------------------- */

static void overlay_configure(void *data, struct zwlr_layer_surface_v1 *layer,
		uint32_t serial, uint32_t w, uint32_t h) {
	zwlr_layer_surface_v1_ack_configure(layer, serial);
	/* A single transparent pixel, stretched over the screen, catches input. */
	static const uint32_t clear = 0;
	wp_viewport_set_destination(g.overlay_viewport, (int)w, (int)h);
	struct wl_buffer *buf = make_buffer(&clear, 1, 1);
	wl_surface_set_buffer_scale(g.overlay, 1);
	wl_surface_attach(g.overlay, buf, 0, 0);
	wl_surface_damage_buffer(g.overlay, 0, 0, INT32_MAX, INT32_MAX);
	g.overlay_configured = true;
	draw_menu();
	wl_surface_commit(g.overlay);
}

static void overlay_closed(void *data, struct zwlr_layer_surface_v1 *layer) {
	g.open_index = -1;
}

static const struct zwlr_layer_surface_v1_listener overlay_listener = {
	.configure = overlay_configure,
	.closed = overlay_closed,
};

static void create_overlay(void) {
	if (g.overlay) {
		return;
	}
	g.overlay = wl_compositor_create_surface(g.compositor);
	g.overlay_viewport = wp_viewporter_get_viewport(g.viewporter, g.overlay);
	g.overlay_layer = zwlr_layer_shell_v1_get_layer_surface(g.layer_shell, g.overlay,
		g.output, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "platinum-menu");
	zwlr_layer_surface_v1_set_anchor(g.overlay_layer,
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(g.overlay_layer, -1);
	zwlr_layer_surface_v1_set_keyboard_interactivity(g.overlay_layer,
		ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
	zwlr_layer_surface_v1_add_listener(g.overlay_layer, &overlay_listener, NULL);

	g.menu_surface = wl_compositor_create_surface(g.compositor);
	g.menu_sub = wl_subcompositor_get_subsurface(g.subcompositor, g.menu_surface, g.overlay);
	wl_subsurface_set_desync(g.menu_sub);
	/* Pointer events all go to the overlay, in screen coordinates. */
	struct wl_region *none = wl_compositor_create_region(g.compositor);
	wl_surface_set_input_region(g.menu_surface, none);
	wl_region_destroy(none);

	g.overlay_configured = false;
	wl_surface_commit(g.overlay);
}

static void destroy_overlay(void) {
	if (!g.overlay) {
		return;
	}
	wl_subsurface_destroy(g.menu_sub);
	wl_surface_destroy(g.menu_surface);
	zwlr_layer_surface_v1_destroy(g.overlay_layer);
	wp_viewport_destroy(g.overlay_viewport);
	wl_surface_destroy(g.overlay);
	g.overlay = NULL;
	g.overlay_configured = false;
}

/* ---- tracking ------------------------------------------------------------ */

static void rebuild(void) {
	menus_rebuild(&g.model, g.width);
	g.rebuild_pending = false;
	draw_bar();
}

void menubar_apps_changed(void) {
	/* Never reshuffle menus under the user's pointer. */
	if (g.open_index >= 0) {
		g.rebuild_pending = true;
	} else if (g.bar_configured) {
		rebuild();
	}
}

static bool title_at(double x, double y, enum side *side, int *index) {
	if (y >= MBAR_HEIGHT) {
		return false;
	}
	int i = mbar_title_at(g.model.left_titles, g.model.n_left, (int)x);
	if (i >= 0) {
		*side = SIDE_LEFT;
		*index = i;
		return true;
	}
	i = mbar_title_at(g.model.right_titles, g.model.n_right, (int)x);
	if (i >= 0 && g.model.right[i].n > 0) {
		*side = SIDE_RIGHT;
		*index = i;
		return true;
	}
	return false;
}

static void open_menu(enum side side, int index) {
	g.open_side = side;
	g.open_index = index;
	g.selected = -1;
	create_overlay();
	draw_bar();
	draw_menu();
}

static void close_menu(void) {
	if (g.open_index < 0) {
		return;
	}
	g.open_index = -1;
	g.selected = -1;
	g.button_down = false;
	destroy_overlay();
	if (g.rebuild_pending) {
		rebuild();
	} else {
		draw_bar();
	}
}

static int item_under_pointer(void) {
	struct mb_menu *m = open_menu_ptr();
	if (!m) {
		return -1;
	}
	int lx = (int)g.px - g.menu_x, ly = (int)g.py - (MBAR_HEIGHT - 1);
	if (lx < 1 || lx > g.menu_w - 2) {
		return -1;
	}
	struct menu_item items[MAX_ITEMS];
	to_menu_items(m, items);
	int i = menu_item_at(items, m->n, g.menu_h, ly);
	return i >= 0 && m->items[i].enabled ? i : -1;
}

static void arm_timer(int fd, long ms) {
	struct itimerspec its = {
		.it_value = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L },
	};
	timerfd_settime(fd, 0, &its, NULL);
}

static void choose(int index) {
	struct mb_item *it = &open_menu_ptr()->items[index];
	g.chosen = (struct mb_item){
		.action = it->action,
		.arg = it->arg ? strdup(it->arg) : NULL,
	};
	g.selected = index;
	g.blinks_left = BLINK_COUNT * 2;
	g.blink_on = false;
	draw_menu();
	arm_timer(g.blink_fd, BLINK_MS);
}

static void blink_tick(void) {
	if (g.blinks_left <= 0) {
		return;
	}
	g.blinks_left--;
	g.blink_on = !g.blink_on;
	if (g.blinks_left > 0) {
		draw_menu();
		arm_timer(g.blink_fd, BLINK_MS);
		return;
	}
	close_menu();
	menus_perform(&g.chosen);
	free(g.chosen.arg);
	g.chosen.arg = NULL;
}

static void pointer_moved(void) {
	if (g.open_index < 0 || g.blinks_left > 0) {
		return;
	}
	enum side side;
	int index;
	if (title_at(g.px, g.py, &side, &index)) {
		if (side != g.open_side || index != g.open_index) {
			open_menu(side, index);
		} else if (g.selected != -1) {
			g.selected = -1;
			draw_menu();
		}
		return;
	}
	int sel = item_under_pointer();
	if (sel != g.selected) {
		g.selected = sel;
		draw_menu();
	}
}

static void pointer_pressed(void) {
	if (g.blinks_left > 0) {
		return;
	}
	enum side side;
	int index;
	bool on_title = title_at(g.px, g.py, &side, &index);
	if (g.open_index < 0) {
		if (on_title && (side == SIDE_LEFT ? g.model.left[index].n : g.model.right[index].n)) {
			g.button_down = true;
			open_menu(side, index);
		}
		return;
	}
	/* A menu is open in sticky mode. */
	if (on_title) {
		if (side == g.open_side && index == g.open_index) {
			close_menu();
		} else {
			g.button_down = true;
			open_menu(side, index);
		}
	} else if (item_under_pointer() >= 0) {
		g.button_down = true;
	} else {
		close_menu();
	}
}

static void pointer_released(void) {
	bool was_down = g.button_down;
	g.button_down = false;
	if (g.open_index < 0 || g.blinks_left > 0 || !was_down) {
		return;
	}
	int sel = item_under_pointer();
	if (sel >= 0) {
		choose(sel);
		return;
	}
	enum side side;
	int index;
	if (title_at(g.px, g.py, &side, &index) && side == g.open_side &&
			index == g.open_index) {
		return; /* released on its title: the menu stays open (sticky) */
	}
	close_menu();
}

/* ---- input --------------------------------------------------------------- */

static void set_cursor(uint32_t serial) {
	if (!g.cursor_theme) {
		return;
	}
	struct wl_cursor *cursor = wl_cursor_theme_get_cursor(g.cursor_theme, "left_ptr");
	if (!cursor) {
		cursor = wl_cursor_theme_get_cursor(g.cursor_theme, "default");
	}
	if (!cursor) {
		return;
	}
	struct wl_cursor_image *img = cursor->images[0];
	wl_surface_set_buffer_scale(g.cursor_surface, g.scale);
	wl_surface_attach(g.cursor_surface, wl_cursor_image_get_buffer(img), 0, 0);
	wl_surface_damage_buffer(g.cursor_surface, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_commit(g.cursor_surface);
	wl_pointer_set_cursor(g.pointer, serial, g.cursor_surface,
		img->hotspot_x / g.scale, img->hotspot_y / g.scale);
}

static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial,
		struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y) {
	g.px = wl_fixed_to_double(x);
	g.py = wl_fixed_to_double(y);
	set_cursor(serial);
	pointer_moved();
}

static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial,
		struct wl_surface *surface) {
}

static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time,
		wl_fixed_t x, wl_fixed_t y) {
	/* Both of our surfaces sit at the screen origin, so surface-local
	 * coordinates are screen coordinates (even during implicit grabs). */
	g.px = wl_fixed_to_double(x);
	g.py = wl_fixed_to_double(y);
	pointer_moved();
}

static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial,
		uint32_t time, uint32_t button, uint32_t state) {
	if (button != BTN_LEFT) {
		return;
	}
	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		pointer_pressed();
	} else {
		pointer_released();
	}
}

static void pointer_axis(void *data, struct wl_pointer *p, uint32_t time,
		uint32_t axis, wl_fixed_t value) {
}

static void pointer_frame(void *data, struct wl_pointer *p) {
}

static void pointer_axis_source(void *data, struct wl_pointer *p, uint32_t source) {
}

static void pointer_axis_stop(void *data, struct wl_pointer *p, uint32_t time,
		uint32_t axis) {
}

static void pointer_axis_discrete(void *data, struct wl_pointer *p, uint32_t axis,
		int32_t discrete) {
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
	.frame = pointer_frame,
	.axis_source = pointer_axis_source,
	.axis_stop = pointer_axis_stop,
	.axis_discrete = pointer_axis_discrete,
};

static void kb_keymap(void *data, struct wl_keyboard *k, uint32_t format, int fd,
		uint32_t size) {
	close(fd);
}

static void kb_enter(void *data, struct wl_keyboard *k, uint32_t serial,
		struct wl_surface *surface, struct wl_array *keys) {
}

static void kb_leave(void *data, struct wl_keyboard *k, uint32_t serial,
		struct wl_surface *surface) {
}

static void kb_key(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t time,
		uint32_t key, uint32_t state) {
	if (state == WL_KEYBOARD_KEY_STATE_PRESSED && key == KEY_ESC) {
		close_menu();
	}
}

static void kb_modifiers(void *data, struct wl_keyboard *k, uint32_t serial,
		uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
}

static void kb_repeat_info(void *data, struct wl_keyboard *k, int32_t rate, int32_t delay) {
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = kb_keymap,
	.enter = kb_enter,
	.leave = kb_leave,
	.key = kb_key,
	.modifiers = kb_modifiers,
	.repeat_info = kb_repeat_info,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !g.pointer) {
		g.pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(g.pointer, &pointer_listener, NULL);
	}
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !g.keyboard) {
		g.keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(g.keyboard, &keyboard_listener, NULL);
	}
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_capabilities,
	.name = seat_name,
};

/* ---- output and bar ------------------------------------------------------ */

static void output_geometry(void *data, struct wl_output *o, int32_t x, int32_t y,
		int32_t pw, int32_t ph, int32_t subpixel, const char *make,
		const char *model, int32_t transform) {
}

static void output_mode(void *data, struct wl_output *o, uint32_t flags,
		int32_t w, int32_t h, int32_t refresh) {
}

static void output_done(void *data, struct wl_output *o) {
	draw_bar();
}

static void output_scale(void *data, struct wl_output *o, int32_t factor) {
	g.scale = factor > 0 ? factor : 1;
}

static const struct wl_output_listener output_listener = {
	.geometry = output_geometry,
	.mode = output_mode,
	.done = output_done,
	.scale = output_scale,
};

static void bar_configure(void *data, struct zwlr_layer_surface_v1 *layer,
		uint32_t serial, uint32_t w, uint32_t h) {
	zwlr_layer_surface_v1_ack_configure(layer, serial);
	bool resized = (int)w != g.width;
	g.width = (int)w;
	g.bar_configured = true;
	if (resized || !g.model.n_left) {
		rebuild();
	} else {
		draw_bar();
	}
}

static void bar_closed(void *data, struct zwlr_layer_surface_v1 *layer) {
	g.running = false;
}

static const struct zwlr_layer_surface_v1_listener bar_listener = {
	.configure = bar_configure,
	.closed = bar_closed,
};

/* ---- globals ------------------------------------------------------------- */

static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
		const char *iface, uint32_t version) {
	if (strcmp(iface, wl_compositor_interface.name) == 0) {
		g.compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
	} else if (strcmp(iface, wl_subcompositor_interface.name) == 0) {
		g.subcompositor = wl_registry_bind(reg, name, &wl_subcompositor_interface, 1);
	} else if (strcmp(iface, wl_shm_interface.name) == 0) {
		g.shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
	} else if (strcmp(iface, wl_seat_interface.name) == 0 && !g.seat) {
		g.seat = wl_registry_bind(reg, name, &wl_seat_interface, 5);
		wl_seat_add_listener(g.seat, &seat_listener, NULL);
	} else if (strcmp(iface, wl_output_interface.name) == 0 && !g.output) {
		g.output = wl_registry_bind(reg, name, &wl_output_interface, 2);
		wl_output_add_listener(g.output, &output_listener, NULL);
	} else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
		g.layer_shell = wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, 4);
	} else if (strcmp(iface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
		g.toplevel_mgr = wl_registry_bind(reg, name,
			&zwlr_foreign_toplevel_manager_v1_interface, 3);
	} else if (strcmp(iface, wp_viewporter_interface.name) == 0) {
		g.viewporter = wl_registry_bind(reg, name, &wp_viewporter_interface, 1);
	}
}

static void registry_remove(void *data, struct wl_registry *reg, uint32_t name) {
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_remove,
};

/* Fire at the start of every minute so the clock flips on time. */
static void arm_clock(void) {
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	struct itimerspec its = {
		.it_value = { .tv_sec = (now.tv_sec / 60 + 1) * 60 },
		.it_interval = { .tv_sec = 60 },
	};
	timerfd_settime(g.clock_fd, TFD_TIMER_ABSTIME, &its, NULL);
}

int main(void) {
	g.display = wl_display_connect(NULL);
	if (!g.display) {
		fprintf(stderr, "platinum-menubar: cannot connect to Wayland\n");
		return 1;
	}
	struct wl_registry *reg = wl_display_get_registry(g.display);
	wl_registry_add_listener(reg, &registry_listener, NULL);
	wl_display_roundtrip(g.display);
	if (!g.compositor || !g.subcompositor || !g.shm || !g.layer_shell ||
			!g.viewporter || !g.toplevel_mgr || !g.seat) {
		fprintf(stderr, "platinum-menubar: compositor lacks required protocols "
			"(needs platinum-wm)\n");
		return 1;
	}
	wl_display_roundtrip(g.display); /* output scale, seat caps */

	toplevels_init(g.toplevel_mgr);
	g.cursor_theme = wl_cursor_theme_load(NULL, 24 * g.scale, g.shm);
	g.cursor_surface = wl_compositor_create_surface(g.compositor);

	g.bar_surface = wl_compositor_create_surface(g.compositor);
	g.bar_layer = zwlr_layer_shell_v1_get_layer_surface(g.layer_shell, g.bar_surface,
		g.output, ZWLR_LAYER_SHELL_V1_LAYER_TOP, "platinum-menubar");
	zwlr_layer_surface_v1_set_anchor(g.bar_layer,
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_size(g.bar_layer, 0, MBAR_HEIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(g.bar_layer, MBAR_HEIGHT);
	zwlr_layer_surface_v1_add_listener(g.bar_layer, &bar_listener, NULL);
	wl_surface_commit(g.bar_surface);

	g.clock_fd = timerfd_create(CLOCK_REALTIME, TFD_CLOEXEC | TFD_NONBLOCK);
	g.blink_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	arm_clock();

	g.running = true;
	while (g.running) {
		while (wl_display_prepare_read(g.display) != 0) {
			wl_display_dispatch_pending(g.display);
		}
		wl_display_flush(g.display);
		struct pollfd fds[] = {
			{ .fd = wl_display_get_fd(g.display), .events = POLLIN },
			{ .fd = g.clock_fd, .events = POLLIN },
			{ .fd = g.blink_fd, .events = POLLIN },
		};
		if (poll(fds, 3, -1) < 0 && errno != EINTR) {
			wl_display_cancel_read(g.display);
			break;
		}
		if (fds[0].revents & POLLIN) {
			if (wl_display_read_events(g.display) < 0) {
				break;
			}
		} else {
			wl_display_cancel_read(g.display);
		}
		if (fds[0].revents & (POLLERR | POLLHUP)) {
			break;
		}
		wl_display_dispatch_pending(g.display);

		uint64_t expirations;
		if ((fds[1].revents & POLLIN) &&
				read(g.clock_fd, &expirations, sizeof(expirations)) > 0) {
			menubar_apps_changed(); /* rebuilds the clock title */
		}
		if ((fds[2].revents & POLLIN) &&
				read(g.blink_fd, &expirations, sizeof(expirations)) > 0) {
			blink_tick();
		}
	}
	return 0;
}
