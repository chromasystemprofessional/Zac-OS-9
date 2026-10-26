/*
 * zacos9-keyboard: the on-screen keyboard. zacos9-wm starts it when a text
 * field has the keyboard focus and no keyboard is attached
 * (compositor/src/osk.c); it shows along the bottom of the screen while
 * $XDG_RUNTIME_DIR/zacos9-keyboard-$WAYLAND_DISPLAY says "shown 1" and
 * leaves when it says 0, or when its hide key is pressed.
 *
 * A bottom layer surface (namespace "zacos9-keyboard") that never takes
 * the keyboard focus, so the field keeps it. It types through
 * virtual-keyboard with the same layouts as the real keyboard would
 * (kblayout.c: desktop.conf's, the one in use first), each key's tag what
 * it types now (xkb), Shift showing the capitals. Shift, Option, Control
 * and ⌘ stay down for the next key, or until tapped again. Each finger is
 * a key of its own (zacos9-wm gives this surface the touches themselves);
 * the mouse works too. Keys go down when touched and up when let go, so
 * holding one repeats it.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "draw.h"
#include "fractional-scale-v1-client-protocol.h"
#include "kblayout.h"
#include "text.h"
#include "viewporter-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "widgets.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "wlclient.h"

#define FACE GRAY(0xC)
#define MARGIN 6
#define GAP 4
#define UNITS 15 /* a row's width, in ordinary keys */
#define MAX_KEYS 80
#define MAX_TOUCHES 10

enum role { R_CHAR, R_MOD, R_CAPS, R_HIDE, R_PLAIN };

struct key {
	int code;          /* evdev; 0 for hide */
	double units;      /* width */
	enum role role;
	const char *name;  /* its tag when it types no character */
	int x, y, w, h;    /* logical, once laid out */
	bool down;         /* held by a finger or the mouse */
	bool latched;      /* a modifier, down for the next key */
	struct plat_text *tag;
};

/* The Apple Extended Keyboard's main block, Return and Delete on the right,
 * without its function keys. */
static struct key keys[MAX_KEYS];
static int nkeys;
static int row_of[MAX_KEYS];

static void row_keys(int row, const struct key *k, int n) {
	for (int i = 0; i < n && nkeys < MAX_KEYS; i++) {
		row_of[nkeys] = row;
		keys[nkeys++] = k[i];
	}
}

#define C(code) { code, 1, R_CHAR, NULL, 0, 0, 0, 0, false, false, NULL }
#define W(code, u, role, name) { code, u, role, name, 0, 0, 0, 0, false, false, NULL }

static void make_keys(void) {
	const struct key r0[] = { C(KEY_GRAVE), C(KEY_1), C(KEY_2), C(KEY_3), C(KEY_4), C(KEY_5), C(KEY_6),
		C(KEY_7), C(KEY_8), C(KEY_9), C(KEY_0), C(KEY_MINUS), C(KEY_EQUAL),
		W(KEY_BACKSPACE, 2, R_PLAIN, "delete") };
	const struct key r1[] = { W(KEY_TAB, 1.5, R_PLAIN, "tab"), C(KEY_Q), C(KEY_W), C(KEY_E), C(KEY_R),
		C(KEY_T), C(KEY_Y), C(KEY_U), C(KEY_I), C(KEY_O), C(KEY_P), C(KEY_LEFTBRACE), C(KEY_RIGHTBRACE),
		W(KEY_BACKSLASH, 1.5, R_CHAR, NULL) };
	const struct key r2[] = { W(KEY_CAPSLOCK, 1.75, R_CAPS, "caps lock"), C(KEY_A), C(KEY_S), C(KEY_D),
		C(KEY_F), C(KEY_G), C(KEY_H), C(KEY_J), C(KEY_K), C(KEY_L), C(KEY_SEMICOLON), C(KEY_APOSTROPHE),
		W(KEY_ENTER, 2.25, R_PLAIN, "return") };
	const struct key r3[] = { W(KEY_LEFTSHIFT, 2.25, R_MOD, "shift"), C(KEY_Z), C(KEY_X), C(KEY_C),
		C(KEY_V), C(KEY_B), C(KEY_N), C(KEY_M), C(KEY_COMMA), C(KEY_DOT), C(KEY_SLASH),
		W(KEY_RIGHTSHIFT, 2.75, R_MOD, "shift") };
	const struct key r4[] = { W(KEY_LEFTCTRL, 1.25, R_MOD, "control"), W(KEY_LEFTALT, 1.25, R_MOD, "option"),
		W(KEY_LEFTMETA, 1.5, R_MOD, "\xe2\x8c\x98"), W(KEY_SPACE, 5.5, R_PLAIN, ""),
		W(KEY_LEFT, 1, R_PLAIN, "\xe2\x86\x90"), W(KEY_DOWN, 1, R_PLAIN, "\xe2\x86\x93"),
		W(KEY_UP, 1, R_PLAIN, "\xe2\x86\x91"), W(KEY_RIGHT, 1, R_PLAIN, "\xe2\x86\x92"),
		W(0, 1.5, R_HIDE, "hide") };
	row_keys(0, r0, sizeof(r0) / sizeof(r0[0]));
	row_keys(1, r1, sizeof(r1) / sizeof(r1[0]));
	row_keys(2, r2, sizeof(r2) / sizeof(r2[0]));
	row_keys(3, r3, sizeof(r3) / sizeof(r3[0]));
	row_keys(4, r4, sizeof(r4) / sizeof(r4[0]));
}

/* ---- typing ------------------------------------------------------------------ */

static struct wl_display *display;
static struct wl_seat *seat;
static struct zwp_virtual_keyboard_manager_v1 *vkbd_manager;
static struct zwp_virtual_keyboard_v1 *vkbd;
static struct xkb_state *xkb;
static bool quitting;

static uint32_t now_ms(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

/* The real keyboard's keymap, the layout in use first. */
static bool make_keyboard(void) {
	struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct kb_layouts layouts;
	kb_layouts_load(&layouts);
	struct xkb_keymap *map = ctx ? kb_layouts_keymap(ctx, &layouts) : NULL;
	if (!map) {
		struct xkb_rule_names names = { .layout = "us" };
		map = ctx ? xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS) : NULL;
	}
	if (!map) {
		return false;
	}
	char *text = xkb_keymap_get_as_string(map, XKB_KEYMAP_FORMAT_TEXT_V1);
	const size_t size = strlen(text) + 1;
	const int fd = memfd_create("zacos9-keyboard-keymap", MFD_CLOEXEC);
	if (fd < 0 || write(fd, text, size) != (ssize_t)size) {
		free(text);
		return false;
	}
	vkbd = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(vkbd_manager, seat);
	zwp_virtual_keyboard_v1_keymap(vkbd, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, size);
	close(fd);
	free(text);
	xkb = xkb_state_new(map);
	char code[64];
	const int group = kb_layout_remembered(code, sizeof(code)) ? kb_layouts_find(&layouts, code) : -1;
	if (group > 0) {
		xkb_state_update_mask(xkb, 0, 0, 0, 0, 0, (xkb_layout_index_t)group);
	}
	xkb_keymap_unref(map);
	xkb_context_unref(ctx);
	return true;
}

/* A virtual keyboard says its modifiers itself (wlroots doesn't work them
 * out from its keys). */
static void send_key(int code, bool down) {
	xkb_state_update_key(xkb, (xkb_keycode_t)code + 8, down ? XKB_KEY_DOWN : XKB_KEY_UP);
	zwp_virtual_keyboard_v1_key(vkbd, now_ms(), (uint32_t)code,
		down ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
	zwp_virtual_keyboard_v1_modifiers(vkbd,
		xkb_state_serialize_mods(xkb, XKB_STATE_MODS_DEPRESSED),
		xkb_state_serialize_mods(xkb, XKB_STATE_MODS_LATCHED),
		xkb_state_serialize_mods(xkb, XKB_STATE_MODS_LOCKED),
		xkb_state_serialize_layout(xkb, XKB_STATE_LAYOUT_EFFECTIVE));
}

static void release_latched(void) {
	for (int i = 0; i < nkeys; i++) {
		if (keys[i].latched) {
			keys[i].latched = false;
			send_key(keys[i].code, false);
		}
	}
}

static void key_press(struct key *k) {
	switch (k->role) {
	case R_MOD:
		k->latched = !k->latched;
		send_key(k->code, k->latched);
		break;
	case R_CAPS:
		send_key(k->code, true);
		send_key(k->code, false);
		break;
	case R_HIDE:
		quitting = true;
		break;
	case R_CHAR:
	case R_PLAIN:
		k->down = true;
		send_key(k->code, true);
		break;
	}
}

static void key_release(struct key *k) {
	if (!k->down) {
		return;
	}
	k->down = false;
	send_key(k->code, false);
	release_latched(); /* Shift for one key */
}

/* ---- drawing ------------------------------------------------------------------ */

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct wp_viewporter *viewporter;
static struct wp_fractional_scale_manager_v1 *fractional_manager;
static struct wl_surface *surface;
static struct zwlr_layer_surface_v1 *layer;
static struct wp_viewport *viewport;
static int w, h;        /* logical */
static int scale120 = 120;
static struct pl_shm_buffer buffers[2];
static uint32_t *logical;
static bool dirty;

/* Whole-number scales draw each logical pixel k x k; 1.25, 1.5 and 1.75
 * (quarters 5, 6, 7) at the screen's own pixels, text in that scale's font. */
static int whole(void) { return scale120 % 120 == 0 ? scale120 / 120 : 0; }
static int quarters(void) { return whole() ? 0 : (scale120 + 15) / 30; }

static int key_height(void) {
	const int unit = (w - 2 * MARGIN) / UNITS;
	return unit < 34 ? 34 : unit > 48 ? 48 : unit;
}

static int keyboard_height(void) {
	return 2 + MARGIN * 2 + 5 * key_height() + 4 * GAP;
}

static void layout(void) {
	const double unit = (double)(w - 2 * MARGIN + GAP) / UNITS;
	const int kh = key_height();
	double x = MARGIN;
	int row = -1;
	for (int i = 0; i < nkeys; i++) {
		if (row_of[i] != row) {
			row = row_of[i];
			x = MARGIN;
		}
		struct key *k = &keys[i];
		k->x = (int)(x + 0.5);
		k->w = (int)(x + k->units * unit + 0.5) - k->x - GAP;
		k->y = 2 + MARGIN + row * (kh + GAP);
		k->h = kh;
		x += k->units * unit;
	}
}

/* What a key types now (Shift, Option, the layout), else its name. */
static void tag(struct key *k) {
	char text[64] = "";
	if (k->role == R_CHAR) {
		xkb_state_key_get_utf8(xkb, (xkb_keycode_t)k->code + 8, text, sizeof(text));
		if ((unsigned char)text[0] < ' ') {
			text[0] = '\0'; /* a dead key, or a control character */
		}
	} else {
		snprintf(text, sizeof(text), "%s", k->name ? k->name : "");
	}
	if (k->tag) {
		text_destroy(k->tag);
	}
	k->tag = text[0] ? text_render_font(text, k->w - 6, PL_FONT_SYSTEM) : NULL;
}

static void paint(struct pl_canvas *c) {
	pl_fill(c, 0, 0, w - 1, h - 1, FACE);
	pl_hline(c, 0, w - 1, 0, C_BLACK);
	pl_hline(c, 0, w - 1, 1, C_WHITE);
	for (int i = 0; i < nkeys; i++) {
		struct key *k = &keys[i];
		tag(k);
		pl_bevel_button_paint(c, k->x, k->y, k->w, k->h, k->down || k->latched);
		if (k->tag) {
			const int ink = k->tag->ink_r - k->tag->ink_l + 1;
			const int shift = k->down || k->latched ? 1 : 0;
			pl_text(c, k->tag, k->x + (k->w - ink) / 2 + shift, k->y + k->h / 2 + 5 + shift, C_BLACK);
		}
	}
}

static void draw(void) {
	if (!surface || w <= 0 || h <= 0) {
		return;
	}
	struct pl_shm_buffer *b = pl_shm_buffer_pick(buffers);
	if (!b) {
		dirty = true;
		return;
	}
	const int k = whole(), q = quarters();
	if (q) {
		struct pl_canvas c = { logical, w, 0, 0, w, h, pl_text_log_new(logical, w) };
		paint(&c);
		pl_snap_scale_canvas(&c, 0, 0, q, b->px);
		pl_text_log_free(c.log);
	} else if (k == 1) {
		struct pl_canvas c = { b->px, w, 0, 0, w, h, NULL };
		paint(&c);
	} else {
		struct pl_canvas c = { logical, w, 0, 0, w, h, NULL };
		paint(&c);
		for (int y = 0; y < h; y++) {
			uint32_t *out = b->px + (size_t)y * k * w * k;
			const uint32_t *in = logical + (size_t)y * w;
			for (int x = 0; x < w; x++) {
				for (int i = 0; i < k; i++) {
					out[x * k + i] = in[x];
				}
			}
			for (int i = 1; i < k; i++) {
				memcpy(out + (size_t)i * w * k, out, (size_t)w * k * 4);
			}
		}
	}
	b->busy = true;
	wl_surface_attach(surface, b->wl, 0, 0);
	wl_surface_damage_buffer(surface, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_commit(surface);
}

static void resize(void) {
	pl_shm_buffer_free(&buffers[0]);
	pl_shm_buffer_free(&buffers[1]);
	free(logical);
	const int k = whole(), q = quarters();
	const int bw = q ? pl_snap(w, q) : w * k, bh = q ? pl_snap(h, q) : h * k;
	logical = malloc((size_t)w * h * 4);
	if (!logical || !pl_shm_buffer_make(&buffers[0], shm, "zacos9-keyboard", bw, bh, WL_SHM_FORMAT_XRGB8888) || !pl_shm_buffer_make(&buffers[1], shm, "zacos9-keyboard", bw, bh, WL_SHM_FORMAT_XRGB8888)) {
		fprintf(stderr, "zacos9-keyboard: no memory\n");
		quitting = true;
		return;
	}
	wl_surface_set_buffer_scale(surface, q ? 1 : k);
	if (viewport) {
		wp_viewport_set_destination(viewport, q ? w : -1, q ? h : -1);
	}
	layout();
	draw();
}

static void layer_configure(void *data, struct zwlr_layer_surface_v1 *l, uint32_t serial, uint32_t cw,
		uint32_t ch) {
	zwlr_layer_surface_v1_ack_configure(l, serial);
	if ((int)cw == w && (int)ch == h && logical) {
		return;
	}
	w = (int)cw;
	h = (int)ch;
	/* The height follows from the width: ask again if it's wrong. */
	if (h != keyboard_height()) {
		zwlr_layer_surface_v1_set_size(layer, 0, (uint32_t)keyboard_height());
		zwlr_layer_surface_v1_set_exclusive_zone(layer, keyboard_height());
		wl_surface_commit(surface);
	}
	resize();
}

static void layer_closed(void *data, struct zwlr_layer_surface_v1 *l) {
	quitting = true;
}

static const struct zwlr_layer_surface_v1_listener layer_listener = { layer_configure, layer_closed };

static void preferred_scale(void *data, struct wp_fractional_scale_v1 *f, uint32_t scale) {
	if ((int)scale == scale120) {
		return;
	}
	scale120 = (int)scale;
	if (w > 0) {
		resize();
	}
}

static const struct wp_fractional_scale_v1_listener fractional_listener = { preferred_scale };

static void show(void) {
	surface = wl_compositor_create_surface(compositor);
	if (viewporter && fractional_manager) {
		viewport = wp_viewporter_get_viewport(viewporter, surface);
		struct wp_fractional_scale_v1 *f =
			wp_fractional_scale_manager_v1_get_fractional_scale(fractional_manager, surface);
		wp_fractional_scale_v1_add_listener(f, &fractional_listener, NULL);
	}
	layer = zwlr_layer_shell_v1_get_layer_surface(layer_shell, surface, NULL,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP, "zacos9-keyboard");
	zwlr_layer_surface_v1_add_listener(layer, &layer_listener, NULL);
	zwlr_layer_surface_v1_set_anchor(layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
		ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
	/* A first guess; the real height comes with the width. */
	zwlr_layer_surface_v1_set_size(layer, 0, 240);
	zwlr_layer_surface_v1_set_exclusive_zone(layer, 240);
	zwlr_layer_surface_v1_set_keyboard_interactivity(layer,
		ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
	wl_surface_commit(surface);
}

/* ---- pointer and touch ------------------------------------------------------ */

static struct key *key_at(double x, double y) {
	for (int i = 0; i < nkeys; i++) {
		struct key *k = &keys[i];
		/* The gaps count as the key to their left and above. */
		if (x >= k->x && x < k->x + k->w + GAP && y >= k->y && y < k->y + k->h + GAP) {
			return k;
		}
	}
	return NULL;
}

static double pointer_x, pointer_y;
static struct key *pointer_key;

static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s,
		wl_fixed_t x, wl_fixed_t y) {
	pointer_x = wl_fixed_to_double(x);
	pointer_y = wl_fixed_to_double(y);
}

static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s) {
	if (pointer_key) {
		key_release(pointer_key);
		pointer_key = NULL;
		draw();
	}
}

static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t x, wl_fixed_t y) {
	pointer_x = wl_fixed_to_double(x);
	pointer_y = wl_fixed_to_double(y);
}

static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time,
		uint32_t button, uint32_t state) {
	if (button != BTN_LEFT) {
		return;
	}
	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		pointer_key = key_at(pointer_x, pointer_y);
		if (pointer_key) {
			key_press(pointer_key);
		}
	} else if (pointer_key) {
		key_release(pointer_key);
		pointer_key = NULL;
	}
	draw();
}

static void pointer_axis(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis, wl_fixed_t v) {
}

static void pointer_frame(void *data, struct wl_pointer *p) {
}

static void pointer_axis_source(void *data, struct wl_pointer *p, uint32_t source) {
}

static void pointer_axis_stop(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis) {
}

static void pointer_axis_discrete(void *data, struct wl_pointer *p, uint32_t axis, int32_t discrete) {
}

/* Every event of the seat's version 5: libwayland aborts on a missing one. */
static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_enter, .leave = pointer_leave, .motion = pointer_motion,
	.button = pointer_button, .axis = pointer_axis, .frame = pointer_frame,
	.axis_source = pointer_axis_source, .axis_stop = pointer_axis_stop,
	.axis_discrete = pointer_axis_discrete,
};

static struct { int32_t id; struct key *key; } touches[MAX_TOUCHES];

static void touch_down(void *data, struct wl_touch *t, uint32_t serial, uint32_t time,
		struct wl_surface *s, int32_t id, wl_fixed_t x, wl_fixed_t y) {
	struct key *k = key_at(wl_fixed_to_double(x), wl_fixed_to_double(y));
	if (!k) {
		return;
	}
	for (int i = 0; i < MAX_TOUCHES; i++) {
		if (!touches[i].key) {
			touches[i].id = id;
			touches[i].key = k;
			key_press(k);
			draw();
			return;
		}
	}
}

static void touch_up(void *data, struct wl_touch *t, uint32_t serial, uint32_t time, int32_t id) {
	for (int i = 0; i < MAX_TOUCHES; i++) {
		if (touches[i].key && touches[i].id == id) {
			key_release(touches[i].key);
			touches[i].key = NULL;
			draw();
		}
	}
}

static void touch_motion(void *data, struct wl_touch *t, uint32_t time, int32_t id, wl_fixed_t x,
		wl_fixed_t y) {
}

static void touch_frame(void *data, struct wl_touch *t) {
}

static void touch_cancel(void *data, struct wl_touch *t) {
	for (int i = 0; i < MAX_TOUCHES; i++) {
		if (touches[i].key) {
			key_release(touches[i].key);
			touches[i].key = NULL;
		}
	}
	draw();
}

static void touch_shape(void *data, struct wl_touch *t, int32_t id, wl_fixed_t major, wl_fixed_t minor) {
}

static void touch_orientation(void *data, struct wl_touch *t, int32_t id, wl_fixed_t orientation) {
}

static const struct wl_touch_listener touch_listener = {
	touch_down, touch_up, touch_motion, touch_frame, touch_cancel, touch_shape, touch_orientation,
};

static struct wl_pointer *pointer;
static struct wl_touch *touch;

static void seat_capabilities(void *data, struct wl_seat *s, uint32_t caps) {
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer) {
		pointer = wl_seat_get_pointer(s);
		wl_pointer_add_listener(pointer, &pointer_listener, NULL);
	}
	if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !touch) {
		touch = wl_seat_get_touch(s);
		wl_touch_add_listener(touch, &touch_listener, NULL);
	}
}

static void seat_name(void *data, struct wl_seat *s, const char *name) {
}

static const struct wl_seat_listener seat_listener = { seat_capabilities, seat_name };

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *iface,
		uint32_t version) {
	if (strcmp(iface, wl_compositor_interface.name) == 0) {
		compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
	} else if (strcmp(iface, wl_shm_interface.name) == 0) {
		shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(iface, wp_viewporter_interface.name) == 0) {
		viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
	} else if (strcmp(iface, wp_fractional_scale_manager_v1_interface.name) == 0) {
		fractional_manager = wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1);
	} else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
		layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
	} else if (strcmp(iface, zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
		vkbd_manager = wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
	} else if (strcmp(iface, wl_seat_interface.name) == 0 && !seat) {
		seat = wl_registry_bind(registry, name, &wl_seat_interface, version < 5 ? version : 5);
		wl_seat_add_listener(seat, &seat_listener, NULL);
	}
}

static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {
}

static const struct wl_registry_listener registry_listener = { global, global_remove };

/* ---- the state file ---------------------------------------------------------- */

static char state_path[1024], state_name[256];

static bool wanted(void) {
	FILE *f = fopen(state_path, "r");
	if (!f) {
		return false;
	}
	int shown = 0;
	const bool ok = fscanf(f, "shown %d", &shown) == 1;
	fclose(f);
	return ok && shown;
}

int main(void) {
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	const char *wayland = getenv("WAYLAND_DISPLAY");
	if (!runtime || !wayland) {
		fprintf(stderr, "zacos9-keyboard: no XDG_RUNTIME_DIR or WAYLAND_DISPLAY\n");
		return 1;
	}
	snprintf(state_name, sizeof(state_name), "zacos9-keyboard-%s", wayland);
	snprintf(state_path, sizeof(state_path), "%s/%s", runtime, state_name);
	/* The file is replaced (renamed into place) on every change. */
	const int inotify = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
	inotify_add_watch(inotify, runtime, IN_MOVED_TO | IN_CLOSE_WRITE);
	if (!wanted()) {
		return 0;
	}

	display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "zacos9-keyboard: no Wayland display\n");
		return 1;
	}
	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);
	if (!compositor || !shm || !layer_shell || !vkbd_manager || !seat) {
		fprintf(stderr, "zacos9-keyboard: the compositor lacks layer-shell or virtual-keyboard\n");
		return 1;
	}
	make_keys();
	if (!make_keyboard()) {
		fprintf(stderr, "zacos9-keyboard: no keymap\n");
		return 1;
	}
	show();

	while (!quitting) {
		while (wl_display_prepare_read(display) != 0) {
			wl_display_dispatch_pending(display);
		}
		wl_display_flush(display);
		struct pollfd fds[2] = { { wl_display_get_fd(display), POLLIN, 0 }, { inotify, POLLIN, 0 } };
		if (poll(fds, 2, -1) < 0 && errno != EINTR) {
			wl_display_cancel_read(display);
			break;
		}
		if (fds[0].revents & POLLIN) {
			if (wl_display_read_events(display) < 0) {
				break; /* zacos9-wm has gone */
			}
		} else {
			wl_display_cancel_read(display);
		}
		if (fds[0].revents & (POLLERR | POLLHUP) || wl_display_dispatch_pending(display) < 0) {
			break;
		}
		if (fds[1].revents & POLLIN) {
			char events[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
			bool ours = false;
			ssize_t n;
			while ((n = read(inotify, events, sizeof(events))) > 0) {
				for (char *p = events; p < events + n;) {
					const struct inotify_event *e = (const struct inotify_event *)p;
					ours |= e->len && strcmp(e->name, state_name) == 0;
					p += sizeof(*e) + e->len;
				}
			}
			if (ours && !wanted()) {
				break;
			}
		}
		if (dirty && (!buffers[0].busy || !buffers[1].busy)) {
			dirty = false;
			draw();
		}
	}
	/* Nothing left held down over there. */
	for (int i = 0; i < nkeys; i++) {
		if (keys[i].down || keys[i].latched) {
			send_key(keys[i].code, false);
		}
	}
	wl_display_roundtrip(display);
	return 0;
}
