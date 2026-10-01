#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/util/log.h>

#include "server.h"

static void usage(const char *argv0) {
	printf("Usage: %s [-s startup-command]\n", argv0);
}

int main(int argc, char *argv[]) {
	wlr_log_init(WLR_DEBUG, NULL);
	char *startup_cmd = NULL;

	int c;
	while ((c = getopt(argc, argv, "s:h")) != -1) {
		switch (c) {
		case 's':
			startup_cmd = optarg;
			break;
		default:
			usage(argv[0]);
			return 0;
		}
	}
	if (optind < argc) {
		usage(argv[0]);
		return 0;
	}

	struct plat_server server = {0};
	server.display = wl_display_create();

	server.backend = wlr_backend_autocreate(
			wl_display_get_event_loop(server.display), NULL);
	if (!server.backend) {
		wlr_log(WLR_ERROR, "failed to create wlr_backend");
		return 1;
	}

	server.renderer = wlr_renderer_autocreate(server.backend);
	if (!server.renderer) {
		wlr_log(WLR_ERROR, "failed to create wlr_renderer");
		return 1;
	}
	wlr_renderer_init_wl_display(server.renderer, server.display);

	server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
	if (!server.allocator) {
		wlr_log(WLR_ERROR, "failed to create wlr_allocator");
		return 1;
	}

	wlr_compositor_create(server.display, 5, server.renderer);
	wlr_subcompositor_create(server.display);
	wlr_data_device_manager_create(server.display);

	server.output_layout = wlr_output_layout_create(server.display);
	server.scene = wlr_scene_create();
	server.scene_layout = wlr_scene_attach_output_layout(
			server.scene, server.output_layout);

	/* Bottom layer: the desktop. Mac OS 8/9 uses a pattern here; until we draw
	 * our own pattern asset, a flat Platinum-era blue-violet stands in. */
	const float desk[4] = {
		0x66 / 255.0f, 0x66 / 255.0f, 0xCC / 255.0f, 1.0f,
	};
	server.desktop = wlr_scene_rect_create(&server.scene->tree, 16384, 16384, desk);
	server.view_layer = wlr_scene_tree_create(&server.scene->tree);

	output_init(&server);
	view_init(&server);
	input_init(&server);

	const char *socket = wl_display_add_socket_auto(server.display);
	if (!socket) {
		wlr_backend_destroy(server.backend);
		return 1;
	}

	if (!wlr_backend_start(server.backend)) {
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.display);
		return 1;
	}

	setenv("WAYLAND_DISPLAY", socket, true);
	if (startup_cmd && fork() == 0) {
		execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
		_exit(127);
	}

	wlr_log(WLR_INFO, "platinum-wm running on WAYLAND_DISPLAY=%s", socket);
	wl_display_run(server.display);

	wl_display_destroy_clients(server.display);
	wlr_scene_node_destroy(&server.scene->tree.node);
	wlr_xcursor_manager_destroy(server.cursor_mgr);
	wlr_cursor_destroy(server.cursor);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wlr_backend_destroy(server.backend);
	wl_display_destroy(server.display);
	return 0;
}
