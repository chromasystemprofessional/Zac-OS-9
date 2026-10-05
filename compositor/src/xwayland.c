#include <stdlib.h>
#include <xcb/xcb_icccm.h>
#include <wlr/util/log.h>

#include "server.h"

/* X11 override-redirect windows (menus, tooltips, drag icons): no frame,
 * placed exactly where the client asks. */
struct plat_unmanaged {
	struct plat_server *server;
	struct wlr_xwayland_surface *xsurface;
	struct wlr_scene_tree *tree;
	struct wl_listener associate;
	struct wl_listener dissociate;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener set_geometry;
	struct wl_listener request_configure;
	struct wl_listener destroy;
};

/* ---- plat_view_impl ---------------------------------------------------- */

static struct wlr_surface *xw_get_surface(struct plat_view *view) {
	return view->xsurface->surface;
}

static void xw_get_geometry(struct plat_view *view, struct wlr_box *geo) {
	*geo = (struct wlr_box){
		.width = view->xsurface->width,
		.height = view->xsurface->height,
	};
}

static void xw_set_activated(struct plat_view *view, bool activated) {
	wlr_xwayland_surface_activate(view->xsurface, activated);
	if (activated) {
		wlr_xwayland_surface_restack(view->xsurface, NULL, XCB_STACK_MODE_ABOVE);
	}
}

static void xw_set_size(struct plat_view *view, int width, int height) {
	int x, y;
	view_content_pos(view, &x, &y);
	wlr_xwayland_surface_configure(view->xsurface, x, y, width, height);
}

static void xw_set_fullscreen(struct plat_view *view, bool fullscreen) {
	wlr_xwayland_surface_set_fullscreen(view->xsurface, fullscreen);
}

/* X11 clients position their own popups from their window position. */
static void xw_moved(struct plat_view *view) {
	xw_set_size(view, view->xsurface->width, view->xsurface->height);
}

static void xw_close(struct plat_view *view) {
	wlr_xwayland_surface_close(view->xsurface);
}

static void xw_get_size_limits(struct plat_view *view,
		int *min_w, int *min_h, int *max_w, int *max_h) {
	*min_w = *min_h = *max_w = *max_h = 0;
	xcb_size_hints_t *hints = view->xsurface->size_hints;
	if (!hints) {
		return;
	}
	if (hints->flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
		*min_w = hints->min_width;
		*min_h = hints->min_height;
	}
	if (hints->flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE) {
		*max_w = hints->max_width;
		*max_h = hints->max_height;
	}
}

static bool xw_has_parent(struct plat_view *view) {
	return view->xsurface->parent != NULL;
}

static const struct plat_view_impl xwayland_impl = {
	.get_surface = xw_get_surface,
	.get_geometry = xw_get_geometry,
	.set_activated = xw_set_activated,
	.set_size = xw_set_size,
	.set_fullscreen = xw_set_fullscreen,
	.moved = xw_moved,
	.close = xw_close,
	.get_size_limits = xw_get_size_limits,
	.has_parent = xw_has_parent,
};

/* ---- managed windows --------------------------------------------------- */

static void handle_map(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, map);
	view_set_title(view, view->xsurface->title);
	view_set_app_id(view, view->xsurface->class);
	view_place_new(view);
	view_handle_map(view);
	if (view->xsurface->fullscreen) {
		view_set_fullscreen(view, true, NULL);
	}
}

static void handle_unmap(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, unmap);
	view_handle_unmap(view);
}

static void handle_commit(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, commit);
	if (view->mapped) {
		view_update_frame(view);
	}
}

static void handle_associate(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, associate);
	struct wlr_surface *surface = view->xsurface->surface;
	view_attach_surface_tree(view,
		wlr_scene_subsurface_tree_create(view->scene_tree, surface));

	view->map.notify = handle_map;
	wl_signal_add(&surface->events.map, &view->map);
	view->unmap.notify = handle_unmap;
	wl_signal_add(&surface->events.unmap, &view->unmap);
	view->commit.notify = handle_commit;
	wl_signal_add(&surface->events.commit, &view->commit);
}

static void handle_dissociate(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, dissociate);
	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
	wl_list_remove(&view->commit.link);
	wl_list_init(&view->map.link);
	wl_list_init(&view->unmap.link);
	wl_list_init(&view->commit.link);
	if (view->surface_tree) {
		wlr_scene_node_destroy(&view->surface_tree->node);
		view->surface_tree = NULL;
	}
}

static void handle_request_configure(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, request_configure);
	struct wlr_xwayland_surface_configure_event *event = data;
	if (!view->mapped) {
		/* Not placed yet: grant the request; we position it on map. */
		wlr_xwayland_surface_configure(view->xsurface,
			event->x, event->y, event->width, event->height);
		return;
	}
	if (view->fullscreen) {
		struct wlr_box box = view_frame_box(view);
		xw_set_size(view, box.width, box.height);
		return;
	}
	/* Windows stay where the user put them; only the size is negotiable. */
	xw_set_size(view, event->width, event->height);
}

static void handle_request_activate(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, request_activate);
	if (view->mapped) {
		view_focus(view);
	}
}

static void handle_request_move(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, request_move);
	if (view->mapped) {
		input_begin_grab(view->server, view, PLAT_CURSOR_MOVE, 0, DECOR_PART_DRAG);
	}
}

static void handle_request_resize(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, request_resize);
	struct wlr_xwayland_resize_event *event = data;
	if (view->mapped) {
		input_begin_grab(view->server, view, PLAT_CURSOR_RESIZE, event->edges,
			DECOR_PART_GROW);
	}
}

static void handle_request_maximize(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, request_maximize);
	if (view->mapped) {
		view_toggle_zoom(view);
	}
}

static void handle_request_fullscreen(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, request_fullscreen);
	if (view->mapped) {
		view_set_fullscreen(view, view->xsurface->fullscreen, NULL);
	}
}

static void handle_set_title(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, set_title);
	view_set_title(view, view->xsurface->title);
}

/* The X server reports a resize when it has applied it, which can be after
 * the client's last commit: the frame is fitted to the window's size here
 * as well as on commits, or it would stay at the old one (until something
 * else - folding the window, say - made it redraw). */
static void handle_set_geometry(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, set_geometry);
	if (view->mapped) {
		view_update_frame(view);
	}
}

/* WM_CLASS is X11's closest thing to an app id. */
static void handle_set_class(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, set_app_id);
	view_set_app_id(view, view->xsurface->class);
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, destroy);
	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
	wl_list_remove(&view->commit.link);
	wl_list_remove(&view->associate.link);
	wl_list_remove(&view->dissociate.link);
	wl_list_remove(&view->request_configure.link);
	wl_list_remove(&view->request_activate.link);
	wl_list_remove(&view->request_move.link);
	wl_list_remove(&view->request_resize.link);
	wl_list_remove(&view->request_maximize.link);
	wl_list_remove(&view->request_fullscreen.link);
	wl_list_remove(&view->set_title.link);
	wl_list_remove(&view->set_geometry.link);
	wl_list_remove(&view->set_app_id.link);
	wl_list_remove(&view->destroy.link);
	view_handle_destroy(view);
}

static void new_managed(struct plat_server *server, struct wlr_xwayland_surface *xsurface) {
	struct plat_view *view = calloc(1, sizeof(*view));
	view->xsurface = xsurface;
	view_setup(view, server, PLAT_VIEW_XWAYLAND, &xwayland_impl);
	xsurface->data = view;

	/* Surface listeners are added on associate. */
	wl_list_init(&view->map.link);
	wl_list_init(&view->unmap.link);
	wl_list_init(&view->commit.link);

	view->associate.notify = handle_associate;
	wl_signal_add(&xsurface->events.associate, &view->associate);
	view->dissociate.notify = handle_dissociate;
	wl_signal_add(&xsurface->events.dissociate, &view->dissociate);
	view->set_geometry.notify = handle_set_geometry;
	wl_signal_add(&xsurface->events.set_geometry, &view->set_geometry);
	view->request_configure.notify = handle_request_configure;
	wl_signal_add(&xsurface->events.request_configure, &view->request_configure);
	view->request_activate.notify = handle_request_activate;
	wl_signal_add(&xsurface->events.request_activate, &view->request_activate);
	view->request_move.notify = handle_request_move;
	wl_signal_add(&xsurface->events.request_move, &view->request_move);
	view->request_resize.notify = handle_request_resize;
	wl_signal_add(&xsurface->events.request_resize, &view->request_resize);
	view->request_maximize.notify = handle_request_maximize;
	wl_signal_add(&xsurface->events.request_maximize, &view->request_maximize);
	view->request_fullscreen.notify = handle_request_fullscreen;
	wl_signal_add(&xsurface->events.request_fullscreen, &view->request_fullscreen);
	view->set_title.notify = handle_set_title;
	wl_signal_add(&xsurface->events.set_title, &view->set_title);
	view->set_app_id.notify = handle_set_class;
	wl_signal_add(&xsurface->events.set_class, &view->set_app_id);
	view->destroy.notify = handle_destroy;
	wl_signal_add(&xsurface->events.destroy, &view->destroy);
}

/* ---- unmanaged (override-redirect) windows ------------------------------- */

static void um_map(struct wl_listener *listener, void *data) {
	struct plat_unmanaged *um = wl_container_of(listener, um, map);
	wlr_scene_node_set_position(&um->tree->node, um->xsurface->x, um->xsurface->y);
	wlr_scene_node_set_enabled(&um->tree->node, true);
	wlr_scene_node_raise_to_top(&um->tree->node);
}

static void um_unmap(struct wl_listener *listener, void *data) {
	struct plat_unmanaged *um = wl_container_of(listener, um, unmap);
	wlr_scene_node_set_enabled(&um->tree->node, false);
}

static void um_set_geometry(struct wl_listener *listener, void *data) {
	struct plat_unmanaged *um = wl_container_of(listener, um, set_geometry);
	if (um->tree) {
		wlr_scene_node_set_position(&um->tree->node, um->xsurface->x, um->xsurface->y);
	}
}

static void um_request_configure(struct wl_listener *listener, void *data) {
	struct plat_unmanaged *um = wl_container_of(listener, um, request_configure);
	struct wlr_xwayland_surface_configure_event *event = data;
	wlr_xwayland_surface_configure(um->xsurface,
		event->x, event->y, event->width, event->height);
}

static void um_associate(struct wl_listener *listener, void *data) {
	struct plat_unmanaged *um = wl_container_of(listener, um, associate);
	struct wlr_surface *surface = um->xsurface->surface;
	um->tree = wlr_scene_subsurface_tree_create(um->server->unmanaged_layer, surface);
	wlr_scene_node_set_enabled(&um->tree->node, false);
	um->map.notify = um_map;
	wl_signal_add(&surface->events.map, &um->map);
	um->unmap.notify = um_unmap;
	wl_signal_add(&surface->events.unmap, &um->unmap);
}

static void um_dissociate(struct wl_listener *listener, void *data) {
	struct plat_unmanaged *um = wl_container_of(listener, um, dissociate);
	wl_list_remove(&um->map.link);
	wl_list_remove(&um->unmap.link);
	wl_list_init(&um->map.link);
	wl_list_init(&um->unmap.link);
	if (um->tree) {
		wlr_scene_node_destroy(&um->tree->node);
		um->tree = NULL;
	}
}

static void um_destroy(struct wl_listener *listener, void *data) {
	struct plat_unmanaged *um = wl_container_of(listener, um, destroy);
	wl_list_remove(&um->map.link);
	wl_list_remove(&um->unmap.link);
	wl_list_remove(&um->associate.link);
	wl_list_remove(&um->dissociate.link);
	wl_list_remove(&um->set_geometry.link);
	wl_list_remove(&um->request_configure.link);
	wl_list_remove(&um->destroy.link);
	if (um->tree) {
		wlr_scene_node_destroy(&um->tree->node);
	}
	free(um);
}

static void new_unmanaged(struct plat_server *server, struct wlr_xwayland_surface *xsurface) {
	struct plat_unmanaged *um = calloc(1, sizeof(*um));
	um->server = server;
	um->xsurface = xsurface;
	wl_list_init(&um->map.link);
	wl_list_init(&um->unmap.link);

	um->associate.notify = um_associate;
	wl_signal_add(&xsurface->events.associate, &um->associate);
	um->dissociate.notify = um_dissociate;
	wl_signal_add(&xsurface->events.dissociate, &um->dissociate);
	um->set_geometry.notify = um_set_geometry;
	wl_signal_add(&xsurface->events.set_geometry, &um->set_geometry);
	um->request_configure.notify = um_request_configure;
	wl_signal_add(&xsurface->events.request_configure, &um->request_configure);
	um->destroy.notify = um_destroy;
	wl_signal_add(&xsurface->events.destroy, &um->destroy);
}

/* ---- server ------------------------------------------------------------ */

static void server_new_xwayland_surface(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, new_xwayland_surface);
	struct wlr_xwayland_surface *xsurface = data;
	/* TODO: a window can switch override-redirect after creation. */
	if (xsurface->override_redirect) {
		new_unmanaged(server, xsurface);
	} else {
		new_managed(server, xsurface);
	}
}

static void xwayland_ready(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, xwayland_ready);
	wlr_xwayland_set_seat(server->xwayland, server->seat);

	/* Root-window cursor for X clients that don't set their own. */
	struct wlr_xcursor *xcursor =
		wlr_xcursor_manager_get_xcursor(server->cursor_mgr, "default", 1);
	if (xcursor) {
		struct wlr_xcursor_image *img = xcursor->images[0];
		wlr_xwayland_set_cursor(server->xwayland, img->buffer, img->width * 4,
			img->width, img->height, img->hotspot_x, img->hotspot_y);
	}
}

void xwayland_init(struct plat_server *server) {
	/* Lazy: Xwayland starts when the first X11 client connects. */
	server->xwayland = wlr_xwayland_create(server->display, server->compositor, true);
	if (!server->xwayland) {
		wlr_log(WLR_ERROR, "Xwayland unavailable; X11 apps will not run");
		return;
	}
	server->xwayland_ready.notify = xwayland_ready;
	wl_signal_add(&server->xwayland->events.ready, &server->xwayland_ready);
	server->new_xwayland_surface.notify = server_new_xwayland_surface;
	wl_signal_add(&server->xwayland->events.new_surface, &server->new_xwayland_surface);
	setenv("DISPLAY", server->xwayland->display_name, true);
}
