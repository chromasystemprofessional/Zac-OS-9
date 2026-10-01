#include <assert.h>
#include <stdlib.h>

#include "frame.h"
#include "server.h"

struct plat_popup {
	struct wlr_xdg_popup *xdg_popup;
	struct wl_listener commit;
	struct wl_listener destroy;
};

struct plat_decoration {
	struct wlr_xdg_toplevel_decoration_v1 *wlr_decoration;
	struct plat_view *view;
	struct wl_listener request_mode;
	struct wl_listener destroy;
};

/* Default minimum content size when the client doesn't state one. */
#define VIEW_MIN_W 100
#define VIEW_MIN_H 50

/* base->data holds the surface tree (popups attach to it); its parent is
 * the view's tree, whose node.data is the view. */
static struct plat_view *view_from_toplevel(struct wlr_xdg_toplevel *toplevel) {
	struct wlr_scene_tree *surface_tree = toplevel->base->data;
	if (!surface_tree) {
		return NULL;
	}
	return surface_tree->node.parent->node.data;
}

struct wlr_box view_frame_box(struct plat_view *view) {
	const struct decor_state *st = &view->frame->st;
	return (struct wlr_box){
		.x = view->scene_tree->node.x,
		.y = view->scene_tree->node.y,
		.width = st->width,
		.height = st->collapsed ? DECOR_COLLAPSED_H : st->height,
	};
}

static bool view_resizable(struct plat_view *view) {
	const struct wlr_xdg_toplevel_state *s = &view->xdg_toplevel->current;
	bool fixed = s->max_width > 0 && s->max_height > 0 &&
		s->min_width == s->max_width && s->min_height == s->max_height;
	return !fixed;
}

void view_min_frame_size(struct plat_view *view, int *w, int *h) {
	const struct wlr_xdg_toplevel_state *s = &view->xdg_toplevel->current;
	*w = frame_outer_w(s->min_width > VIEW_MIN_W ? s->min_width : VIEW_MIN_W);
	*h = frame_outer_h(s->min_height > VIEW_MIN_H ? s->min_height : VIEW_MIN_H);
}

/* Sync the frame to the client's current geometry. */
static void view_update_frame(struct plat_view *view) {
	struct wlr_box geo;
	wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo);
	wlr_scene_node_set_position(&view->surface_tree->node,
		DECOR_LEFT - geo.x, DECOR_TOP - geo.y);
	bool resizable = view_resizable(view);
	frame_set_features(view->frame, resizable, resizable);
	frame_set_size(view->frame, geo.width, geo.height);
	frame_commit(view->frame);
}

void view_focus(struct plat_view *view) {
	if (!view) {
		return;
	}
	struct plat_server *server = view->server;
	wlr_scene_node_raise_to_top(&view->scene_tree->node);
	wl_list_remove(&view->link);
	wl_list_insert(&server->views, &view->link);

	struct plat_view *prev = server->focused_view;
	if (prev == view) {
		return;
	}
	if (prev) {
		wlr_xdg_toplevel_set_activated(prev->xdg_toplevel, false);
		frame_set_active(prev->frame, false);
		frame_commit(prev->frame);
	}
	server->focused_view = view;
	wlr_xdg_toplevel_set_activated(view->xdg_toplevel, true);
	frame_set_active(view->frame, true);
	frame_commit(view->frame);

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (keyboard) {
		wlr_seat_keyboard_notify_enter(server->seat,
			view->xdg_toplevel->base->surface, keyboard->keycodes,
			keyboard->num_keycodes, &keyboard->modifiers);
	}
}

struct plat_view *view_at(struct plat_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	*surface = NULL;
	struct wlr_scene_node *node =
		wlr_scene_node_at(&server->view_layer->node, lx, ly, sx, sy);
	if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
		return NULL;
	}
	struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
	struct wlr_scene_surface *scene_surface =
		wlr_scene_surface_try_from_buffer(scene_buffer);
	if (scene_surface) {
		*surface = scene_surface->surface;
	}

	/* Walk up to the view's tree; frame buffers and surfaces both lead there. */
	struct wlr_scene_tree *tree = node->parent;
	while (tree && !tree->node.data) {
		tree = tree->node.parent;
	}
	return tree ? tree->node.data : NULL;
}

enum decor_part view_part_at(struct plat_view *view, double lx, double ly) {
	return decor_hit(&view->frame->st,
		(int)lx - view->scene_tree->node.x, (int)ly - view->scene_tree->node.y);
}

void view_move_to(struct plat_view *view, int x, int y) {
	wlr_scene_node_set_position(&view->scene_tree->node, x, y);
}

void view_resize_frame(struct plat_view *view, struct wlr_box box) {
	view_move_to(view, box.x, box.y);
	wlr_xdg_toplevel_set_size(view->xdg_toplevel,
		box.width - DECOR_LEFT - DECOR_RIGHT, box.height - DECOR_TOP - DECOR_BOTTOM);
}

void view_close(struct plat_view *view) {
	wlr_xdg_toplevel_send_close(view->xdg_toplevel);
}

void view_toggle_zoom(struct plat_view *view) {
	if (view->collapsed) {
		view_set_collapsed(view, false);
	}
	if (view->zoomed) {
		view->zoomed = false;
		view_resize_frame(view, view->unzoomed);
		return;
	}

	struct wlr_box frame = view_frame_box(view);
	struct plat_server *server = view->server;
	struct wlr_output *output = wlr_output_layout_output_at(server->output_layout,
		frame.x + frame.width / 2.0, frame.y + frame.height / 2.0);
	struct wlr_box screen;
	wlr_output_layout_get_box(server->output_layout, output, &screen);
	if (wlr_box_empty(&screen)) {
		return;
	}

	/* TODO: Mac OS zooms to the app's "standard state"; for Linux clients
	 * we use the screen below the menu bar with a 3 px margin. */
	const int margin = 3;
	view->unzoomed = frame;
	view->zoomed = true;
	view_resize_frame(view, (struct wlr_box){
		.x = screen.x + margin,
		.y = screen.y + PLAT_MENUBAR_H + margin,
		.width = screen.width - 2 * margin - DECOR_SHADOW,
		.height = screen.height - PLAT_MENUBAR_H - 2 * margin - DECOR_SHADOW,
	});
}

void view_set_collapsed(struct plat_view *view, bool collapsed) {
	if (view->collapsed == collapsed) {
		return;
	}
	view->collapsed = collapsed;
	wlr_scene_node_set_enabled(&view->surface_tree->node, !collapsed);
	frame_set_collapsed(view->frame, collapsed);
	frame_commit(view->frame);
	/* TODO(phase 6): collapse/expand sounds (HIG: on by default). */
}

void view_set_pressed(struct plat_view *view, enum decor_part part) {
	frame_set_pressed(view->frame, part);
	frame_commit(view->frame);
}

static void view_map(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, map);
	view->mapped = true;
	frame_set_title(view->frame, view->xdg_toplevel->title);
	view_update_frame(view);
	wl_list_insert(&view->server->views, &view->link);
	view_focus(view);
}

static void view_unmap(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, unmap);
	struct plat_server *server = view->server;
	view->mapped = false;
	if (view == server->grabbed_view) {
		outline_hide(&server->outline);
		server->cursor_mode = PLAT_CURSOR_PASSTHROUGH;
		server->grabbed_view = NULL;
	}
	if (view == server->last_click_view) {
		server->last_click_view = NULL;
	}
	wl_list_remove(&view->link);

	/* Like the Mac, activate the next window in line. */
	if (server->focused_view == view) {
		server->focused_view = NULL;
		if (!wl_list_empty(&server->views)) {
			struct plat_view *next = wl_container_of(server->views.next, next, link);
			view_focus(next);
		}
	}
}

static void view_commit(struct wl_listener *listener, void *data) {
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

static void view_set_title(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, set_title);
	frame_set_title(view->frame, view->xdg_toplevel->title);
	frame_commit(view->frame);
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
	wl_list_remove(&view->set_title.link);
	frame_destroy(view->frame);
	wlr_scene_node_destroy(&view->scene_tree->node);
	free(view);
}

static void view_request_move(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, request_move);
	input_begin_grab(view->server, view, PLAT_CURSOR_MOVE, 0, DECOR_PART_DRAG);
}

static void view_request_resize(struct wl_listener *listener, void *data) {
	struct wlr_xdg_toplevel_resize_event *event = data;
	struct plat_view *view = wl_container_of(listener, view, request_resize);
	input_begin_grab(view->server, view, PLAT_CURSOR_RESIZE, event->edges,
		DECOR_PART_GROW);
}

static void view_request_maximize(struct wl_listener *listener, void *data) {
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
	view->scene_tree = wlr_scene_tree_create(server->view_layer);
	view->scene_tree->node.data = view;
	view->frame = frame_create(view, view->scene_tree);
	view->surface_tree = wlr_scene_xdg_surface_create(view->scene_tree, xdg_toplevel->base);
	frame_raise_overlay(view->frame);
	xdg_toplevel->base->data = view->surface_tree;

	/* Stagger new windows down and to the right, like the Finder does. */
	static int stagger;
	int offset = 20 * (stagger++ % 10);
	view_move_to(view, 40 + offset, PLAT_MENUBAR_H + 20 + offset);

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
	view->set_title.notify = view_set_title;
	wl_signal_add(&xdg_toplevel->events.set_title, &view->set_title);
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

/* ---- xdg-decoration: always server-side --------------------------------- */

static void decoration_request_mode(struct wl_listener *listener, void *data) {
	struct plat_decoration *deco = wl_container_of(listener, deco, request_mode);
	/* Before the initial commit the mode is sent from view_commit. */
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

void view_init(struct plat_server *server) {
	wl_list_init(&server->views);
	server->xdg_shell = wlr_xdg_shell_create(server->display, 3);
	server->new_xdg_toplevel.notify = server_new_xdg_toplevel;
	wl_signal_add(&server->xdg_shell->events.new_toplevel, &server->new_xdg_toplevel);
	server->new_xdg_popup.notify = server_new_xdg_popup;
	wl_signal_add(&server->xdg_shell->events.new_popup, &server->new_xdg_popup);

	server->xdg_decoration_mgr = wlr_xdg_decoration_manager_v1_create(server->display);
	server->new_xdg_decoration.notify = server_new_xdg_decoration;
	wl_signal_add(&server->xdg_decoration_mgr->events.new_toplevel_decoration,
		&server->new_xdg_decoration);
}
