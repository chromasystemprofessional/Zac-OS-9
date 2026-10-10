#include <string.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/log.h>

#include "server.h"
#include "xdg-output-unstable-v1-protocol.h"

/* Outputs as Xwayland sees them: in pixels, not layout units. Xwayland makes
 * its root window (and keeps the X11 pointer inside it) from xdg-output's
 * logical sizes, and a HiDPI X11 window lives in pixel coordinates
 * (xwayland.c), so on a 2x screen the root must be twice the layout's size.
 * Xwayland gets this xdg-output manager instead of wlroots' one; every
 * other client gets wlroots' one only. All outputs share one integer scale. */

#define XDG_OUTPUT_VERSION 3

/* There is one compositor; resources lead back to their output only. */
static struct plat_server *xw_server;

static void send_details(struct wl_resource *resource, bool done) {
	struct wl_resource *output_resource = wl_resource_get_user_data(resource);
	struct wlr_output *output = output_resource ? wlr_output_from_resource(output_resource) : NULL;
	struct plat_server *server = xw_server;
	if (!output) {
		return;
	}
	struct wlr_box box;
	wlr_output_layout_get_box(server->output_layout, output, &box);
	if (wlr_box_empty(&box)) {
		return;
	}
	int scale = output->scale > 1 ? (int)output->scale : 1;
	zxdg_output_v1_send_logical_position(resource, box.x * scale, box.y * scale);
	zxdg_output_v1_send_logical_size(resource, box.width * scale, box.height * scale);
	if (!done) {
		return;
	}
	if (wl_resource_get_version(resource) < 3) {
		zxdg_output_v1_send_done(resource);
	} else if (wl_resource_get_version(output_resource) >= WL_OUTPUT_DONE_SINCE_VERSION) {
		wl_output_send_done(output_resource);
	}
}

static void xdg_output_destroy(struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct zxdg_output_v1_interface xdg_output_impl = {
	.destroy = xdg_output_destroy,
};

static void xdg_output_resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void manager_destroy(struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void manager_get_xdg_output(struct wl_client *client, struct wl_resource *resource,
		uint32_t id, struct wl_resource *output_resource) {
	struct plat_server *server = wl_resource_get_user_data(resource);
	struct wl_resource *xdg_output = wl_resource_create(client, &zxdg_output_v1_interface,
		wl_resource_get_version(resource), id);
	if (!xdg_output) {
		wl_client_post_no_memory(client);
		return;
	}
	/* The wl_output resource outlives its output (it goes inert), so it is
	 * looked up again on every update. */
	wl_resource_set_implementation(xdg_output, &xdg_output_impl, output_resource,
		xdg_output_resource_destroy);
	wl_list_insert(&server->xwayland_xdg_outputs, wl_resource_get_link(xdg_output));
	struct wlr_output *output = wlr_output_from_resource(output_resource);
	if (output && wl_resource_get_version(xdg_output) >= ZXDG_OUTPUT_V1_NAME_SINCE_VERSION) {
		zxdg_output_v1_send_name(xdg_output, output->name);
	}
	send_details(xdg_output, true);
}

static const struct zxdg_output_manager_v1_interface manager_impl = {
	.destroy = manager_destroy,
	.get_xdg_output = manager_get_xdg_output,
};

static void manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
	struct wl_resource *resource =
		wl_resource_create(client, &zxdg_output_manager_v1_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &manager_impl, data, NULL);
}

static void layout_change(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, xwayland_layout_change);
	struct wl_resource *resource;
	wl_resource_for_each(resource, &server->xwayland_xdg_outputs) {
		send_details(resource, true);
	}
}

static bool is_xwayland(struct plat_server *server, const struct wl_client *client) {
	return server->xwayland && server->xwayland->server &&
		server->xwayland->server->client == client;
}

static bool global_filter(const struct wl_client *client, const struct wl_global *global,
		void *data) {
	struct plat_server *server = data;
	if (global == server->xwayland_xdg_output) {
		return is_xwayland(server, client);
	}
	/* wlroots' manager: same interface name, its own interface struct. */
	if (strcmp(wl_global_get_interface(global)->name,
			zxdg_output_manager_v1_interface.name) == 0) {
		return !is_xwayland(server, client);
	}
	return true;
}

void xwayland_output_init(struct plat_server *server) {
	xw_server = server;
	wl_list_init(&server->xwayland_xdg_outputs);
	server->xwayland_xdg_output = wl_global_create(server->display,
		&zxdg_output_manager_v1_interface, XDG_OUTPUT_VERSION, server, manager_bind);
	if (!server->xwayland_xdg_output) {
		wlr_log(WLR_ERROR, "Could not make Xwayland's xdg-output manager");
		return;
	}
	server->xwayland_layout_change.notify = layout_change;
	wl_signal_add(&server->output_layout->events.change, &server->xwayland_layout_change);
	wl_display_set_global_filter(server->display, global_filter, server);
}
