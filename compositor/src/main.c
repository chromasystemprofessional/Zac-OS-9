#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/util/log.h>

#include "startup.h"
#include "server.h"
#include "snapshot.h"

static void usage(const char *argv0) {
	printf("Usage: %s [-d] [-S scale] [-s startup-command]\n"
		"  -d  verbose (debug) logging\n"
		"  -S  integer output scale, 1-4 (default: $ZACOS9_SCALE or 1)\n",
		argv0);
}

static void spawn(const char *command) {
	if (fork() == 0) {
		setsid();
		execl("/bin/sh", "/bin/sh", "-c", command, (void *)NULL);
		_exit(127);
	}
}

/* Start a shell component (menu bar, Finder): $env_var if set (empty
 * disables it), else next to our own binary (installed) or in the build
 * tree's shell/ directory, else from $PATH. */
static void spawn_component(const char *env_var, const char *program) {
	const char *env = getenv(env_var);
	if (env) {
		if (*env) {
			spawn(env);
		}
		return;
	}
	char exe[PATH_MAX];
	ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (len > 0) {
		exe[len] = '\0';
		char *slash = strrchr(exe, '/');
		if (slash) {
			*slash = '\0';
			const char *candidates[] = { "%s/%s", "%s/../shell/%s" };
			for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
				char path[PATH_MAX + 64];
				snprintf(path, sizeof(path), candidates[i], exe, program);
				if (access(path, X_OK) == 0) {
					char quoted[PATH_MAX + 80];
					snprintf(quoted, sizeof(quoted), "exec '%s'", path);
					spawn(quoted);
					return;
				}
			}
		}
	}
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "exec %s", program);
	spawn(cmd);
}

/* Our desktop entries (app names for the menu bar) live in ../share next
 * to this binary: build/share in a build tree, /usr/share when installed.
 * Put that first in XDG_DATA_DIRS for every client we start. */
static void share_our_data(void) {
	char exe[PATH_MAX];
	ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (len <= 0) {
		return;
	}
	exe[len] = '\0';
	char *slash = strrchr(exe, '/');
	if (!slash) {
		return;
	}
	*slash = '\0';
	const char *old = getenv("XDG_DATA_DIRS");
	char dirs[2 * PATH_MAX];
	snprintf(dirs, sizeof(dirs), "%s/../share:%s", exe,
		old && *old ? old : "/usr/local/share:/usr/share");
	setenv("XDG_DATA_DIRS", dirs, 1);
}

static int parse_scale(const char *s) {
	int scale = s ? atoi(s) : 1;
	return scale >= 1 && scale <= 4 ? scale : 1;
}

int main(int argc, char *argv[]) {
	enum wlr_log_importance log_level = WLR_ERROR;
	char *startup_cmd = NULL;
	int scale = parse_scale(getenv("ZACOS9_SCALE"));
	bool scale_explicit = getenv("ZACOS9_SCALE") != NULL;

	int c;
	while ((c = getopt(argc, argv, "dS:s:h")) != -1) {
		switch (c) {
		case 'd':
			log_level = WLR_DEBUG;
			break;
		case 'S':
			scale = parse_scale(optarg);
			scale_explicit = true;
			break;
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
	wlr_log_init(log_level, NULL);

	struct plat_server server = {0};
	server.output_scale = scale;
	server.scale_explicit = scale_explicit;
	server.display = wl_display_create();

	server.backend = wlr_backend_autocreate(
			wl_display_get_event_loop(server.display), &server.session);
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

	/* Version 6: clients follow wl_surface.preferred_buffer_scale, which we
	 * update when a panel changes the scale (prefs.c). */
	server.compositor = wlr_compositor_create(server.display, 6, server.renderer);
	wlr_subcompositor_create(server.display);
	wlr_viewporter_create(server.display);
	wlr_data_device_manager_create(server.display);
	/* wl-copy and wl-paste (Screen Snapshot puts its picture on the
	 * clipboard) set and read the clipboard without a window of their own. */
	wlr_data_control_manager_v1_create(server.display);
	/* Lets `grim` capture screenshots for visual checks. */
	wlr_screencopy_manager_v1_create(server.display);

	server.output_layout = wlr_output_layout_create(server.display);
	wlr_xdg_output_manager_v1_create(server.display, server.output_layout);
	server.scene = wlr_scene_create();
	server.scene_layout = wlr_scene_attach_output_layout(
			server.scene, server.output_layout);

	/* Bottom layer: the desktop. Mac OS 8/9 uses a pattern here; until we draw
	 * our own pattern asset, a flat Platinum-era blue-violet stands in. */
	const float desk[4] = {
		0x66 / 255.0f, 0x66 / 255.0f, 0xCC / 255.0f, 1.0f,
	};
	server.desktop = wlr_scene_rect_create(&server.scene->tree, 16384, 16384, desk);
	/* Screens may sit left of or above the first one: centre the rect on the origin. */
	wlr_scene_node_set_position(&server.desktop->node, -8192, -8192);
	struct wlr_scene_tree *root = &server.scene->tree;
	server.shell_layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND] = wlr_scene_tree_create(root);
	server.shell_layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM] = wlr_scene_tree_create(root);
	server.view_layer = wlr_scene_tree_create(root);
	server.shell_layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP] = wlr_scene_tree_create(root);
	server.fullscreen_layer = wlr_scene_tree_create(root);
	server.dialog_layer = wlr_scene_tree_create(root);
	server.unmanaged_layer = wlr_scene_tree_create(root);
	server.shell_layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY] = wlr_scene_tree_create(root);
	server.overlay_layer = wlr_scene_tree_create(root);
	outline_init(&server.outline, server.overlay_layer);

	/* GTK3 asks for decorations through the older KDE protocol. */
	wlr_server_decoration_manager_set_default_mode(
		wlr_server_decoration_manager_create(server.display),
		WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);

	wl_list_init(&server.views);
	server.foreign_toplevel_mgr = wlr_foreign_toplevel_manager_v1_create(server.display);
	layers_init(&server);
	gtk_shell_init(&server);
	platinum_shell_init(&server);
	output_init(&server);
	snapshot_init(&server);
	xdg_init(&server);
	input_init(&server);
	prefs_init(&server);
	xwayland_init(&server);

	const char *socket = wl_display_add_socket_auto(server.display);
	if (!socket) {
		wlr_backend_destroy(server.backend);
		return 1;
	}
	server.socket = socket;

	if (!wlr_backend_start(server.backend)) {
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.display);
		return 1;
	}

	setenv("WAYLAND_DISPLAY", socket, true);
	prefs_write_outputs(&server);
	share_our_data();
	startup_begin(&server);
	spawn_component("ZACOS9_MENUBAR", "zacos9-menubar");
	spawn_component("ZACOS9_FINDER", "zacos9-finder");
	spawn_component("ZACOS9_COLLAR", "zacos9-collar");
	/* On a live medium, open the installer automatically; on a freshly
	 * installed system (zacos9-install leaves the flag), the Setup Assistant. */
	{
		struct stat st_live;
		if (stat("/run/live", &st_live) == 0 && S_ISDIR(st_live.st_mode)) {
			spawn("exec zacos9-installer");
		} else if (access("/var/lib/zacos9/setup-pending", F_OK) == 0) {
			spawn("exec zacos9-setup");
		}
	}
	if (startup_cmd) {
		spawn(startup_cmd);
	}

	fprintf(stderr, "zacos9-wm running (WAYLAND_DISPLAY=%s). "
		"Quit with Ctrl+Alt+Backspace or by closing its window.\n", socket);
	wl_display_run(server.display);

	input_cancel_grab(&server);
	wl_display_destroy_clients(server.display);
	if (server.xwayland) {
		wlr_xwayland_destroy(server.xwayland);
	}
	wlr_scene_node_destroy(&server.scene->tree.node);
	wlr_xcursor_manager_destroy(server.cursor_mgr);
	wlr_cursor_destroy(server.cursor);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wlr_backend_destroy(server.backend);
	wl_display_destroy(server.display);
	return 0;
}
