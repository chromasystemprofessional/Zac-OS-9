#include <assert.h>
#include <stdlib.h>
#include <wlr/util/edges.h>

#include "server.h"

struct plat_popup {
	struct wlr_xdg_popup *xdg_popup;
	struct wl_listener commit;
	struct wl_listener destroy;
};

void view_focus(struct plat_view *view, struct wlr_surface *surface) {
	if (!view) {
		return;
	}
	struct plat_server *server = view->server;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev = seat->keyboard_state.focused_surface;
	if (prev == surface) {
		return;
	}
	if (prev) {
		struct wlr_xdg_toplevel *prev_toplevel =
			wlr_xdg_toplevel_try_from_wlr_surface(prev);
		if (prev_toplevel) {
			wlr_xdg_toplevel_set_activated(prev_toplevel, false);
		}
	}

	wlr_scene_node_raise_to_top(&view->scene_tree->node);
	wl_list_remove(&view->link);
	wl_list_insert(&server->views, &view->link);
	wlr_xdg_toplevel_set_activated(view->xdg_toplevel, true);

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	if (keyboard) {
		wlr_seat_keyboard_notify_enter(seat, view->xdg_toplevel->base->surface,
			keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	}
}

struct plat_view *view_at(struct plat_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	struct wlr_scene_node *node =
		wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
	if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}
	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *scene_surface =
		wlr_scene_surface_try_from_buffer(scene_buffer);
	if (!scene_surface) {
		return NULL;
	}
	*surface = scene_surface->surface;

	/* Walk up to the toplevel's tree, which carries the plat_view. */
	struct wlr_scene_tree *tree = node->parent;
	while (tree && !tree->node.data) {
		tree = tree->node.parent;
	}
	return tree ? tree->node.data : NULL;
}

void view_begin_interactive(struct plat_view *view,
		enum plat_cursor_mode mode, uint32_t edges) {
	struct plat_server *server = view->server;
	server->grabbed_view = view;
	server->cursor_mode = mode;

	if (mode == PLAT_CURSOR_MOVE) {
		server->grab_x = server->cursor->x - view->scene_tree->node.x;
		server->grab_y = server->cursor->y - view->scene_tree->node.y;
		return;
	}

	struct wlr_box geo;
	wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo);
	double border_x = (view->scene_tree->node.x + geo.x) +
		((edges & WLR_EDGE_RIGHT) ? geo.width : 0);
	double border_y = (view->scene_tree->node.y + geo.y) +
		((edges & WLR_EDGE_BOTTOM) ? geo.height : 0);
	server->grab_x = server->cursor->x - border_x;
	server->grab_y = server->cursor->y - border_y;
	server->grab_geobox = geo;
	server->grab_geobox.x += view->scene_tree->node.x;
	server->grab_geobox.y += view->scene_tree->node.y;
	server->resize_edges = edges;
}

static void view_map(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, map);
	wl_list_insert(&view->server->views, &view->link);
	view_focus(view, view->xdg_toplevel->base->surface);
}

static void view_unmap(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, unmap);
	if (view == view->server->grabbed_view) {
		view->server->cursor_mode = PLAT_CURSOR_PASSTHROUGH;
		view->server->grabbed_view = NULL;
	}
	wl_list_remove(&view->link);
}

static void view_commit(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, commit);
	if (view->xdg_toplevel->base->initial_commit) {
		/* Let the client pick its own size, as classic Mac apps do. */
		wlr_xdg_toplevel_set_size(view->xdg_toplevel, 0, 0);
	}
}

static void view_destroy(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, destroy);
	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
	wl_list_remove(&view->commit.link);
	wl_list_remove(&view->destroy.link);
	wl_list_remove(&view->request_move.link);
	wl_list_remove(&view->request_resize.link);
	wl_list_remove(&view->request_maximize.link);
	wl_list_remove(&view->request_fullscreen.link);
	free(view);
}

static void view_request_move(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, request_move);
	view_begin_interactive(view, PLAT_CURSOR_MOVE, 0);
}

static void view_request_resize(struct wl_listener *listener, void *data) {
	struct wlr_xdg_toplevel_resize_event *event = data;
	struct plat_view *view = wl_container_of(listener, view, request_resize);
	view_begin_interactive(view, PLAT_CURSOR_RESIZE, event->edges);
}

static void view_request_maximize(struct wl_listener *listener, void *data) {
	/* Mac OS has no maximize; the zoom box comes in Phase 1. Ack only. */
	struct plat_view *view = wl_container_of(listener, view, request_maximize);
	if (view->xdg_toplevel->base->initialized) {
		wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
	}
}

static void view_request_fullscreen(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, request_fullscreen);
	if (view->xdg_toplevel->base->initialized) {
		wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
	}
}

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *xdg_toplevel = data;

	struct plat_view *view = calloc(1, sizeof(*view));
	view->server = server;
	view->xdg_toplevel = xdg_toplevel;
	view->scene_tree =
		wlr_scene_xdg_surface_create(server->view_layer, xdg_toplevel->base);
	view->scene_tree->node.data = view;
	xdg_toplevel->base->data = view->scene_tree;

	/* Stagger new windows down and to the right, like the Finder does. */
	static int stagger;
	int offset = 20 * (stagger++ % 10);
	wlr_scene_node_set_position(&view->scene_tree->node, 40 + offset, 60 + offset);

	view->map.notify = view_map;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &view->map);
	view->unmap.notify = view_unmap;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &view->unmap);
	view->commit.notify = view_commit;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit, &view->commit);
	view->destroy.notify = view_destroy;
	wl_signal_add(&xdg_toplevel->events.destroy, &view->destroy);

	view->request_move.notify = view_request_move;
	wl_signal_add(&xdg_toplevel->events.request_move, &view->request_move);
	view->request_resize.notify = view_request_resize;
	wl_signal_add(&xdg_toplevel->events.request_resize, &view->request_resize);
	view->request_maximize.notify = view_request_maximize;
	wl_signal_add(&xdg_toplevel->events.request_maximize, &view->request_maximize);
	view->request_fullscreen.notify = view_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen, &view->request_fullscreen);
}

static void popup_commit(struct wl_listener *listener, void *data) {
	struct plat_popup *popup = wl_container_of(listener, popup, commit);
	if (popup->xdg_popup->base->initial_commit) {
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
	popup->xdg_popup = xdg_popup;

	struct wlr_xdg_surface *parent =
		wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
	assert(parent);
	struct wlr_scene_tree *parent_tree = parent->data;
	xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

	popup->commit.notify = popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);
	popup->destroy.notify = popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

void view_init(struct plat_server *server) {
	wl_list_init(&server->views);
	server->xdg_shell = wlr_xdg_shell_create(server->display, 3);
	server->new_xdg_toplevel.notify = server_new_xdg_toplevel;
	wl_signal_add(&server->xdg_shell->events.new_toplevel, &server->new_xdg_toplevel);
	server->new_xdg_popup.notify = server_new_xdg_popup;
	wl_signal_add(&server->xdg_shell->events.new_popup, &server->new_xdg_popup);
}
