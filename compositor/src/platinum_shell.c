/*
 * platinum-shell-v1: window style hints from Platinum's own programs.
 */
#include <stdlib.h>
#include <wlr/types/wlr_compositor.h>

#include "platinum-shell-v1-protocol.h"
#include "server.h"

struct style_hint {
	struct wl_list link;
	struct wlr_surface *surface;
	enum decor_style style;
	struct wl_listener surface_destroy;
};

static struct wl_list hints; /* style_hint.link */
static struct plat_server *the_server;

int platinum_shell_style_for(struct wlr_surface *surface) {
	struct style_hint *h;
	wl_list_for_each(h, &hints, link) {
		if (h->surface == surface) {
			return (int)h->style;
		}
	}
	return -1;
}

static void hint_surface_destroy(struct wl_listener *listener, void *data) {
	struct style_hint *h = wl_container_of(listener, h, surface_destroy);
	wl_list_remove(&h->surface_destroy.link);
	wl_list_remove(&h->link);
	free(h);
}

static void handle_set_window_style(struct wl_client *client, struct wl_resource *resource,
		struct wl_resource *surface_resource, uint32_t style) {
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);
	enum decor_style s = style == PLATINUM_SHELL_V1_STYLE_MOVABLE_MODAL
		? DECOR_STYLE_MOVABLE_MODAL : DECOR_STYLE_DOCUMENT;
	struct style_hint *h;
	bool found = false;
	wl_list_for_each(h, &hints, link) {
		if (h->surface == surface) {
			h->style = s;
			found = true;
		}
	}
	if (!found) {
		h = calloc(1, sizeof(*h));
		h->surface = surface;
		h->style = s;
		h->surface_destroy.notify = hint_surface_destroy;
		wl_signal_add(&surface->events.destroy, &h->surface_destroy);
		wl_list_insert(&hints, &h->link);
	}
	/* Already on screen: reframe now. */
	struct plat_view *view;
	wl_list_for_each(view, &the_server->views, link) {
		if (view->impl->get_surface(view) == surface) {
			view_update_frame(view);
		}
	}
}

static void handle_destroy(struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct platinum_shell_v1_interface shell_impl = {
	.set_window_style = handle_set_window_style,
	.destroy = handle_destroy,
};

static void bind_shell(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
	struct wl_resource *resource =
		wl_resource_create(client, &platinum_shell_v1_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &shell_impl, NULL, NULL);
}

void platinum_shell_init(struct plat_server *server) {
	the_server = server;
	wl_list_init(&hints);
	wl_global_create(server->display, &platinum_shell_v1_interface, 1, NULL, bind_shell);
}
