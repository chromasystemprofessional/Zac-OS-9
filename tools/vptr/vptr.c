/*
 * vptr: scripted pointer for UI tests, via wlr-virtual-pointer.
 * Unlike wlrctl it can hold the button down, so it can drag.
 *
 *   vptr home                 pointer to the top-left corner
 *   vptr move DX DY           relative motion (several small steps)
 *   vptr down | up | click    left button
 *   vptr wait MS              pause
 * Commands chain: vptr home move 100 50 down move 40 0 up
 */
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

static struct zwlr_virtual_pointer_manager_v1 *manager;
static struct wl_seat *seat;

static void global(void *data, struct wl_registry *reg, uint32_t name,
		const char *iface, uint32_t version) {
	if (strcmp(iface, zwlr_virtual_pointer_manager_v1_interface.name) == 0) {
		manager = wl_registry_bind(reg, name, &zwlr_virtual_pointer_manager_v1_interface, 1);
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

int main(int argc, char *argv[]) {
	struct wl_display *display = wl_display_connect(NULL);
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
	zwlr_virtual_pointer_v1_destroy(ptr);
	wl_display_roundtrip(display);
	return 0;
}
