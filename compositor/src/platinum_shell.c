/*
 * platinum-shell-v1: window hints from Platinum's own programs (frame
 * style, remembered position) and window-position reports back to them.
 */
#include <stdlib.h>
#include <wlr/types/wlr_compositor.h>

#include "platinum-shell-v1-protocol.h"
#include "server.h"

struct surface_hint {
	struct wl_list link;
	struct wlr_surface *surface;
	int style;           /* enum decor_style, or -1 */
	bool has_position;
	int x, y;
	bool reported;       /* last reported position, to avoid repeats */
	int rx, ry;
	struct wl_listener surface_destroy;
};

static struct wl_list hints;     /* surface_hint.link */
static struct wl_list resources; /* bound platinum_shell_v1 resources */
static struct plat_server *the_server;

static struct surface_hint *find_hint(struct wlr_surface *surface) {
	struct surface_hint *h;
	wl_list_for_each(h, &hints, link) {
		if (h->surface == surface) {
			return h;
		}
	}
	return NULL;
}

static void hint_surface_destroy(struct wl_listener *listener, void *data) {
	struct surface_hint *h = wl_container_of(listener, h, surface_destroy);
	wl_list_remove(&h->surface_destroy.link);
	wl_list_remove(&h->link);
	free(h);
}

static struct surface_hint *get_hint(struct wlr_surface *surface) {
	struct surface_hint *h = find_hint(surface);
	if (!h) {
		h = calloc(1, sizeof(*h));
		h->surface = surface;
		h->style = -1;
		h->surface_destroy.notify = hint_surface_destroy;
		wl_signal_add(&surface->events.destroy, &h->surface_destroy);
		wl_list_insert(&hints, &h->link);
	}
	return h;
}

int platinum_shell_style_for(struct wlr_surface *surface) {
	struct surface_hint *h = find_hint(surface);
	return h ? h->style : -1;
}

bool platinum_shell_position_for(struct wlr_surface *surface, int *x, int *y) {
	struct surface_hint *h = find_hint(surface);
	if (!h || !h->has_position) {
		return false;
	}
	*x = h->x;
	*y = h->y;
	return true;
}

void platinum_shell_report_position(struct plat_view *view) {
	struct wlr_surface *surface = view->impl->get_surface(view);
	if (!surface || !surface->resource || !view->mapped) {
		return;
	}
	struct wl_client *client = wl_resource_get_client(surface->resource);
	struct wl_resource *shell = NULL, *r;
	wl_resource_for_each(r, &resources) {
		if (wl_resource_get_client(r) == client) {
			shell = r;
		}
	}
	if (!shell) {
		return; /* not one of ours */
	}
	struct wlr_box box = view_frame_box(view);
	struct surface_hint *h = get_hint(surface);
	if (h->reported && h->rx == box.x && h->ry == box.y) {
		return;
	}
	h->reported = true;
	h->rx = box.x;
	h->ry = box.y;
	platinum_shell_v1_send_window_position(shell, surface->resource, box.x, box.y);
}

static struct plat_view *mapped_view_for(struct wlr_surface *surface) {
	struct plat_view *view;
	wl_list_for_each(view, &the_server->views, link) {
		if (view->impl->get_surface(view) == surface) {
			return view;
		}
	}
	return NULL;
}

static void handle_set_window_style(struct wl_client *client, struct wl_resource *resource,
		struct wl_resource *surface_resource, uint32_t style) {
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);
	get_hint(surface)->style = style == PLATINUM_SHELL_V1_STYLE_MOVABLE_MODAL
		? DECOR_STYLE_MOVABLE_MODAL : DECOR_STYLE_DOCUMENT;
	/* Already on screen: reframe now. */
	struct plat_view *view = mapped_view_for(surface);
	if (view) {
		view_update_frame(view);
	}
}

static void handle_set_window_position(struct wl_client *client, struct wl_resource *resource,
		struct wl_resource *surface_resource, int32_t x, int32_t y) {
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);
	struct surface_hint *h = get_hint(surface);
	h->has_position = true;
	h->x = x;
	h->y = y;
	struct plat_view *view = mapped_view_for(surface);
	if (view) {
		view_move_to(view, x, y);
	}
}

static void handle_destroy(struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct platinum_shell_v1_interface shell_impl = {
	.set_window_style = handle_set_window_style,
	.set_window_position = handle_set_window_position,
	.destroy = handle_destroy,
};

static void resource_destroy(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void bind_shell(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
	struct wl_resource *resource =
		wl_resource_create(client, &platinum_shell_v1_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &shell_impl, NULL, resource_destroy);
	wl_list_insert(&resources, wl_resource_get_link(resource));
}

void platinum_shell_init(struct plat_server *server) {
	the_server = server;
	wl_list_init(&hints);
	wl_list_init(&resources);
	wl_global_create(server->display, &platinum_shell_v1_interface, 1, NULL, bind_shell);
}
