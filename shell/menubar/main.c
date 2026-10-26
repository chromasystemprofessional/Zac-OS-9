/*
 * zacos9-menubar: the Mac OS 8/9 menu bar for zacos9-wm.
 *
 * The bar is a wlr-layer-shell surface at the top of the screen with a
 * 20 px exclusive zone: on the main display (zacos9-wm keeps it there), or
 * with "menubar-every-display=1" (the Monitors panel's Menu Bar on Every
 * Display) one on each screen, each at that screen's scale. The bar last
 * pointed at is the active one: its menus open on its screen. While a menu
 * is open, a transparent full-screen overlay surface takes the pointer and
 * keyboard (classic menus are modal); the menu itself is a small subsurface
 * of it, so redraws stay cheap.
 *
 * At 1.25, 1.5 and 1.75 (wp_fractional_scale_v1) everything is drawn at
 * the screen's own pixels, each 1x pixel one or two of them (pl_snap), and
 * shown through a viewport at its logical size: sharp, where 2x shrunk by
 * zacos9-wm would be soft.
 *
 * Tracking follows Mac OS 8: press a title and drag to an item; or click a
 * title and the menu stays open ("sticky") until the next click. An item
 * with a submenu opens it beside the menu, in a second subsurface.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#include "fractional-scale-v1-client-protocol.h"
#include "icons.h"
#include "menubar.h"
#include "platinum-shell-v1-client-protocol.h"
#include "settings.h"
#include "viewporter-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "wlclient.h"

/* Chosen items blink before the menu closes (General Controls default).
 * TODO: verify count and timing on a real Mac. */
#define BLINK_MS 60

/* SIDE_CONTEXT: a contextual menu at the pointer, not from a title. */
enum side { SIDE_LEFT, SIDE_RIGHT, SIDE_CONTEXT };

/* A screen, as zacos9-wm announces it. */
struct output {
	struct wl_list link;
	uint32_t global;
	struct wl_output *wl;
	int scale; /* wl_output.scale, if the compositor can't tell a surface its own */
};

/* A menu bar: the one on the main display (no output named), or one on
 * each screen. */
struct bar {
	struct wl_list link;
	struct output *output; /* NULL: wherever zacos9-wm puts it (the main display) */
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer;
	int width; /* logical screen width */
	int scale;
	int quarters; /* a fractional scale (5, 6, 7: 1.25, 1.5, 1.75), else 0 */
	struct wp_viewport *viewport;
	struct wp_fractional_scale_v1 *fractional;
	bool configured;
};

static struct {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_subcompositor *subcompositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct zwlr_foreign_toplevel_manager_v1 *toplevel_mgr;
	struct wp_viewporter *viewporter;
	struct wp_fractional_scale_manager_v1 *fractional;
	struct zwp_virtual_keyboard_manager_v1 *vkbd_mgr;
	struct platinum_shell_v1 *platinum_shell;
	int compositor_version;

	struct wl_list outputs; /* struct output */
	struct wl_list bars;    /* struct bar */
	bool every_display;     /* a bar on each screen */
	/* The active bar, and what of it menus use: its screen's width and
	 * scale, and the screen to open them on (NULL: the main display). */
	struct bar *active;
	int width;
	int scale;
	int quarters;
	struct wl_output *output;

	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;
	struct wl_cursor_theme *cursor_themes[4]; /* by scale, loaded when needed */
	int cursor_px;
	struct wl_surface *cursor_surface;
	double px, py; /* pointer, in screen (= surface) coordinates */

	/* The overlay that exists while a menu is open. */
	struct wl_surface *overlay;
	struct zwlr_layer_surface_v1 *overlay_layer;
	struct wp_viewport *overlay_viewport;
	struct wp_viewport *menu_viewport, *sub_viewport, *hint_viewport;
	struct wl_surface *menu_surface;
	struct wl_subsurface *menu_sub;
	bool overlay_configured;
	int menu_x, menu_y, menu_w, menu_h;
	int height; /* logical screen height, from the overlay */
	struct wl_surface *sub_surface;
	struct wl_subsurface *sub_sub;
	int sub_x, sub_y, sub_w, sub_h; /* screen position of the border */
	struct wl_surface *hint_surface; /* Hint Bubbles (hints.c) */
	struct wl_subsurface *hint_sub;
	bool hints; /* Show Hint Bubbles, read when a menu opens */

	struct mb_bar model;
	bool rebuild_pending;

	/* A contextual menu: where the pointer was when it opened (known once
	 * the overlay has the pointer), and its items. */
	struct mb_menu context;
	bool context_placed;
	double context_x, context_y;

	/* Tracking. */
	enum side open_side;
	int open_index; /* -1 when closed */
	int selected;   /* item index, or -1 */
	int sub_parent; /* item whose submenu is open, or -1 */
	int sub_selected;
	bool button_down;
	int blinks_left;
	bool blink_on;
	bool chosen_in_sub;
	struct mb_item chosen; /* copy of the item being performed */
	/* A press on the Application menu's handle, and where. */
	bool handle_down;
	double handle_x;
	/* Option, from the keyboard we have while a menu is open. */
	uint32_t option_mask, mods;

	int clock_fd, blink_fd;
	bool running;
} g = {
	.scale = 1, .open_index = -1, .selected = -1, .sub_parent = -1, .sub_selected = -1,
	.clock_fd = -1, .blink_fd = -1,
};

struct wl_seat *menubar_seat(void) {
	return g.seat;
}

/* ---- shm buffers --------------------------------------------------------- */

/* Each buffer is drawn once: freed when the compositor is done with it. */
static void buffer_done(struct pl_shm_buffer *b, void *data) {
	pl_shm_buffer_free(b);
	free(b);
}

/* Upscale a logical 1x ARGB canvas into a new shm buffer at a scale
 * (nearest neighbour: pixel art stays crisp): s times, or at a fractional
 * scale (quarters) to the screen's pixels for an image whose top left is
 * (x, y) on the screen, its text in that scale's font. */
static struct wl_buffer *make_buffer(const struct pl_canvas *c, int s, int quarters, int x, int y) {
	const uint32_t *src = c->px;
	const int w = c->width, h = c->height;
	int bw = w * s, bh = h * s;
	if (quarters) {
		bw = pl_snap(x + w, quarters) - pl_snap(x, quarters);
		bh = pl_snap(y + h, quarters) - pl_snap(y, quarters);
	}
	struct pl_shm_buffer *b = calloc(1, sizeof(*b));
	if (!b || !pl_shm_buffer_make(b, g.shm, "zacos9-menubar", bw, bh, WL_SHM_FORMAT_ARGB8888)) {
		free(b);
		return NULL;
	}
	b->released = buffer_done;
	uint32_t *dst = b->px;
	if (quarters) {
		pl_snap_scale_canvas(c, x, y, quarters, dst);
	} else {
		for (int j = 0; j < bh; j++) {
			const uint32_t *row = src + (j / s) * w;
			for (int i = 0; i < bw; i++) {
				dst[j * bw + i] = row[i / s];
			}
		}
	}
	return b->wl;
}

/* A canvas's image at (x, y) on the screen, its top left there. Frees
 * its text log. */
static void present_at(struct wl_surface *surface, struct wp_viewport *viewport,
		struct pl_canvas *c, int x, int y, int scale, int quarters) {
	quarters = viewport ? quarters : 0;
	const int w = c->width, h = c->height;
	struct wl_buffer *buf = make_buffer(c, scale, quarters, x, y);
	pl_text_log_free(c->log);
	c->log = NULL;
	if (!buf) {
		return;
	}
	wl_surface_set_buffer_scale(surface, quarters ? 1 : scale);
	if (viewport) {
		wp_viewport_set_destination(viewport, quarters ? w : -1, quarters ? h : -1);
	}
	wl_surface_attach(surface, buf, 0, 0);
	wl_surface_damage_buffer(surface, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_commit(surface);
}

/* Menus and the rest, at the active bar's scale. */
static void present(struct wl_surface *surface, struct wp_viewport *viewport, struct pl_canvas *c,
		int x, int y) {
	present_at(surface, viewport, c, x, y, g.scale, g.quarters);
}

/* A canvas over w x h pixels, logging its text at a fractional scale. */
static struct pl_canvas canvas_for(uint32_t *px, int w, int h, int quarters) {
	return (struct pl_canvas){ .px = px, .stride = w, .width = w, .height = h,
		.log = quarters ? pl_text_log_new(px, w) : NULL };
}

/* ---- drawing ------------------------------------------------------------- */

static struct mb_menu *open_menu_ptr(void) {
	if (g.open_index < 0) {
		return NULL;
	}
	if (g.open_side == SIDE_CONTEXT) {
		return &g.context;
	}
	return g.open_side == SIDE_LEFT ? &g.model.left[g.open_index]
		: &g.model.right[g.open_index];
}

static struct mbar_title *open_title_ptr(void) {
	if (g.open_index < 0 || g.open_side == SIDE_CONTEXT) {
		return NULL;
	}
	return g.open_side == SIDE_LEFT ? &g.model.left_titles[g.open_index]
		: &g.model.right_titles[g.open_index];
}

/* One bar with a model laid out for its width; an open menu's title
 * highlighted on the active bar only. */
static void paint_bar(struct bar *b, const struct mb_bar *model) {
	if (!b->configured || b->width <= 0) {
		return;
	}
	uint32_t *px = calloc((size_t)b->width * MBAR_HEIGHT, sizeof(*px));
	if (!px) {
		return;
	}
	struct pl_canvas c = canvas_for(px, b->width, MBAR_HEIGHT, b->quarters);
	mbar_paint_background(&c, b->width);
	const bool here = b == g.active;
	int hl = here && g.open_side == SIDE_LEFT ? g.open_index : -1;
	int hr = here && g.open_side == SIDE_RIGHT ? g.open_index : -1;
	mbar_paint_titles(&c, model->left_titles, model->n_left, hl, pl_accent_current());
	mbar_paint_titles(&c, model->right_titles, model->n_right, hr, pl_accent_current());
	present_at(b->surface, b->viewport, &c, 0, 0, b->scale, b->quarters);
	free(px);
}

/* The active bar (where menus open and highlight). */
static void draw_bar(void) {
	if (g.active) {
		paint_bar(g.active, &g.model);
	}
}

/* The Zac menu's icons: in a menu with any, every name moves over for one. */
#define MENU_ICON_INDENT (MENU_ICON + 4)

static bool menu_has_icons(const struct mb_menu *m) {
	for (int i = 0; i < m->n; i++) {
		if (m->items[i].icon_px || m->items[i].icon_kind) {
			return true;
		}
	}
	return false;
}

static void to_menu_items(const struct mb_menu *m, struct menu_item *out) {
	const int indent = menu_has_icons(m) ? MENU_ICON_INDENT : 0;
	for (int i = 0; i < m->n; i++) {
		out[i] = (struct menu_item){
			.label = m->items[i].label_text,
			.key = m->items[i].key_text,
			.shift = m->items[i].shift,
			.indent = m->items[i].label ? indent : 0,
			.enabled = m->items[i].enabled,
			.checked = m->items[i].checked,
			.submenu = m->items[i].submenu != NULL,
			.swatch = m->items[i].swatch,
		};
	}
}

/* Each item's icon, before its name; a dimmed item's dimmed too, half
 * way to the menu's own colour, as Mac OS 9 drew them. */
static void paint_icons(struct pl_canvas *c, const struct mb_menu *m, const struct menu_item *items) {
	for (int i = 0; i < m->n; i++) {
		const struct mb_item *it = &m->items[i];
		const int y = menu_item_top(items, m->n, i) + (MENU_ITEM_H - MENU_ICON) / 2;
		const uint32_t back = c->px[(size_t)(y + MENU_ICON / 2) * c->stride + MENU_TEXT_X - 2];
		if (it->icon_px) {
			pl_image_blend(c, MENU_TEXT_X, y, it->icon_px, MENU_ICON, MENU_ICON, false);
		} else if (it->icon_kind) {
			pl_icon_paint(c, MENU_TEXT_X, y, (enum pl_icon_kind)(it->icon_kind - 1), PL_ICON_SMALL, false);
		} else {
			continue;
		}
		if (!it->enabled) {
			for (int yy = y; yy < y + MENU_ICON && yy < c->height; yy++) {
				for (int xx = MENU_TEXT_X; xx < MENU_TEXT_X + MENU_ICON && xx < c->width; xx++) {
					uint32_t *p = &c->px[(size_t)yy * c->stride + xx];
					uint32_t mixed = 0xFF000000u;
					for (int shift = 0; shift < 24; shift += 8) {
						mixed |= ((((*p >> shift) & 0xFF) + ((back >> shift) & 0xFF)) / 2) << shift;
					}
					*p = mixed;
				}
			}
		}
	}
}

static struct mb_menu *sub_menu_ptr(void) {
	struct mb_menu *m = open_menu_ptr();
	return m && g.sub_parent >= 0 ? m->items[g.sub_parent].submenu : NULL;
}

/* The submenu's first item lines up with its parent item, and the menu
 * overlaps the parent's right border. TODO: the HIG shows no open
 * submenu; the overlap is an estimate. */
static void draw_submenu(struct menu_item *parent_items, int parent_n) {
	struct mb_menu *s = sub_menu_ptr();
	if (!s) {
		wl_surface_attach(g.sub_surface, NULL, 0, 0);
		wl_surface_commit(g.sub_surface);
		return;
	}
	struct menu_item items[MAX_ITEMS];
	to_menu_items(s, items);
	menu_measure(items, s->n, &g.sub_w, &g.sub_h);
	g.sub_x = g.menu_x + g.menu_w - 1;
	if (g.sub_x + g.sub_w + MENU_SHADOW > g.width) {
		g.sub_x = g.menu_x - g.sub_w + 1; /* no room: open to the left */
	}
	g.sub_y = g.menu_y + menu_item_top(parent_items, parent_n, g.sub_parent) - 1;
	if (g.height > 0 && g.sub_y + g.sub_h + MENU_SHADOW > g.height) {
		g.sub_y = g.height - g.sub_h - MENU_SHADOW;
	}
	if (g.sub_y < MBAR_HEIGHT - 1) {
		g.sub_y = MBAR_HEIGHT - 1;
	}

	int w = g.sub_w + MENU_SHADOW, h = g.sub_h + MENU_SHADOW;
	uint32_t *px = calloc((size_t)w * h, sizeof(*px));
	struct pl_canvas c = canvas_for(px, w, h, g.quarters);
	int sel = g.blinks_left > 0 && g.chosen_in_sub && !g.blink_on ? -1 : g.sub_selected;
	menu_paint(&c, items, s->n, g.sub_w, g.sub_h, sel, pl_accent_current());
	paint_icons(&c, s, items);
	wl_subsurface_set_position(g.sub_sub, g.sub_x, g.sub_y);
	present(g.sub_surface, g.sub_viewport, &c, g.sub_x, g.sub_y);
	free(px);
}

/* The selected item's Hint Bubble, beside its menu with the tail pointing
 * into the item; on the menu's left if there's no room on its right. */
static void draw_hint(struct menu_item *parent_items, int parent_n) {
	struct mb_menu *m = open_menu_ptr(), *s = sub_menu_ptr();
	const struct mb_item *it = NULL;
	int mx = g.menu_x, mw = g.menu_w, iy = 0;
	if (g.hints && g.blinks_left == 0 && s && g.sub_selected >= 0) {
		struct menu_item items[MAX_ITEMS];
		to_menu_items(s, items);
		it = &s->items[g.sub_selected];
		mx = g.sub_x, mw = g.sub_w;
		iy = g.sub_y + menu_item_top(items, s->n, g.sub_selected);
	} else if (g.hints && g.blinks_left == 0 && g.selected >= 0 && g.sub_parent < 0) {
		it = &m->items[g.selected];
		iy = g.menu_y + menu_item_top(parent_items, parent_n, g.selected);
	}
	char buf[256];
	const char *text = it ? hint_text(it, buf, sizeof(buf)) : NULL;
	int w, h;
	uint32_t *px = NULL;
	struct pl_text_log *log = NULL;
	struct pl_text_log **logged = g.quarters ? &log : NULL;
	bool left = false;
	if (text) {
		px = hint_paint(text, false, &w, &h, logged);
		if (px && mx + mw - 8 + w > g.width) {
			free(px);
			pl_text_log_free(log);
			px = hint_paint(text, true, &w, &h, logged);
			left = true;
		}
	}
	if (!px) {
		wl_surface_attach(g.hint_surface, NULL, 0, 0);
		wl_surface_commit(g.hint_surface);
		return;
	}
	int x = left ? mx + 8 - w : mx + mw - 8;
	int y = iy + MENU_ITEM_H / 2 - HINT_TIP_Y;
	if (g.height > 0 && y + h > g.height) {
		y = g.height - h;
	}
	if (y < MBAR_HEIGHT) {
		y = MBAR_HEIGHT;
	}
	x = x < 0 ? 0 : x;
	wl_subsurface_set_position(g.hint_sub, x, y);
	struct pl_canvas c = { .px = px, .stride = w, .width = w, .height = h, .log = log };
	present(g.hint_surface, g.hint_viewport, &c, x, y);
	free(px);
}

static void draw_menu(void) {
	struct mb_menu *m = open_menu_ptr();
	if (!m || !g.overlay_configured) {
		return;
	}
	struct menu_item items[MAX_ITEMS];
	to_menu_items(m, items);
	menu_measure(items, m->n, &g.menu_w, &g.menu_h);

	if (g.open_side == SIDE_CONTEXT) {
		/* At the pointer, kept on screen: drawn once the overlay has
		 * told us where the pointer is. */
		if (!g.context_placed) {
			return;
		}
		g.menu_x = (int)g.context_x;
		g.menu_y = (int)g.context_y;
		if (g.height > 0 && g.menu_y + g.menu_h + MENU_SHADOW > g.height) {
			g.menu_y = g.height - g.menu_h - MENU_SHADOW;
		}
		if (g.menu_y < MBAR_HEIGHT - 1) {
			g.menu_y = MBAR_HEIGHT - 1;
		}
	} else {
		/* The menu hangs from its title, kept on screen at the right edge. */
		g.menu_x = open_title_ptr()->hi_l;
		g.menu_y = MBAR_HEIGHT - 1;
	}
	if (g.menu_x + g.menu_w + MENU_SHADOW > g.width) {
		g.menu_x = g.width - g.menu_w - MENU_SHADOW;
	}
	if (g.menu_x < 0) {
		g.menu_x = 0;
	}

	int w = g.menu_w + MENU_SHADOW, h = g.menu_h + MENU_SHADOW;
	uint32_t *px = calloc((size_t)w * h, sizeof(*px));
	struct pl_canvas c = canvas_for(px, w, h, g.quarters);
	int sel = g.blinks_left > 0 && !g.chosen_in_sub && !g.blink_on ? -1 : g.selected;
	menu_paint(&c, items, m->n, g.menu_w, g.menu_h, sel, pl_accent_current());
	paint_icons(&c, m, items);

	wl_subsurface_set_position(g.menu_sub, g.menu_x, g.menu_y);
	present(g.menu_surface, g.menu_viewport, &c, g.menu_x, g.menu_y);
	free(px);
	draw_submenu(items, m->n);
	draw_hint(items, m->n);
	wl_surface_commit(g.overlay); /* applies the subsurface position */
}

/* ---- the overlay --------------------------------------------------------- */

static void overlay_configure(void *data, struct zwlr_layer_surface_v1 *layer,
		uint32_t serial, uint32_t w, uint32_t h) {
	zwlr_layer_surface_v1_ack_configure(layer, serial);
	/* A single transparent pixel, stretched over the screen, catches input. */
	static uint32_t clear = 0;
	wp_viewport_set_destination(g.overlay_viewport, (int)w, (int)h);
	g.height = (int)h;
	const struct pl_canvas one = { .px = &clear, .stride = 1, .width = 1, .height = 1 };
	struct wl_buffer *buf = make_buffer(&one, 1, 0, 0, 0);
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
		g.output, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "zacos9-menu");
	zwlr_layer_surface_v1_set_anchor(g.overlay_layer,
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(g.overlay_layer, -1);
	zwlr_layer_surface_v1_set_keyboard_interactivity(g.overlay_layer,
		ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
	zwlr_layer_surface_v1_add_listener(g.overlay_layer, &overlay_listener, NULL);

	g.menu_surface = wl_compositor_create_surface(g.compositor);
	g.menu_viewport = wp_viewporter_get_viewport(g.viewporter, g.menu_surface);
	g.menu_sub = wl_subcompositor_get_subsurface(g.subcompositor, g.menu_surface, g.overlay);
	wl_subsurface_set_desync(g.menu_sub);
	/* Pointer events all go to the overlay, in screen coordinates. */
	struct wl_region *none = wl_compositor_create_region(g.compositor);
	wl_surface_set_input_region(g.menu_surface, none);
	g.sub_surface = wl_compositor_create_surface(g.compositor);
	g.sub_viewport = wp_viewporter_get_viewport(g.viewporter, g.sub_surface);
	g.sub_sub = wl_subcompositor_get_subsurface(g.subcompositor, g.sub_surface, g.overlay);
	wl_subsurface_set_desync(g.sub_sub);
	wl_surface_set_input_region(g.sub_surface, none);
	g.hint_surface = wl_compositor_create_surface(g.compositor);
	g.hint_viewport = wp_viewporter_get_viewport(g.viewporter, g.hint_surface);
	g.hint_sub = wl_subcompositor_get_subsurface(g.subcompositor, g.hint_surface, g.overlay);
	wl_subsurface_set_desync(g.hint_sub);
	wl_surface_set_input_region(g.hint_surface, none);
	wl_region_destroy(none);

	g.overlay_configured = false;
	wl_surface_commit(g.overlay);
}

static void destroy_overlay(void) {
	if (!g.overlay) {
		return;
	}
	wl_subsurface_destroy(g.hint_sub);
	wp_viewport_destroy(g.hint_viewport);
	wl_surface_destroy(g.hint_surface);
	wl_subsurface_destroy(g.sub_sub);
	wp_viewport_destroy(g.sub_viewport);
	wl_surface_destroy(g.sub_surface);
	wl_subsurface_destroy(g.menu_sub);
	wp_viewport_destroy(g.menu_viewport);
	wl_surface_destroy(g.menu_surface);
	zwlr_layer_surface_v1_destroy(g.overlay_layer);
	wp_viewport_destroy(g.overlay_viewport);
	wl_surface_destroy(g.overlay);
	g.overlay = NULL;
	g.overlay_configured = false;
}

/* ---- tracking ------------------------------------------------------------ */

static bool any_configured(void) {
	struct bar *b;
	wl_list_for_each(b, &g.bars, link) {
		if (b->configured) {
			return true;
		}
	}
	return false;
}

/* Every bar again: the active one's model is kept (menus open from it);
 * one on a screen of another width gets a model laid out for that. */
static void rebuild(void) {
	menus_rebuild(&g.model, g.width);
	g.rebuild_pending = false;
	struct bar *b;
	wl_list_for_each(b, &g.bars, link) {
		if (b == g.active || !b->configured) {
			continue;
		}
		if (b->width == g.width) {
			paint_bar(b, &g.model);
		} else {
			struct mb_bar other = { 0 };
			menus_rebuild(&other, b->width);
			paint_bar(b, &other);
			menus_free(&other);
		}
	}
	draw_bar();
}

/* The bar the pointer came to becomes the active one. */
static void activate(struct bar *b) {
	if (!b || g.open_index >= 0) {
		return;
	}
	const bool relayout = !g.active || b->width != g.width;
	g.active = b;
	g.width = b->width;
	g.scale = b->scale;
	g.quarters = b->quarters;
	g.output = b->output ? b->output->wl : NULL;
	if (relayout && b->configured) {
		menus_rebuild(&g.model, g.width);
	}
}

void menubar_apps_changed(void) {
	/* Never reshuffle menus under the user's pointer. */
	if (g.open_index >= 0) {
		g.rebuild_pending = true;
	} else if (any_configured()) {
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
	pl_sound_event("menu-open");
	g.open_side = side;
	g.open_index = index;
	g.selected = -1;
	g.sub_parent = g.sub_selected = -1;
	char hints[8];
	g.hints = !pl_setting("hint-bubbles", hints, sizeof(hints)) ||
		(strcmp(hints, "0") != 0 && strcmp(hints, "false") != 0);
	create_overlay();
	draw_bar();
	draw_menu();
}

static void close_menu(void) {
	if (g.open_index < 0) {
		return;
	}
	if (g.open_side == SIDE_CONTEXT) {
		menus_free_menu(&g.context);
		g.context_placed = false;
	}
	g.open_index = -1;
	g.selected = -1;
	g.sub_parent = g.sub_selected = -1;
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
	int lx = (int)g.px - g.menu_x, ly = (int)g.py - g.menu_y;
	if (lx < 1 || lx > g.menu_w - 2) {
		return -1;
	}
	struct menu_item items[MAX_ITEMS];
	to_menu_items(m, items);
	int i = menu_item_at(items, m->n, g.menu_h, ly);
	return i >= 0 && m->items[i].enabled ? i : -1;
}

static bool in_submenu(void) {
	return g.sub_parent >= 0 && g.px >= g.sub_x && g.px < g.sub_x + g.sub_w &&
		g.py >= g.sub_y && g.py < g.sub_y + g.sub_h;
}

static int sub_item_under_pointer(void) {
	struct mb_menu *s = sub_menu_ptr();
	if (!s || !in_submenu()) {
		return -1;
	}
	int lx = (int)g.px - g.sub_x, ly = (int)g.py - g.sub_y;
	if (lx < 1 || lx > g.sub_w - 2) {
		return -1;
	}
	struct menu_item items[MAX_ITEMS];
	to_menu_items(s, items);
	int i = menu_item_at(items, s->n, g.sub_h, ly);
	return i >= 0 && s->items[i].enabled ? i : -1;
}

static void arm_timer(int fd, long ms) {
	struct itimerspec its = {
		.it_value = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L },
	};
	timerfd_settime(fd, 0, &its, NULL);
}

static void blink_tick(void);

static void choose(int index, bool in_sub) {
	struct mb_item *it = in_sub ? &sub_menu_ptr()->items[index]
		: &open_menu_ptr()->items[index];
	g.chosen = (struct mb_item){
		.action = it->action,
		.key = it->key,
		.arg = it->arg ? strdup(it->arg) : NULL,
		.option = g.option_mask && (g.mods & g.option_mask),
	};
	g.chosen_in_sub = in_sub;
	if (in_sub) {
		g.sub_selected = index;
	} else {
		g.selected = index;
	}
	/* General Controls' Menu Blinking: Off acts at once. */
	g.blinks_left = pl_menu_blinks() * 2;
	g.blink_on = false;
	if (g.blinks_left == 0) {
		g.blinks_left = 1;
		blink_tick();
		return;
	}
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
	pl_sound_event("menu-command");
	menus_perform(&g.chosen, g.chosen.option);
	free(g.chosen.arg);
	g.chosen.arg = NULL;
}

static void pointer_moved(void) {
	if (g.open_index < 0 || g.blinks_left > 0) {
		return;
	}
	if (g.open_side == SIDE_CONTEXT && !g.context_placed) {
		/* The overlay's first pointer event: the menu opens here. */
		g.context_x = g.px;
		g.context_y = g.py;
		g.context_placed = true;
		draw_menu();
		return;
	}
	enum side side;
	int index;
	if (g.open_side != SIDE_CONTEXT && title_at(g.px, g.py, &side, &index)) {
		if (side != g.open_side || index != g.open_index) {
			open_menu(side, index);
		} else if (g.selected != -1) {
			g.selected = -1;
			g.sub_parent = g.sub_selected = -1;
			draw_menu();
		}
		return;
	}
	if (in_submenu()) {
		int sel = sub_item_under_pointer();
		if (sel != g.sub_selected) {
			g.sub_selected = sel;
			draw_menu();
		}
		return;
	}
	int sel = item_under_pointer();
	if (sel < 0 && g.sub_parent >= 0) {
		/* Off both menus: the submenu stays up, so a diagonal path from
		 * its parent item does not lose it. */
		if (g.sub_selected != -1) {
			g.sub_selected = -1;
			draw_menu();
		}
		return;
	}
	if (sel != g.selected) {
		g.selected = sel;
		g.sub_parent = sel >= 0 && open_menu_ptr()->items[sel].submenu ? sel : -1;
		g.sub_selected = -1;
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
	if (g.open_index < 0 && !on_title && g.py < MBAR_HEIGHT &&
			mbar_handle_at(g.model.right_titles, g.model.n_right, (int)g.px) >= 0) {
		/* The Application menu's handle: its name, or just an icon,
		 * once the button comes up (pointer_released). */
		g.handle_down = true;
		g.handle_x = g.px;
		return;
	}
	if (g.open_index < 0 && !on_title && g.py < MBAR_HEIGHT && g.model.has_clock &&
			mbar_title_at(g.model.right_titles, 1, (int)g.px) == 0) {
		/* The clock: a click flips between time and date. */
		menus_toggle_clock();
		rebuild();
		return;
	}
	if (g.open_index < 0) {
		if (on_title) {
			/* Fresh contents each time a menu opens: Favorites and the
			 * recent items change while the menus sit closed. */
			rebuild();
			on_title = title_at(g.px, g.py, &side, &index);
		}
		if (on_title && (side == SIDE_LEFT ? g.model.left[index].n : g.model.right[index].n)) {
			g.button_down = true;
			open_menu(side, index);
		}
		return;
	}
	/* A menu is open in sticky mode. */
	if (g.open_side == SIDE_CONTEXT) {
		if (item_under_pointer() >= 0 || sub_item_under_pointer() >= 0) {
			g.button_down = true;
		} else {
			close_menu();
		}
		return;
	}
	if (on_title) {
		if (side == g.open_side && index == g.open_index) {
			close_menu();
		} else {
			g.button_down = true;
			open_menu(side, index);
		}
	} else if (item_under_pointer() >= 0 || sub_item_under_pointer() >= 0) {
		g.button_down = true;
	} else {
		close_menu();
	}
}

/* Mac OS 9's tear-off: the Application menu dragged down past its bottom
 * and let go there becomes the Pack (the Application palette), where the
 * pointer is. */
static bool tear_off_pack(void) {
	if (g.open_side != SIDE_RIGHT || g.open_index != g.model.n_right - 1 ||
			g.py < g.menu_y + g.menu_h + 8) {
		return false;
	}
	close_menu();
	char command[PATH_MAX + 96];
	sibling_program("zacos9-pack", command, sizeof(command));
	char at[48];
	snprintf(at, sizeof(at), " --at %d,%d", (int)g.px - 10, (int)g.py - 6);
	strncat(command, at, sizeof(command) - strlen(command) - 1);
	launch(command);
	return true;
}

static void pointer_released(void) {
	if (g.handle_down) {
		/* Dragged left, the name shows; right, it hides; a click switches. */
		g.handle_down = false;
		const double dx = g.px - g.handle_x;
		if (dx <= -6 || dx >= 6) {
			menus_show_app_name(dx < 0);
		} else {
			menus_toggle_app_name();
		}
		rebuild();
		return;
	}
	bool was_down = g.button_down;
	g.button_down = false;
	if (g.open_index < 0 || g.blinks_left > 0 || !was_down) {
		return;
	}
	if (tear_off_pack()) {
		return;
	}
	int sub = sub_item_under_pointer();
	if (sub >= 0) {
		choose(sub, true);
		return;
	}
	if (in_submenu()) {
		return; /* a separator or disabled item: keep tracking */
	}
	int sel = item_under_pointer();
	if (sel >= 0 && sel == g.sub_parent) {
		return; /* released on a submenu's parent: it stays open */
	}
	if (sel >= 0) {
		choose(sel, false);
		return;
	}
	if (g.open_side == SIDE_CONTEXT) {
		/* Released where it opened (a click): it stays open, sticky. */
		if (g.context_placed && abs((int)(g.px - g.context_x)) < 4 && abs((int)(g.py - g.context_y)) < 4) {
			return;
		}
		close_menu();
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

/* The Sniffer's Control-click: its contextual menu, at the pointer. The
 * button is still down from that click, so it tracks press-drag-release;
 * a click without moving leaves it open. */
static void open_context(void);

void menubar_context_menu(const char *kind) {
	if (g.open_index >= 0 || !g.active || !g.active->configured) {
		return;
	}
	menus_context(&g.context, kind);
	open_context();
}

/* The Sniffer's folder-path menu (⌘-click on a window's title), also at
 * the pointer, with the button still down. */
void menubar_path_menu(const char *names) {
	if (g.open_index >= 0 || !g.active || !g.active->configured) {
		return;
	}
	menus_path(&g.context, names);
	open_context();
}

static void open_context(void) {
	if (g.context.n == 0) {
		menus_free_menu(&g.context);
		return;
	}
	pl_sound_event("menu-open");
	g.open_side = SIDE_CONTEXT;
	g.open_index = 0;
	g.selected = -1;
	g.sub_parent = g.sub_selected = -1;
	g.context_placed = false;
	g.button_down = true;
	create_overlay();
}

/* ---- input --------------------------------------------------------------- */

/* zacos9-wm sets XCURSOR_THEME/SIZE to its own cursor theme; each scale
 * a screen has, its own size of it. */
static struct wl_cursor_theme *cursor_theme(int scale) {
	scale = scale < 1 ? 1 : scale > 3 ? 3 : scale;
	if (!g.cursor_themes[scale]) {
		g.cursor_themes[scale] = wl_cursor_theme_load(getenv("XCURSOR_THEME"),
			g.cursor_px * scale, g.shm);
	}
	return g.cursor_themes[scale];
}

static struct bar *bar_of(struct wl_surface *surface) {
	struct bar *b;
	wl_list_for_each(b, &g.bars, link) {
		if (b->surface == surface) {
			return b;
		}
	}
	return NULL;
}

static void set_cursor(uint32_t serial) {
	struct wl_cursor_theme *theme = cursor_theme(g.scale);
	if (!theme) {
		return;
	}
	struct wl_cursor *cursor = wl_cursor_theme_get_cursor(theme, "left_ptr");
	if (!cursor) {
		cursor = wl_cursor_theme_get_cursor(theme, "default");
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
	activate(bar_of(surface));
	set_cursor(serial);
	pointer_moved();
}

static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial,
		struct wl_surface *surface) {
}

static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time,
		wl_fixed_t x, wl_fixed_t y) {
	/* Both of our surfaces sit at their screen's origin, so surface-local
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
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || size == 0) {
		close(fd);
		return;
	}
	void *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED) {
		return;
	}
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = context
		? xkb_keymap_new_from_buffer(context, map, strnlen(map, size),
			XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS)
		: NULL;
	munmap(map, size);
	g.option_mask = 0;
	if (keymap) {
		const xkb_mod_index_t alt = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_ALT);
		g.option_mask = alt < 32 ? 1u << alt : 0;
		xkb_keymap_unref(keymap);
	}
	xkb_context_unref(context);
}

static void kb_enter(void *data, struct wl_keyboard *k, uint32_t serial,
		struct wl_surface *surface, struct wl_array *keys) {
}

static void kb_leave(void *data, struct wl_keyboard *k, uint32_t serial,
		struct wl_surface *surface) {
	g.mods = 0; /* until the next enter's modifiers */
}

static void kb_key(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t time,
		uint32_t key, uint32_t state) {
	if (state == WL_KEYBOARD_KEY_STATE_PRESSED && key == KEY_ESC) {
		close_menu();
	}
}

static void kb_modifiers(void *data, struct wl_keyboard *k, uint32_t serial,
		uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
	g.mods = depressed | latched | locked;
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
}

static void output_scale(void *data, struct wl_output *o, int32_t factor) {
	struct output *out = data;
	out->scale = factor > 0 ? factor : 1;
}

static const struct wl_output_listener output_listener = {
	.geometry = output_geometry,
	.mode = output_mode,
	.done = output_done,
	.scale = output_scale,
};

static void bar_configure(void *data, struct zwlr_layer_surface_v1 *layer,
		uint32_t serial, uint32_t w, uint32_t h) {
	struct bar *b = data;
	zwlr_layer_surface_v1_ack_configure(layer, serial);
	const bool resized = (int)w != b->width;
	b->width = (int)w;
	b->configured = true;
	if (!g.active) {
		activate(b);
	}
	if (b == g.active) {
		g.width = b->width;
	}
	if (b != g.active || resized || !g.model.n_left) {
		/* (A menu open meanwhile keeps its layout until it closes.) */
		if (g.open_index >= 0) {
			g.rebuild_pending = true;
		} else {
			rebuild();
		}
	} else {
		draw_bar();
	}
}

static void destroy_bar(struct bar *b);

static void bar_closed(void *data, struct zwlr_layer_surface_v1 *layer) {
	struct bar *b = data;
	if (!b->output) {
		g.running = false; /* the only bar, and zacos9-wm took it away */
	} else {
		destroy_bar(b); /* its screen is gone */
	}
}

static const struct zwlr_layer_surface_v1_listener bar_listener = {
	.configure = bar_configure,
	.closed = bar_closed,
};

/* zacos9-wm tells each bar the scale of the screen it is on. */
static void bar_enter(void *data, struct wl_surface *surface, struct wl_output *output) {
}

static void bar_leave(void *data, struct wl_surface *surface, struct wl_output *output) {
}

static void bar_preferred_scale(void *data, struct wl_surface *surface, int32_t factor) {
	struct bar *b = data;
	factor = factor > 0 ? factor : 1;
	if (factor == b->scale) {
		return;
	}
	b->scale = factor;
	if (b == g.active) {
		g.scale = factor;
	}
	if (b->configured && g.open_index < 0) {
		rebuild();
	}
}

static void bar_preferred_transform(void *data, struct wl_surface *surface, uint32_t transform) {
}

/* 1.25, 1.5, 1.75: the screen's own pixels (scale in 120ths). */
static void bar_fractional_scale(void *data, struct wp_fractional_scale_v1 *fractional,
		uint32_t scale) {
	struct bar *b = data;
	const int quarters = (int)((scale + 15) / 30);
	const int q = quarters % 4 ? quarters : 0;
	if (q == b->quarters) {
		return;
	}
	b->quarters = q;
	if (b == g.active) {
		g.quarters = q;
	}
	if (b->configured && g.open_index < 0) {
		rebuild();
	}
}

static const struct wp_fractional_scale_v1_listener bar_fractional_listener = {
	.preferred_scale = bar_fractional_scale,
};

static const struct wl_surface_listener bar_surface_listener = {
	.enter = bar_enter,
	.leave = bar_leave,
	.preferred_buffer_scale = bar_preferred_scale,
	.preferred_buffer_transform = bar_preferred_transform,
};

static void create_bar(struct output *output) {
	struct bar *b = calloc(1, sizeof(*b));
	if (!b) {
		return;
	}
	b->output = output;
	/* Until zacos9-wm says (wl_surface v6), the screen's own scale. */
	b->scale = output ? output->scale : 1;
	if (!output && !wl_list_empty(&g.outputs)) {
		struct output *first = wl_container_of(g.outputs.next, first, link);
		b->scale = first->scale;
	}
	b->surface = wl_compositor_create_surface(g.compositor);
	if (g.compositor_version >= 6) {
		wl_surface_add_listener(b->surface, &bar_surface_listener, b);
	}
	b->viewport = wp_viewporter_get_viewport(g.viewporter, b->surface);
	if (g.fractional) {
		b->fractional = wp_fractional_scale_manager_v1_get_fractional_scale(g.fractional,
			b->surface);
		wp_fractional_scale_v1_add_listener(b->fractional, &bar_fractional_listener, b);
	}
	b->layer = zwlr_layer_shell_v1_get_layer_surface(g.layer_shell, b->surface,
		output ? output->wl : NULL, ZWLR_LAYER_SHELL_V1_LAYER_TOP, "zacos9-menubar");
	zwlr_layer_surface_v1_set_anchor(b->layer,
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	zwlr_layer_surface_v1_set_size(b->layer, 0, MBAR_HEIGHT);
	zwlr_layer_surface_v1_set_exclusive_zone(b->layer, MBAR_HEIGHT);
	zwlr_layer_surface_v1_add_listener(b->layer, &bar_listener, b);
	wl_surface_commit(b->surface);
	wl_list_insert(g.bars.prev, &b->link);
}

static void destroy_bar(struct bar *b) {
	if (b == g.active) {
		close_menu();
		g.active = NULL;
	}
	wl_list_remove(&b->link);
	zwlr_layer_surface_v1_destroy(b->layer);
	if (b->fractional) {
		wp_fractional_scale_v1_destroy(b->fractional);
	}
	wp_viewport_destroy(b->viewport);
	wl_surface_destroy(b->surface);
	free(b);
	if (!g.active && !wl_list_empty(&g.bars)) {
		activate(wl_container_of(g.bars.next, b, link));
	}
}

static bool every_display_setting(void) {
	char v[8];
	return pl_setting("menubar-every-display", v, sizeof(v)) && strcmp(v, "1") == 0;
}

/* One bar on the main display, or one on each screen, as the Monitors
 * panel says; bars for screens that came or went. */
static void sync_bars(void) {
	const bool every = every_display_setting();
	struct bar *b, *tmp;
	if (every != g.every_display) {
		close_menu();
		wl_list_for_each_safe(b, tmp, &g.bars, link) {
			destroy_bar(b);
		}
		g.every_display = every;
	}
	if (!every) {
		if (wl_list_empty(&g.bars)) {
			create_bar(NULL);
		}
		return;
	}
	struct output *o;
	wl_list_for_each(o, &g.outputs, link) {
		bool has = false;
		wl_list_for_each(b, &g.bars, link) {
			has = has || b->output == o;
		}
		if (!has) {
			create_bar(o);
		}
	}
}

/* ---- the front window's process (global menus) ---------------------------- */

static void shell_window_position(void *data, struct platinum_shell_v1 *shell,
		struct wl_surface *surface, int32_t x, int32_t y) {
}

static void shell_active_client(void *data, struct platinum_shell_v1 *shell, uint32_t pid) {
	appmenu_set_active_pid(pid);
}

static void shell_active_gtk_menu(void *data, struct platinum_shell_v1 *shell,
		const char *bus_name, const char *app_menu_path, const char *menubar_path,
		const char *window_path, const char *application_path) {
	appmenu_set_active_gtk(bus_name, app_menu_path, menubar_path, window_path, application_path);
}

/* ⌘-Tab, caught by zacos9-wm: we know the names, so we choose. */
static void shell_switch_app(void *data, struct platinum_shell_v1 *shell, int32_t step) {
	platinum_shell_v1_activate_app(shell, app_switch_target(step < 0 ? -1 : 1));
}

/* (Only the Sniffer's windows ask for it.) */
static void shell_title_menu(void *data, struct platinum_shell_v1 *shell,
		struct wl_surface *surface, uint32_t option) {
}

static void shell_keyboard_layouts(void *data, struct platinum_shell_v1 *shell,
		const char *layouts, uint32_t current) {
	kbmenu_set(layouts, current);
	menubar_apps_changed();
}

static void shell_proxy_drag(void *data, struct platinum_shell_v1 *shell, struct wl_surface *surface) {
}

static void shell_sticky_keys(void *data, struct platinum_shell_v1 *shell, uint32_t on,
		uint32_t latched, uint32_t locked) {
	menus_set_sticky(on != 0, latched, locked);
	menubar_apps_changed();
}

void menubar_set_keyboard_layout(int index) {
	if (g.platinum_shell && wl_proxy_get_version((struct wl_proxy *)g.platinum_shell) >=
			PLATINUM_SHELL_V1_SET_KEYBOARD_LAYOUT_SINCE_VERSION) {
		platinum_shell_v1_set_keyboard_layout(g.platinum_shell, (uint32_t)index);
	}
}

static const struct platinum_shell_v1_listener shell_listener = {
	.window_position = shell_window_position,
	.active_client = shell_active_client,
	.active_gtk_menu = shell_active_gtk_menu,
	.switch_app = shell_switch_app,
	.title_menu = shell_title_menu,
	.keyboard_layouts = shell_keyboard_layouts,
	.proxy_drag = shell_proxy_drag,
	.sticky_keys = shell_sticky_keys,
};

static void flush_launch_feedback(void) {
	if (wl_display_flush(g.display) < 0 && errno != EAGAIN) {
		fprintf(stderr, "Could not send launch feedback: %s\n", strerror(errno));
	}
}

uint32_t launch_feedback_begin(void) {
	if (!g.platinum_shell ||
			wl_proxy_get_version((struct wl_proxy *)g.platinum_shell) < 3) {
		return 0;
	}
	static uint32_t cookie;
	if (++cookie == 0) {
		++cookie;
	}
	platinum_shell_v1_begin_launch(g.platinum_shell, cookie, 0, "");
	flush_launch_feedback();
	return cookie;
}

void launch_feedback_update(uint32_t cookie, uint32_t pid) {
	if (cookie) {
		platinum_shell_v1_update_launch(g.platinum_shell, cookie, pid);
		flush_launch_feedback();
	}
}

void launch_feedback_cancel(uint32_t cookie) {
	if (cookie) {
		platinum_shell_v1_cancel_launch(g.platinum_shell, cookie);
		flush_launch_feedback();
	}
}

/* ---- globals ------------------------------------------------------------- */

static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
		const char *iface, uint32_t version) {
	if (strcmp(iface, wl_compositor_interface.name) == 0) {
		/* 6: each bar told its own screen's scale. */
		g.compositor_version = version < 6 ? (int)version : 6;
		g.compositor = wl_registry_bind(reg, name, &wl_compositor_interface,
			g.compositor_version);
	} else if (strcmp(iface, wl_subcompositor_interface.name) == 0) {
		g.subcompositor = wl_registry_bind(reg, name, &wl_subcompositor_interface, 1);
	} else if (strcmp(iface, wl_shm_interface.name) == 0) {
		g.shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
	} else if (strcmp(iface, wl_seat_interface.name) == 0 && !g.seat) {
		g.seat = wl_registry_bind(reg, name, &wl_seat_interface, 5);
		wl_seat_add_listener(g.seat, &seat_listener, NULL);
	} else if (strcmp(iface, wl_output_interface.name) == 0) {
		struct output *o = calloc(1, sizeof(*o));
		if (o) {
			o->global = name;
			o->scale = 1;
			o->wl = wl_registry_bind(reg, name, &wl_output_interface, 2);
			wl_output_add_listener(o->wl, &output_listener, o);
			wl_list_insert(g.outputs.prev, &o->link);
			if (g.running) {
				sync_bars(); /* a screen plugged in gets its bar */
			}
		}
	} else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
		g.layer_shell = wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, 4);
	} else if (strcmp(iface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
		g.toplevel_mgr = wl_registry_bind(reg, name,
			&zwlr_foreign_toplevel_manager_v1_interface, 3);
	} else if (strcmp(iface, wp_viewporter_interface.name) == 0) {
		g.viewporter = wl_registry_bind(reg, name, &wp_viewporter_interface, 1);
	} else if (strcmp(iface, wp_fractional_scale_manager_v1_interface.name) == 0) {
		g.fractional = wl_registry_bind(reg, name, &wp_fractional_scale_manager_v1_interface, 1);
	} else if (strcmp(iface, zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
		g.vkbd_mgr = wl_registry_bind(reg, name,
			&zwp_virtual_keyboard_manager_v1_interface, 1);
	} else if (strcmp(iface, platinum_shell_v1_interface.name) == 0 && version >= 2) {
		g.platinum_shell = wl_registry_bind(reg, name, &platinum_shell_v1_interface,
			version < 13 ? version : 13);
		platinum_shell_v1_add_listener(g.platinum_shell, &shell_listener, NULL);
	}
}

static void registry_remove(void *data, struct wl_registry *reg, uint32_t name) {
	struct output *o, *tmp;
	wl_list_for_each_safe(o, tmp, &g.outputs, link) {
		if (o->global != name) {
			continue;
		}
		struct bar *b, *btmp;
		wl_list_for_each_safe(b, btmp, &g.bars, link) {
			if (b->output == o) {
				destroy_bar(b);
			}
		}
		wl_list_remove(&o->link);
		wl_output_destroy(o->wl);
		free(o);
	}
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
	wl_list_init(&g.outputs);
	wl_list_init(&g.bars);
	g.display = wl_display_connect(NULL);
	if (!g.display) {
		fprintf(stderr, "zacos9-menubar: cannot connect to Wayland\n");
		return 1;
	}
	struct wl_registry *reg = wl_display_get_registry(g.display);
	wl_registry_add_listener(reg, &registry_listener, NULL);
	wl_display_roundtrip(g.display);
	if (!g.compositor || !g.subcompositor || !g.shm || !g.layer_shell ||
			!g.viewporter || !g.toplevel_mgr || !g.seat) {
		fprintf(stderr, "zacos9-menubar: compositor lacks required protocols "
			"(needs zacos9-wm)\n");
		return 1;
	}
	wl_display_roundtrip(g.display); /* output scale, seat caps */

	if (g.vkbd_mgr) {
		keys_init(g.vkbd_mgr, g.seat);
	}
	toplevels_init(g.toplevel_mgr);
	if (g.platinum_shell) {
		appmenu_init();
	}
	/* The Pack (the Application palette) comes back if it was left open. */
	char pack_open[16];
	if (pl_setting("pack-open", pack_open, sizeof(pack_open)) && strcmp(pack_open, "true") == 0) {
		char command[PATH_MAX + 64];
		sibling_program("zacos9-pack", command, sizeof(command));
		launch(command);
	}
	const char *cursor_size = getenv("XCURSOR_SIZE");
	g.cursor_px = cursor_size && atoi(cursor_size) > 0 ? atoi(cursor_size) : 16;
	g.cursor_surface = wl_compositor_create_surface(g.compositor);

	g.running = true;
	sync_bars();

	g.clock_fd = timerfd_create(CLOCK_REALTIME, TFD_CLOEXEC | TFD_NONBLOCK);
	g.blink_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	arm_clock();

	/* Watch the settings folder (the panels write desktop.conf there). */
	int settings_fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
	if (settings_fd >= 0) {
		const char *config = getenv("XDG_CONFIG_HOME");
		const char *home = getenv("HOME");
		char dir[1024];
		if (config && *config) {
			snprintf(dir, sizeof(dir), "%s/zacos9", config);
		} else {
			snprintf(dir, sizeof(dir), "%s/.config/zacos9", home ? home : "");
		}
		mkdir(dir, 0755);
		inotify_add_watch(settings_fd, dir, IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE);
	}

	while (g.running) {
		while (wl_display_prepare_read(g.display) != 0) {
			wl_display_dispatch_pending(g.display);
		}
		wl_display_flush(g.display);
		struct pollfd fds[5 + 16] = {
			{ .fd = wl_display_get_fd(g.display), .events = POLLIN },
			{ .fd = g.clock_fd, .events = POLLIN },
			{ .fd = g.blink_fd, .events = POLLIN },
			{ .fd = finder_fd(), .events = POLLIN }, /* -1 is ignored */
			{ .fd = settings_fd, .events = POLLIN },
		};
		/* The Finder may start after us, or restart: while we have no line
		 * to it, look again every second. */
		int timeout = finder_fd() < 0 ? 1000 : -1;
		const int logout = logout_poll_timeout();
		if (logout >= 0 && (timeout < 0 || logout < timeout)) {
			timeout = logout;
		}
		int n_glib = appmenu_poll_prepare(fds + 5, 16, &timeout);
		if (poll(fds, 5 + n_glib, timeout) < 0 && errno != EINTR) {
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
			menus_hourly_chime();
			menubar_apps_changed(); /* rebuilds the clock title */
		}
		if ((fds[2].revents & POLLIN) &&
				read(g.blink_fd, &expirations, sizeof(expirations)) > 0) {
			blink_tick();
		}
		if (settings_fd >= 0 && (fds[4].revents & POLLIN)) {
			/* desktop.conf changed: clock options, accent colour, a bar on
			 * every screen or not. */
			char events[4096];
			while (read(settings_fd, events, sizeof(events)) > 0) {
			}
			if (every_display_setting() != g.every_display && g.open_index < 0) {
				sync_bars();
			}
			menubar_apps_changed();
		}
		if (fds[3].fd >= 0 && (fds[3].revents & (POLLIN | POLLHUP)) && finder_read()) {
			menubar_apps_changed(); /* re-enable Finder menu items */
		} else if (finder_fd() < 0 && finder_connect()) {
			menubar_apps_changed(); /* state arrives shortly; rebuild then too */
		}
		appmenu_poll_dispatch(fds + 5); /* D-Bus: other programs' menus */
		logout_tick();
	}
	return 0;
}
