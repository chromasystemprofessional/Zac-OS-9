/*
 * vptr: scripted pointer and keyboard for UI tests, via wlr-virtual-pointer
 * and virtual-keyboard. Unlike wlrctl it can hold the button down, so it
 * can drag, and it types with a standard US keymap and real Shift presses
 * (so emulators and X11 apps see ordinary key events).
 *
 *   vptr home                 pointer to the top-left corner
 *   vptr move DX DY           relative motion (several small steps)
 *   vptr down | up | click    left button
 *   vptr type TEXT            type printable ASCII
 *   vptr key NAME             Return, BackSpace, Tab, Escape, Delete,
 *                             Up, Down, Left, Right, or one character
 *   vptr cmd C                ⌘C (the Super key, as platinum-wm maps it)
 *   vptr wait MS              pause
 * Commands chain: vptr home move 100 50 down move 40 0 up
 */
#define _GNU_SOURCE
#include <linux/input-event-codes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

static struct zwlr_virtual_pointer_manager_v1 *manager;
static struct zwp_virtual_keyboard_manager_v1 *kbd_manager;
static struct wl_seat *seat;

static void global(void *data, struct wl_registry *reg, uint32_t name,
		const char *iface, uint32_t version) {
	if (strcmp(iface, zwlr_virtual_pointer_manager_v1_interface.name) == 0) {
		manager = wl_registry_bind(reg, name, &zwlr_virtual_pointer_manager_v1_interface, 1);
	} else if (strcmp(iface, zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
		kbd_manager = wl_registry_bind(reg, name,
			&zwp_virtual_keyboard_manager_v1_interface, 1);
	} else if (strcmp(iface, wl_seat_interface.name) == 0 && !seat) {
		seat = wl_registry_bind(reg, name, &wl_seat_interface, 1);
	}
}

static void global_remove(void *data, struct wl_registry *reg, uint32_t name) {
}

static const struct wl_registry_listener registry_listener = { global, global_remove };

static uint32_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void sleep_ms(int ms) {
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
	nanosleep(&ts, NULL);
}

/* ---- keyboard ------------------------------------------------------------ */

static struct wl_display *display;
static struct zwp_virtual_keyboard_v1 *kbd;

static struct zwp_virtual_keyboard_v1 *keyboard(void) {
	if (kbd || !kbd_manager) {
		return kbd;
	}
	struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_rule_names names = { .layout = "us" };
	struct xkb_keymap *map = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
	char *text = xkb_keymap_get_as_string(map, XKB_KEYMAP_FORMAT_TEXT_V1);
	size_t size = strlen(text) + 1;
	int fd = memfd_create("vptr-keymap", MFD_CLOEXEC);
	if (fd < 0 || write(fd, text, size) != (ssize_t)size) {
		return NULL;
	}
	kbd = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(kbd_manager, seat);
	zwp_virtual_keyboard_v1_keymap(kbd, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, size);
	close(fd);
	free(text);
	xkb_keymap_unref(map);
	xkb_context_unref(ctx);
	wl_display_roundtrip(display);
	return kbd;
}

static void key_event(uint32_t code, bool down) {
	zwp_virtual_keyboard_v1_key(keyboard(), now_ms(), code,
		down ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
	wl_display_flush(display);
	sleep_ms(20);
}

/* US layout: the key for an ASCII character, and whether it needs Shift. */
static bool char_key(char ch, uint32_t *code, bool *shift) {
	static const char *const rows[][2] = {
		{ "1234567890-=", "!@#$%^&*()_+" },
		{ "qwertyuiop[]", "QWERTYUIOP{}" },
		{ "asdfghjkl;'`", "ASDFGHJKL:\"~" },
		{ "\\zxcvbnm,./", "|ZXCVBNM<>?" },
	};
	static const uint32_t first[] = { KEY_1, KEY_Q, KEY_A, KEY_BACKSLASH };
	if (ch == ' ') {
		*code = KEY_SPACE;
		*shift = false;
		return true;
	}
	for (int r = 0; r < 4; r++) {
		for (int s = 0; s < 2; s++) {
			const char *p = strchr(rows[r][s], ch);
			if (!p || !ch) {
				continue;
			}
			int i = (int)(p - rows[r][s]);
			*shift = s == 1;
			if (r == 3) {
				/* Backslash sits apart from the z row. */
				*code = i == 0 ? KEY_BACKSLASH : KEY_Z + (i - 1);
			} else if (r == 2 && i == 11) {
				*code = KEY_GRAVE;
			} else {
				*code = first[r] + (uint32_t)i;
			}
			return true;
		}
	}
	return false;
}

static void tap(uint32_t code, bool shift) {
	if (shift) {
		key_event(KEY_LEFTSHIFT, true);
	}
	key_event(code, true);
	key_event(code, false);
	if (shift) {
		key_event(KEY_LEFTSHIFT, false);
	}
}

static bool named_key(const char *name, uint32_t *code, bool *shift) {
	static const struct { const char *name; uint32_t code; } keys[] = {
		{ "Return", KEY_ENTER }, { "BackSpace", KEY_BACKSPACE }, { "Tab", KEY_TAB },
		{ "Escape", KEY_ESC }, { "Delete", KEY_DELETE }, { "Up", KEY_UP },
		{ "Down", KEY_DOWN }, { "Left", KEY_LEFT }, { "Right", KEY_RIGHT },
	};
	for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		if (strcmp(name, keys[i].name) == 0) {
			*code = keys[i].code;
			*shift = false;
			return true;
		}
	}
	return strlen(name) == 1 && char_key(name[0], code, shift);
}

int main(int argc, char *argv[]) {
	display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "vptr: no Wayland display\n");
		return 1;
	}
	struct wl_registry *reg = wl_display_get_registry(display);
	wl_registry_add_listener(reg, &registry_listener, NULL);
	wl_display_roundtrip(display);
	if (!manager) {
		fprintf(stderr, "vptr: compositor lacks wlr-virtual-pointer\n");
		return 1;
	}
	struct zwlr_virtual_pointer_v1 *ptr =
		zwlr_virtual_pointer_manager_v1_create_virtual_pointer(manager, seat);

	for (int i = 1; i < argc; i++) {
		const char *cmd = argv[i];
		if (strcmp(cmd, "home") == 0) {
			zwlr_virtual_pointer_v1_motion(ptr, now_ms(),
				wl_fixed_from_int(-100000), wl_fixed_from_int(-100000));
			zwlr_virtual_pointer_v1_frame(ptr);
		} else if (strcmp(cmd, "move") == 0 && i + 2 < argc) {
			int dx = atoi(argv[++i]), dy = atoi(argv[++i]);
			/* Several steps, so drags see intermediate positions. */
			const int steps = 8;
			for (int s = 1; s <= steps; s++) {
				int x = dx * s / steps - dx * (s - 1) / steps;
				int y = dy * s / steps - dy * (s - 1) / steps;
				zwlr_virtual_pointer_v1_motion(ptr, now_ms(),
					wl_fixed_from_int(x), wl_fixed_from_int(y));
				zwlr_virtual_pointer_v1_frame(ptr);
				wl_display_flush(display);
				sleep_ms(15);
			}
		} else if (strcmp(cmd, "down") == 0 || strcmp(cmd, "up") == 0) {
			zwlr_virtual_pointer_v1_button(ptr, now_ms(), BTN_LEFT,
				cmd[0] == 'd' ? WL_POINTER_BUTTON_STATE_PRESSED
				: WL_POINTER_BUTTON_STATE_RELEASED);
			zwlr_virtual_pointer_v1_frame(ptr);
		} else if (strcmp(cmd, "click") == 0) {
			zwlr_virtual_pointer_v1_button(ptr, now_ms(), BTN_LEFT,
				WL_POINTER_BUTTON_STATE_PRESSED);
			zwlr_virtual_pointer_v1_frame(ptr);
			zwlr_virtual_pointer_v1_button(ptr, now_ms(), BTN_LEFT,
				WL_POINTER_BUTTON_STATE_RELEASED);
			zwlr_virtual_pointer_v1_frame(ptr);
		} else if (strcmp(cmd, "type") == 0 && i + 1 < argc && keyboard()) {
			for (const char *p = argv[++i]; *p; p++) {
				uint32_t code;
				bool shift;
				if (char_key(*p, &code, &shift)) {
					tap(code, shift);
				}
			}
		} else if ((strcmp(cmd, "key") == 0 || strcmp(cmd, "cmd") == 0) && i + 1 < argc &&
				keyboard()) {
			uint32_t code;
			bool shift;
			if (!named_key(argv[++i], &code, &shift)) {
				fprintf(stderr, "vptr: unknown key '%s'\n", argv[i]);
				return 2;
			}
			if (cmd[0] == 'c') {
				key_event(KEY_LEFTMETA, true);
			}
			tap(code, shift);
			if (cmd[0] == 'c') {
				key_event(KEY_LEFTMETA, false);
			}
		} else if (strcmp(cmd, "wait") == 0 && i + 1 < argc) {
			wl_display_flush(display);
			sleep_ms(atoi(argv[++i]));
		} else {
			fprintf(stderr, "vptr: bad command '%s'\n", cmd);
			return 2;
		}
		wl_display_flush(display);
	}
	wl_display_roundtrip(display);
	if (kbd) {
		zwp_virtual_keyboard_v1_destroy(kbd);
	}
	zwlr_virtual_pointer_v1_destroy(ptr);
	wl_display_roundtrip(display);
	return 0;
}
