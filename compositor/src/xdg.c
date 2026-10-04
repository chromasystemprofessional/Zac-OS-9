#include <assert.h>
#include <stdlib.h>

#include <wlr/types/wlr_xdg_activation_v1.h>

#include "server.h"

struct plat_popup {
	struct plat_server *server;
	struct wlr_xdg_popup *xdg_popup;
	struct wlr_scene_tree *parent_tree;
	struct wl_listener commit;
	struct wl_listener destroy;
};

struct plat_decoration {
	struct wlr_xdg_toplevel_decoration_v1 *wlr_decoration;
	struct plat_view *view;
	struct wl_listener request_mode;
	struct wl_listener destroy;
};

/* base->data holds the surface tree (popups attach to it); its parent is
 * the view's tree, whose node.data is the view. */
static struct plat_view *view_from_toplevel(struct wlr_xdg_toplevel *toplevel) {
	struct wlr_scene_tree *surface_tree = toplevel->base->data;
	if (!surface_tree) {
		return NULL;
	}
	return surface_tree->node.parent->node.data;
}

/* ---- plat_view_impl ---------------------------------------------------- */

static struct wlr_surface *xdg_get_surface(struct plat_view *view) {
	return view->xdg_toplevel->base->surface;
}

static void xdg_get_geometry(struct plat_view *view, struct wlr_box *geo) {
	wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, geo);
}

static void xdg_set_activated(struct plat_view *view, bool activated) {
	wlr_xdg_toplevel_set_activated(view->xdg_toplevel, activated);
}

static void xdg_set_size(struct plat_view *view, int width, int height) {
	wlr_xdg_toplevel_set_size(view->xdg_toplevel, width, height);
}

static void xdg_close(struct plat_view *view) {
	wlr_xdg_toplevel_send_close(view->xdg_toplevel);
}

static void xdg_get_size_limits(struct plat_view *view,
		int *min_w, int *min_h, int *max_w, int *max_h) {
	const struct wlr_xdg_toplevel_state *s = &view->xdg_toplevel->current;
	*min_w = s->min_width;
	*min_h = s->min_height;
	*max_w = s->max_width;
	*max_h = s->max_height;
}

static bool xdg_has_parent(struct plat_view *view) {
	return view->xdg_toplevel->parent != NULL;
}

static const struct plat_view_impl xdg_impl = {
	.get_surface = xdg_get_surface,
	.get_geometry = xdg_get_geometry,
	.set_activated = xdg_set_activated,
	.set_size = xdg_set_size,
	.close = xdg_close,
	.get_size_limits = xdg_get_size_limits,
	.has_parent = xdg_has_parent,
};

/* ---- toplevel events --------------------------------------------------- */

static void handle_map(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, map);
	view_set_title(view, view->xdg_toplevel->title);
	view_set_app_id(view, view->xdg_toplevel->app_id);
	view_handle_map(view);
}

static void handle_unmap(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, unmap);
	view_handle_unmap(view);
}

static void handle_commit(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, commit);
	if (view->xdg_toplevel->base->initial_commit) {
		/* Let the client pick its own size, as classic Mac apps do. */
		wlr_xdg_toplevel_set_size(view->xdg_toplevel, 0, 0);
		if (view->decoration) {
			wlr_xdg_toplevel_decoration_v1_set_mode(view->decoration,
				WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
		}
		return;
	}
	if (view->mapped) {
		view_update_frame(view);
	}
}

static void handle_set_title(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, set_title);
	view_set_title(view, view->xdg_toplevel->title);
}

static void handle_set_app_id(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, set_app_id);
	view_set_app_id(view, view->xdg_toplevel->app_id);
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, destroy);
	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
	wl_list_remove(&view->commit.link);
	wl_list_remove(&view->destroy.link);
	wl_list_remove(&view->request_move.link);
	wl_list_remove(&view->request_resize.link);
	wl_list_remove(&view->request_maximize.link);
	wl_list_remove(&view->request_fullscreen.link);
	wl_list_remove(&view->set_title.link);
	wl_list_remove(&view->set_app_id.link);
	view_handle_destroy(view);
}

static void handle_request_move(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, request_move);
	input_begin_grab(view->server, view, PLAT_CURSOR_MOVE, 0, DECOR_PART_DRAG);
}

static void handle_request_resize(struct wl_listener *listener, void *data) {
	struct wlr_xdg_toplevel_resize_event *event = data;
	struct plat_view *view = wl_container_of(listener, view, request_resize);
	input_begin_grab(view->server, view, PLAT_CURSOR_RESIZE, event->edges,
		DECOR_PART_GROW);
}

static void handle_request_maximize(struct wl_listener *listener, void *data) {
	/* Mac OS has no maximize; treat a client's request as the zoom box. */
	struct plat_view *view = wl_container_of(listener, view, request_maximize);
	if (!view->xdg_toplevel->base->initialized) {
		return;
	}
	if (view->mapped && view->xdg_toplevel->requested.maximized != view->zoomed) {
		view_toggle_zoom(view);
	} else {
		wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
	}
}

static void handle_request_fullscreen(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, request_fullscreen);
	if (view->xdg_toplevel->base->initialized) {
		wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
	}
}

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *xdg_toplevel = data;

	struct plat_view *view = calloc(1, sizeof(*view));
	view->xdg_toplevel = xdg_toplevel;
	view_setup(view, server, PLAT_VIEW_XDG, &xdg_impl);
	struct wlr_scene_tree *tree =
		wlr_scene_xdg_surface_create(view->scene_tree, xdg_toplevel->base);
	xdg_toplevel->base->data = tree;
	view_attach_surface_tree(view, tree);
	view_place_new(view);

	view->map.notify = handle_map;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &view->map);
	view->unmap.notify = handle_unmap;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &view->unmap);
	view->commit.notify = handle_commit;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit, &view->commit);
	view->destroy.notify = handle_destroy;
	wl_signal_add(&xdg_toplevel->events.destroy, &view->destroy);

	view->request_move.notify = handle_request_move;
	wl_signal_add(&xdg_toplevel->events.request_move, &view->request_move);
	view->request_resize.notify = handle_request_resize;
	wl_signal_add(&xdg_toplevel->events.request_resize, &view->request_resize);
	view->request_maximize.notify = handle_request_maximize;
	wl_signal_add(&xdg_toplevel->events.request_maximize, &view->request_maximize);
	view->request_fullscreen.notify = handle_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen, &view->request_fullscreen);
	view->set_title.notify = handle_set_title;
	wl_signal_add(&xdg_toplevel->events.set_title, &view->set_title);
	view->set_app_id.notify = handle_set_app_id;
	wl_signal_add(&xdg_toplevel->events.set_app_id, &view->set_app_id);
}

/* ---- popups ------------------------------------------------------------ */

static void popup_commit(struct wl_listener *listener, void *data) {
	struct plat_popup *popup = wl_container_of(listener, popup, commit);
	if (popup->xdg_popup->base->initial_commit) {
		/* Keep menus on the screen and off the menu bar, sliding or
		 * flipping them as the client allows. */
		int lx, ly;
		if (wlr_scene_node_coords(&popup->parent_tree->node, &lx, &ly)) {
			struct wlr_box box = output_usable_area(popup->server, lx, ly);
			box.x -= lx;
			box.y -= ly;
			wlr_xdg_popup_unconstrain_from_box(popup->xdg_popup, &box);
		}
		wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
	}
}

static void popup_destroy(struct wl_listener *listener, void *data) {
	struct plat_popup *popup = wl_container_of(listener, popup, destroy);
	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->destroy.link);
	free(popup);
}

static void server_new_xdg_popup(struct wl_listener *listener, void *data) {
	struct wlr_xdg_popup *xdg_popup = data;

	struct plat_popup *popup = calloc(1, sizeof(*popup));
	popup->server = wl_container_of(listener, popup->server, new_xdg_popup);
	popup->xdg_popup = xdg_popup;

	struct wlr_xdg_surface *parent =
		wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
	assert(parent);
	struct wlr_scene_tree *parent_tree = parent->data;
	popup->parent_tree = parent_tree;
	xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

	popup->commit.notify = popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);
	popup->destroy.notify = popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

/* ---- xdg-decoration: always server-side --------------------------------- */

static void decoration_request_mode(struct wl_listener *listener, void *data) {
	struct plat_decoration *deco = wl_container_of(listener, deco, request_mode);
	/* Before the initial commit the mode is sent from handle_commit. */
	if (deco->wlr_decoration->toplevel->base->initialized) {
		wlr_xdg_toplevel_decoration_v1_set_mode(deco->wlr_decoration,
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
}

static void decoration_destroy(struct wl_listener *listener, void *data) {
	struct plat_decoration *deco = wl_container_of(listener, deco, destroy);
	if (deco->view) {
		deco->view->decoration = NULL;
	}
	wl_list_remove(&deco->request_mode.link);
	wl_list_remove(&deco->destroy.link);
	free(deco);
}

static void server_new_xdg_decoration(struct wl_listener *listener, void *data) {
	struct wlr_xdg_toplevel_decoration_v1 *wlr_decoration = data;
	struct plat_decoration *deco = calloc(1, sizeof(*deco));
	deco->wlr_decoration = wlr_decoration;
	deco->view = view_from_toplevel(wlr_decoration->toplevel);
	if (deco->view) {
		deco->view->decoration = wlr_decoration;
	}
	deco->request_mode.notify = decoration_request_mode;
	wl_signal_add(&wlr_decoration->events.request_mode, &deco->request_mode);
	deco->destroy.notify = decoration_destroy;
	wl_signal_add(&wlr_decoration->events.destroy, &deco->destroy);
	decoration_request_mode(&deco->request_mode, NULL);
}

/* A client asking for one of its windows to come forward (Qt's
 * requestActivate, used by the Finder when a folder that is already open is
 * opened again): bring it to the front and give it the keyboard. */
static void handle_request_activate(struct wl_listener *listener, void *data) {
	const struct wlr_xdg_activation_v1_request_activate_event *event = data;
	struct wlr_xdg_surface *xdg = wlr_xdg_surface_try_from_wlr_surface(event->surface);
	if (!xdg || xdg->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL || !xdg->toplevel) {
		return;
	}
	struct plat_view *view = view_from_toplevel(xdg->toplevel);
	/* Only a window on screen: focusing one that isn't mapped yet would put it
	 * in the window list before view_handle_map does. */
	if (view && view->mapped) {
		view_focus(view);
	}
}

void xdg_init(struct plat_server *server) {
	server->xdg_shell = wlr_xdg_shell_create(server->display, 3);
	server->new_xdg_toplevel.notify = server_new_xdg_toplevel;
	wl_signal_add(&server->xdg_shell->events.new_toplevel, &server->new_xdg_toplevel);
	server->new_xdg_popup.notify = server_new_xdg_popup;
	wl_signal_add(&server->xdg_shell->events.new_popup, &server->new_xdg_popup);

	server->xdg_activation = wlr_xdg_activation_v1_create(server->display);
	server->request_activate.notify = handle_request_activate;
	wl_signal_add(&server->xdg_activation->events.request_activate, &server->request_activate);

	server->xdg_decoration_mgr = wlr_xdg_decoration_manager_v1_create(server->display);
	server->new_xdg_decoration.notify = server_new_xdg_decoration;
	wl_signal_add(&server->xdg_decoration_mgr->events.new_toplevel_decoration,
		&server->new_xdg_decoration);
}
