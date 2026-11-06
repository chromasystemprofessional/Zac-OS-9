/* Types one key (evdev KEY_A, press and release) on a virtual
 * keyboard, so the compositor has a keyboard to deliver from. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "virtual-keyboard-unstable-v1-client-protocol.h"

static struct wl_seat *seat;
static struct zwp_virtual_keyboard_manager_v1 *vk_manager;

static void registry(void *d, struct wl_registry *r, uint32_t name,
		const char *iface, uint32_t version) {
	if (strcmp(iface, wl_seat_interface.name) == 0) {
		seat = wl_registry_bind(r, name, &wl_seat_interface, 5);
	} else if (strcmp(iface, zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
		vk_manager = wl_registry_bind(r, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
	}
}
static void registry_remove(void *d, struct wl_registry *r, uint32_t name) {}
static const struct wl_registry_listener reg_listener = {
	.global = registry, .global_remove = registry_remove,
};

int main(int argc, char **argv) {
	uint32_t keycode = argc > 1 ? (uint32_t)atoi(argv[1]) : 30;
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) { fprintf(stderr, "no display\n"); return 1; }
	struct wl_registry *reg = wl_display_get_registry(display);
	wl_registry_add_listener(reg, &reg_listener, NULL);
	wl_display_roundtrip(display);
	if (!vk_manager) { fprintf(stderr, "no virtual keyboard manager\n"); return 2; }

	struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_rule_names names = { .rules = "evdev", .model = "pc105",
		.layout = "us", .variant = NULL, .options = NULL };
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
	char *km_str = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
	size_t km_size = strlen(km_str) + 1;
	int fd = memfd_create("vkbd-test", MFD_CLOEXEC);
	ftruncate(fd, km_size);
	void *data = mmap(NULL, km_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	memcpy(data, km_str, km_size);
	munmap(data, km_size);

	struct zwp_virtual_keyboard_v1 *vkbd =
		zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(vk_manager, seat);
	zwp_virtual_keyboard_v1_keymap(vkbd, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, km_size);
	close(fd);
	wl_display_roundtrip(display);
	usleep(300 * 1000);
	zwp_virtual_keyboard_v1_key(vkbd, 1000, keycode, WL_KEYBOARD_KEY_STATE_PRESSED);
	wl_display_roundtrip(display);
	usleep(200 * 1000);
	zwp_virtual_keyboard_v1_key(vkbd, 1100, keycode, WL_KEYBOARD_KEY_STATE_RELEASED);
	wl_display_roundtrip(display);
	usleep(300 * 1000);
	printf("VKBD sent key A\n");
	fflush(stdout);
	zwp_virtual_keyboard_v1_destroy(vkbd);
	wl_display_disconnect(display);
	return 0;
}
