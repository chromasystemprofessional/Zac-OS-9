#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

#include "platinum-shell-v1-client-protocol.h"

static struct platinum_shell_v1 *shell;

static void global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version) {
	if (strcmp(interface, platinum_shell_v1_interface.name) == 0) {
		shell = wl_registry_bind(registry, name, &platinum_shell_v1_interface,
			version < 16 ? version : 16);
	}
}

static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {
}

static const struct wl_registry_listener registry_listener = {
	.global = global,
	.global_remove = global_remove,
};

int main(int argc, char **argv) {
	if (argc != 2 || !*argv[1]) {
		fprintf(stderr, "usage: close_app_client APP_ID\n");
		return 2;
	}
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "could not connect to Wayland display\n");
		return 1;
	}
	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	if (wl_display_roundtrip(display) < 0 || !shell ||
			wl_proxy_get_version((struct wl_proxy *)shell) <
				PLATINUM_SHELL_V1_CLOSE_APP_WINDOW_SINCE_VERSION) {
		fprintf(stderr, "platinum-shell close-app-window request is unavailable\n");
		wl_registry_destroy(registry);
		wl_display_disconnect(display);
		return 1;
	}
	platinum_shell_v1_close_app_window(shell, argv[1]);
	const int status = wl_display_roundtrip(display) < 0 ? 1 : 0;
	platinum_shell_v1_destroy(shell);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
	return status;
}
