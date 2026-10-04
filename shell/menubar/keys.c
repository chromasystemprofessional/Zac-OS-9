/*
 * Edit-menu commands are delivered to the front app as ⌘ keystrokes through
 * a virtual keyboard: zacos9-wm makes ⌘X Ctrl+X for a Linux app, but leaves
 * it ⌘X for a terminal, where Ctrl+X means something else. By the time they
 * are sent, the menu overlay is gone and the app has keyboard focus again;
 * requests on one connection are handled in order.
 */
#define _GNU_SOURCE
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

#include "menubar.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"

static struct zwp_virtual_keyboard_v1 *vkbd;
static uint32_t command_mask;

void keys_init(struct zwp_virtual_keyboard_manager_v1 *mgr, struct wl_seat *seat) {
	struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = ctx ?
		xkb_keymap_new_from_names(ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS) : NULL;
	if (!keymap) {
		fprintf(stderr, "zacos9-menubar: no keymap; Edit commands disabled\n");
		xkb_context_unref(ctx);
		return;
	}
	command_mask = 1u << xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_LOGO);

	char *text = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
	size_t size = strlen(text) + 1;
	int fd = memfd_create("zacos9-keymap", MFD_CLOEXEC);
	if (fd >= 0 && ftruncate(fd, size) == 0) {
		void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (map != MAP_FAILED) {
			memcpy(map, text, size);
			munmap(map, size);
			vkbd = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(mgr, seat);
			zwp_virtual_keyboard_v1_keymap(vkbd, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, size);
		}
	}
	if (fd >= 0) {
		close(fd);
	}
	free(text);
	xkb_keymap_unref(keymap);
	xkb_context_unref(ctx);
}

bool keys_available(void) {
	return vkbd != NULL;
}

static uint32_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/* Evdev keycode for a ⌘ shortcut letter, or 0. */
static uint32_t keycode_for(char key) {
	switch (key) {
	case 'A': return KEY_A;
	case 'C': return KEY_C;
	case 'V': return KEY_V;
	case 'X': return KEY_X;
	case 'Z': return KEY_Z;
	default: return 0;
	}
}

static void tap(uint32_t keycode, uint32_t mods) {
	uint32_t t = now_ms();
	zwp_virtual_keyboard_v1_modifiers(vkbd, mods, 0, 0, 0);
	zwp_virtual_keyboard_v1_key(vkbd, t, keycode, WL_KEYBOARD_KEY_STATE_PRESSED);
	zwp_virtual_keyboard_v1_key(vkbd, t + 1, keycode, WL_KEYBOARD_KEY_STATE_RELEASED);
	zwp_virtual_keyboard_v1_modifiers(vkbd, 0, 0, 0, 0);
}

void keys_send_command(char key) {
	uint32_t code = keycode_for(key);
	if (vkbd && code) {
		tap(code, command_mask);
	}
}

void keys_send_clear(void) {
	if (vkbd) {
		tap(KEY_DELETE, 0);
	}
}
