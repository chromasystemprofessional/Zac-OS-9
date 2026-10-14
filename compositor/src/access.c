#include "access.h"

#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_seat.h>

#include "server.h"
#include "settings.h"

/* ---- Sticky Keys, the state machine ------------------------------------ */

unsigned sticky_mod_for(xkb_keysym_t sym) {
	switch (sym) {
	case XKB_KEY_Shift_L: case XKB_KEY_Shift_R: return STICKY_SHIFT;
	case XKB_KEY_Control_L: case XKB_KEY_Control_R: return STICKY_CONTROL;
	case XKB_KEY_Alt_L: case XKB_KEY_Alt_R: case XKB_KEY_Meta_L: case XKB_KEY_Meta_R:
		return STICKY_OPTION;
	case XKB_KEY_Super_L: case XKB_KEY_Super_R: return STICKY_COMMAND;
	default: return 0;
	}
}

void sticky_event(struct sticky *s, unsigned mod, bool pressed) {
	if (!mod) {
		if (pressed && s->pending) {
			s->other = true; /* held down with this key: an ordinary chord */
		}
		return;
	}
	if (pressed) {
		s->pending = mod;
		s->other = false;
		return;
	}
	if (s->pending != mod || s->other) {
		s->pending = 0;
		return;
	}
	s->pending = 0;
	if (s->locked & mod) {
		s->locked &= ~mod;          /* a third press: let go */
	} else if (s->latched & mod) {
		s->latched &= ~mod;         /* a second: lock it */
		s->locked |= mod;
	} else {
		s->latched |= mod;          /* a first: for the next key */
	}
}

void sticky_key_done(struct sticky *s) {
	s->latched = 0;
}

double access_zoom_step(double factor, int direction) {
	static const double steps[] = { 1, 1.5, 2, 3, 4, 6, 8, 12, 16 };
	const int n = (int)(sizeof(steps) / sizeof(steps[0]));
	int i = 0;
	while (i < n - 1 && steps[i] < factor - 0.01) {
		i++;
	}
	i += direction;
	return steps[i < 0 ? 0 : i >= n ? n - 1 : i];
}

/* ---- the compositor's side ----------------------------------------------- */

static struct {
	struct plat_server *server;
	bool sticky_on, slow_on, mouse_on, zoom_on;
	int slow_delay, mouse_delay, mouse_speed;
	double zoom;
	struct sticky sticky;
	int shift_taps;
	uint32_t last_shift_ms;
	/* Slow Keys: the key being held back. */
	struct plat_keyboard *slow_kb;
	uint32_t slow_key, slow_time;
	bool slow_accepted;
	struct wl_event_source *slow_timer;
	/* Mouse Keys: which way, how fast, held since. */
	int dx[2], dy[2]; /* per held key slot: direction */
	uint32_t held[2];
	double speed;
	uint32_t held_since;
	bool button_locked;
	struct wl_event_source *mouse_timer;
} a;

static uint32_t now_ms(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

static bool flag(const char *key) {
	bool on = false;
	return prefs_flag(key, &on) && on;
}

static int number(const char *key, int fallback, int min, int max) {
	char v[32];
	if (!pl_setting(key, v, sizeof(v))) {
		return fallback;
	}
	const int n = atoi(v);
	return n < min ? min : n > max ? max : n;
}

/* Sticky Keys' bits as the keyboard's modifier mask (zacos9-wm's keymaps
 * keep xkb's usual order, as wlroots' WLR_MODIFIER_* assume). */
static uint32_t wlr_mask(unsigned bits) {
	return ((bits & STICKY_SHIFT) ? WLR_MODIFIER_SHIFT : 0) |
		((bits & STICKY_CONTROL) ? WLR_MODIFIER_CTRL : 0) |
		((bits & STICKY_OPTION) ? WLR_MODIFIER_ALT : 0) |
		((bits & STICKY_COMMAND) ? WLR_MODIFIER_LOGO : 0);
}

void access_merge_modifiers(struct wlr_keyboard_modifiers *m) {
	if (a.sticky_on) {
		m->latched |= wlr_mask(a.sticky.latched);
		m->locked |= wlr_mask(a.sticky.locked);
	}
}

uint32_t access_held_modifiers(void) {
	return a.sticky_on ? wlr_mask(a.sticky.latched | a.sticky.locked) : 0;
}

/* The modifiers clients see (rather than the keyboard's own state, which a
 * virtual keyboard sets as it likes), and the menu bar's marker. */
static void sticky_apply(void) {
	input_send_modifiers(a.server);
	platinum_shell_sticky_changed(a.server);
}

static void toggle_setting(const char *key, bool on) {
	pl_setting_set(key, on ? "true" : NULL);
	/* An ascending or descending pair, as the Mac's arpeggio said so. */
	pl_beep();
	if (on) {
		pl_beep();
	}
}

void access_sticky_state(unsigned *latched, unsigned *locked) {
	*latched = a.sticky_on ? a.sticky.latched : 0;
	*locked = a.sticky_on ? a.sticky.locked : 0;
}

bool access_sticky_on(void) {
	return a.sticky_on;
}

double access_zoom_factor(void) {
	return a.zoom_on ? a.zoom : 1.0;
}

/* ---- Mouse Keys ------------------------------------------------------------ */

/* The keypad's directions, by evdev code. */
static bool keypad_direction(uint32_t key, int *dx, int *dy) {
	switch (key) {
	case KEY_KP7: *dx = -1; *dy = -1; return true;
	case KEY_KP8: *dx = 0; *dy = -1; return true;
	case KEY_KP9: *dx = 1; *dy = -1; return true;
	case KEY_KP4: *dx = -1; *dy = 0; return true;
	case KEY_KP6: *dx = 1; *dy = 0; return true;
	case KEY_KP1: *dx = -1; *dy = 1; return true;
	case KEY_KP2: *dx = 0; *dy = 1; return true;
	case KEY_KP3: *dx = 1; *dy = 1; return true;
	default: return false;
	}
}

/* The button, as the mouse's would be: clients act on a pointer frame. */
static void mouse_button(uint32_t time, bool down) {
	input_pointer_button(a.server, time, BTN_LEFT,
		down ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
	wlr_seat_pointer_notify_frame(a.server->seat);
}

static void mouse_move(int steps) {
	int dx = 0, dy = 0;
	for (int i = 0; i < 2; i++) {
		if (a.held[i]) {
			dx += a.dx[i];
			dy += a.dy[i];
		}
	}
	if (!dx && !dy) {
		return;
	}
	wlr_cursor_move(a.server->cursor, NULL, dx * steps, dy * steps);
	input_pointer_refocus(a.server);
	wlr_seat_pointer_notify_frame(a.server->seat);
}

static int mouse_tick(void *data) {
	if (!a.held[0] && !a.held[1]) {
		return 0;
	}
	/* After the initial delay, faster and faster up to the top speed. */
	if (now_ms() - a.held_since >= (uint32_t)a.mouse_delay) {
		a.speed = a.speed < 1 ? 1 : a.speed + 0.5;
		if (a.speed > a.mouse_speed) {
			a.speed = a.mouse_speed;
		}
		mouse_move((int)a.speed);
	}
	wl_event_source_timer_update(a.mouse_timer, 16);
	return 0;
}

static bool mouse_key(uint32_t key, bool pressed, uint32_t time) {
	int dx, dy;
	if (keypad_direction(key, &dx, &dy)) {
		if (pressed) {
			const int slot = a.held[0] ? 1 : 0;
			a.held[slot] = key;
			a.dx[slot] = dx;
			a.dy[slot] = dy;
			if (!a.held[1 - slot]) {
				a.held_since = now_ms();
				a.speed = 0;
			}
			mouse_move(2); /* a tap moves a little */
			wl_event_source_timer_update(a.mouse_timer, 16);
		} else {
			for (int i = 0; i < 2; i++) {
				if (a.held[i] == key) {
					a.held[i] = 0;
				}
			}
		}
		return true;
	}
	switch (key) {
	case KEY_KP5: /* click */
		if (pressed) {
			mouse_button(time, true);
			mouse_button(time, false);
		}
		return true;
	case KEY_KP0: /* hold the button down */
		if (pressed && !a.button_locked) {
			a.button_locked = true;
			mouse_button(time, true);
		}
		return true;
	case KEY_KPDOT: /* and let it go */
		if (pressed && a.button_locked) {
			a.button_locked = false;
			mouse_button(time, false);
		}
		return true;
	default:
		return false;
	}
}

/* ---- Slow Keys ------------------------------------------------------------- */

static int slow_accept(void *data) {
	if (a.slow_kb && a.slow_key) {
		a.slow_accepted = true;
		input_deliver_key(a.slow_kb, a.slow_key, WL_KEYBOARD_KEY_STATE_PRESSED, now_ms());
	}
	return 0;
}

/* ---- keys ------------------------------------------------------------------- */

bool access_key(struct plat_keyboard *kb, uint32_t keycode, enum wl_keyboard_key_state state,
		uint32_t time_msec) {
	const bool pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
	const xkb_keysym_t sym = kb->wlr_keyboard->keymap ?
		xkb_state_key_get_one_sym(kb->wlr_keyboard->xkb_state, keycode + 8) : XKB_KEY_NoSymbol;
	const uint32_t mods = wlr_keyboard_get_modifiers(kb->wlr_keyboard);

	/* Mouse Keys: ⌘-Shift-Num Lock on, Num Lock alone off; the keypad. */
	if (keycode == KEY_NUMLOCK && pressed) {
		if (!a.mouse_on && (mods & WLR_MODIFIER_LOGO) && (mods & WLR_MODIFIER_SHIFT)) {
			toggle_setting("mouse-keys", true);
			a.mouse_on = true;
			return true;
		}
		if (a.mouse_on && !(mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT))) {
			toggle_setting("mouse-keys", false);
			a.mouse_on = false;
			return true;
		}
	}
	if (a.mouse_on && mouse_key(keycode, pressed, time_msec)) {
		return true;
	}

	/* Shift five times in a row: Sticky Keys on or off. */
	const unsigned mod = sticky_mod_for(sym);
	if (pressed && mod == STICKY_SHIFT) {
		const uint32_t now = now_ms();
		a.shift_taps = now - a.last_shift_ms < 1500 ? a.shift_taps + 1 : 1;
		a.last_shift_ms = now;
		if (a.shift_taps == 5) {
			a.shift_taps = 0;
			a.sticky_on = !a.sticky_on;
			a.sticky = (struct sticky){ 0 };
			toggle_setting("sticky-keys", a.sticky_on);
			sticky_apply();
		}
	} else if (pressed && !mod) {
		a.shift_taps = 0;
	}

	/* Slow Keys: a press waits; let go too soon, it never happened. */
	if (a.slow_on && !kb->is_virtual) {
		if (pressed) {
			a.slow_kb = kb;
			a.slow_key = keycode;
			a.slow_time = time_msec;
			a.slow_accepted = false;
			wl_event_source_timer_update(a.slow_timer, a.slow_delay);
			return true;
		}
		if (keycode == a.slow_key) {
			wl_event_source_timer_update(a.slow_timer, 0);
			const bool accepted = a.slow_accepted;
			a.slow_key = 0;
			a.slow_kb = NULL;
			if (!accepted) {
				return true;
			}
		}
	}
	return false;
}

void access_key_delivered(struct plat_keyboard *kb, uint32_t keycode, enum wl_keyboard_key_state state) {
	if (!a.sticky_on || !kb->wlr_keyboard->keymap) {
		return;
	}
	const bool pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
	const unsigned mod = sticky_mod_for(
		xkb_state_key_get_one_sym(kb->wlr_keyboard->xkb_state, keycode + 8));
	const struct sticky before = a.sticky;
	sticky_event(&a.sticky, mod, pressed);
	if (!mod && !pressed && a.sticky.latched) {
		sticky_key_done(&a.sticky); /* used by the key just typed */
	}
	if (a.sticky.latched != before.latched || a.sticky.locked != before.locked) {
		sticky_apply();
	}
}

void access_click_done(void) {
	if (a.sticky_on && a.sticky.latched) {
		sticky_key_done(&a.sticky);
		sticky_apply();
	}
}

bool access_keybinding(struct plat_server *server, uint32_t mods, xkb_keysym_t sym) {
	const uint32_t want = WLR_MODIFIER_LOGO | WLR_MODIFIER_ALT;
	if ((mods & (want | WLR_MODIFIER_CTRL)) != want) {
		return false;
	}
	if (sym == XKB_KEY_k || sym == XKB_KEY_K) {
		toggle_setting("zoom", !a.zoom_on);
		a.zoom_on = !a.zoom_on;
	} else if (sym == XKB_KEY_Up || sym == XKB_KEY_Down) {
		a.zoom = access_zoom_step(a.zoom, sym == XKB_KEY_Up ? 1 : -1);
		if (a.zoom <= 1.0) {
			a.zoom = 1.5;
		}
		char v[16];
		snprintf(v, sizeof(v), "%g", a.zoom);
		pl_setting_set("zoom-factor", v);
		if (!a.zoom_on) {
			a.zoom_on = true;
			pl_setting_set("zoom", "true");
		}
	} else {
		return false;
	}
	struct plat_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		wlr_output_schedule_frame(output->wlr_output);
	}
	return true;
}

void access_apply(struct plat_server *server) {
	const bool sticky = flag("sticky-keys");
	if (sticky != a.sticky_on) {
		a.sticky_on = sticky;
		a.sticky = (struct sticky){ 0 };
		sticky_apply();
	}
	a.slow_on = flag("slow-keys");
	a.slow_delay = number("slow-keys-delay", 750, 100, 3000);
	a.mouse_on = flag("mouse-keys");
	a.mouse_delay = number("mouse-keys-delay", 500, 0, 3000);
	a.mouse_speed = number("mouse-keys-speed", 12, 2, 60);
	const bool zoom = flag("zoom");
	char v[16];
	double factor = pl_setting("zoom-factor", v, sizeof(v)) ? atof(v) : 2.0;
	factor = factor < 1.5 ? 1.5 : factor > 16 ? 16 : factor;
	if (zoom != a.zoom_on || factor != a.zoom) {
		a.zoom_on = zoom;
		a.zoom = factor;
		struct plat_output *output;
		wl_list_for_each(output, &server->outputs, link) {
			wlr_output_schedule_frame(output->wlr_output);
		}
	}
}

void access_init(struct plat_server *server) {
	a.server = server;
	a.zoom = 2.0;
	struct wl_event_loop *loop = wl_display_get_event_loop(server->display);
	a.slow_timer = wl_event_loop_add_timer(loop, slow_accept, NULL);
	a.mouse_timer = wl_event_loop_add_timer(loop, mouse_tick, NULL);
	access_apply(server);
}
