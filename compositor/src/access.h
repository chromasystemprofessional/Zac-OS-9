#ifndef ZACOS9_ACCESS_H
#define ZACOS9_ACCESS_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-protocol.h>
#include <xkbcommon/xkbcommon.h>

struct plat_server;
struct plat_keyboard;

/*
 * Easy Access and Zoom (Mac OS 9's Easy Access and CloseView, Missing Manual
 * pp. 237-239), set in the Accessibility panel (desktop.conf):
 *   Sticky Keys ("sticky-keys"): modifiers pressed one at a time apply to
 *     the next key; pressed twice they lock until pressed again. Shift
 *     pressed five times in a row turns it on or off.
 *   Slow Keys ("slow-keys", "slow-keys-delay" ms): a key counts only once
 *     held that long.
 *   Mouse Keys ("mouse-keys", "mouse-keys-delay" ms, "mouse-keys-speed"):
 *     the keypad's 1-9 move the pointer (held, faster after the delay),
 *     5 clicks, 0 holds the button down and . lets it go. ⌘-Shift-Num Lock
 *     turns it on, Num Lock alone off.
 *   Zoom ("zoom", "zoom-factor" 2..16): the screen magnified about the
 *     pointer; ⌘-Option-K turns it on or off, ⌘-Option-Up/Down changes it.
 */

/* Sticky Keys' modifiers, as bits. */
enum {
	STICKY_SHIFT = 1, STICKY_CONTROL = 2, STICKY_OPTION = 4, STICKY_COMMAND = 8,
};

/* The Sticky Keys state machine, apart from any keyboard (for tests). */
struct sticky {
	unsigned latched, locked; /* applying to the next key; until pressed again */
	unsigned pending;         /* a modifier down with no other key yet */
	bool other;               /* another key went down while it was */
};
/* The Sticky Keys bit for a keysym, or 0. */
unsigned sticky_mod_for(xkb_keysym_t sym);
/* A modifier's press or release (mod != 0), or another key's (mod == 0).
 * A modifier pressed and released alone latches; again, locks; again,
 * unlocks. */
void sticky_event(struct sticky *s, unsigned mod, bool pressed);
/* Another key was released: the latched modifiers have been used. */
void sticky_key_done(struct sticky *s);

/* The zoom factors the shortcuts step through. */
double access_zoom_step(double factor, int direction);

void access_init(struct plat_server *server);
/* The settings changed. */
void access_apply(struct plat_server *server);
/* A key, before anything else: true if Easy Access took it (Mouse Keys,
 * or a Slow Keys key it is still holding back). */
bool access_key(struct plat_keyboard *kb, uint32_t keycode, enum wl_keyboard_key_state state,
	uint32_t time_msec);
/* A key went on to the compositor or a client (Sticky Keys' bookkeeping). */
void access_key_delivered(struct plat_keyboard *kb, uint32_t keycode, enum wl_keyboard_key_state state);
/* Zoom's shortcuts; true if taken. */
bool access_keybinding(struct plat_server *server, uint32_t mods, xkb_keysym_t sym);
/* The screen's magnification: 1 when Zoom is off. */
double access_zoom_factor(void);
/* What Sticky Keys holds, for the menu bar's marker. */
void access_sticky_state(unsigned *latched, unsigned *locked);
bool access_sticky_on(void);
struct wlr_keyboard_modifiers;
/* Sticky Keys' modifiers added to a keyboard's, for clients. */
void access_merge_modifiers(struct wlr_keyboard_modifiers *m);
/* Sticky Keys' modifiers as WLR_MODIFIER_* bits, for the compositor's own
 * shortcuts and clicks. */
uint32_t access_held_modifiers(void);
/* A click used the latched modifiers. */
void access_click_done(void);

#endif
