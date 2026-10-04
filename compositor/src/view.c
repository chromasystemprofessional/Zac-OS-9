#include <stdlib.h>
#include <string.h>

#include "frame.h"
#include "server.h"

/* Default minimum content size when the client doesn't state one. */
#define VIEW_MIN_W 100
#define VIEW_MIN_H 50

void view_setup(struct plat_view *view, struct plat_server *server,
		enum plat_view_type type, const struct plat_view_impl *impl) {
	view->server = server;
	view->type = type;
	view->impl = impl;
	view->scene_tree = wlr_scene_tree_create(server->view_layer);
	view->scene_tree->node.data = view;
	view->frame = frame_create(view, view->scene_tree);
	wl_list_init(&view->link);
}

void view_attach_surface_tree(struct plat_view *view, struct wlr_scene_tree *tree) {
	view->surface_tree = tree;
	if (tree) {
		wlr_scene_node_set_enabled(&tree->node, !view->collapsed);
		frame_raise_overlay(view->frame);
	}
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

void view_content_pos(struct plat_view *view, int *x, int *y) {
	struct decor_margins m = decor_margins(view->frame->st.style);
	*x = view->scene_tree->node.x + m.left;
	*y = view->scene_tree->node.y + m.top;
}

static bool view_resizable(struct plat_view *view) {
	int min_w, min_h, max_w, max_h;
	view->impl->get_size_limits(view, &min_w, &min_h, &max_w, &max_h);
	bool fixed = max_w > 0 && max_h > 0 && min_w == max_w && min_h == max_h;
	return !fixed;
}

void view_min_frame_size(struct plat_view *view, int *w, int *h) {
	int min_w, min_h, max_w, max_h;
	view->impl->get_size_limits(view, &min_w, &min_h, &max_w, &max_h);
	*w = frame_outer_w(view->frame, min_w > VIEW_MIN_W ? min_w : VIEW_MIN_W);
	*h = frame_outer_h(view->frame, min_h > VIEW_MIN_H ? min_h : VIEW_MIN_H);
}

/* Sync the frame to the client's current geometry. */
void view_update_frame(struct plat_view *view) {
	struct wlr_box geo;
	view->impl->get_geometry(view, &geo);
	/* Our own programs say how to frame a window; otherwise fixed-size
	 * windows with a parent are dialogs (HIG: movable modal). */
	int hint = platinum_shell_style_for(view->impl->get_surface(view));
	bool dialog = hint >= 0 ? hint == DECOR_STYLE_MOVABLE_MODAL
		: !view_resizable(view) && view->impl->has_parent(view);
	frame_set_style(view->frame, dialog ? DECOR_STYLE_MOVABLE_MODAL : DECOR_STYLE_DOCUMENT);
	struct decor_margins m = decor_margins(view->frame->st.style);
	if (view->surface_tree) {
		wlr_scene_node_set_position(&view->surface_tree->node,
			m.left - geo.x, m.top - geo.y);
	}
	bool resizable = view_resizable(view);
	frame_set_features(view->frame, resizable, resizable);
	frame_set_size(view->frame, geo.width, geo.height);
	frame_commit(view->frame);
}

void view_set_title(struct plat_view *view, const char *title) {
	frame_set_title(view->frame, title);
	frame_commit(view->frame);
	if (view->toplevel_handle) {
		wlr_foreign_toplevel_handle_v1_set_title(view->toplevel_handle, title ? title : "");
	}
}

void view_set_app_id(struct plat_view *view, const char *app_id) {
	free(view->app_id);
	view->app_id = strdup(app_id ? app_id : "");
	if (view->toplevel_handle) {
		wlr_foreign_toplevel_handle_v1_set_app_id(view->toplevel_handle, view->app_id);
	}
}

void view_place_new(struct plat_view *view) {
	/* Stagger new windows down and to the right, like the Finder does. */
	static int stagger;
	int offset = 20 * (stagger++ % 10);
	struct plat_server *server = view->server;
	struct wlr_box area = output_usable_area(server, server->cursor->x, server->cursor->y);
	view_move_to(view, area.x + 40 + offset, area.y + 20 + offset);
}

struct plat_view *view_topmost(struct plat_server *server) {
	struct plat_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view->mapped && !view->hidden) {
			return view;
		}
	}
	return NULL;
}

static void set_active(struct plat_view *view, bool active) {
	view->impl->set_activated(view, active);
	frame_set_active(view->frame, active);
	frame_commit(view->frame);
	if (view->toplevel_handle) {
		wlr_foreign_toplevel_handle_v1_set_activated(view->toplevel_handle, active);
	}
}

void view_set_hidden(struct plat_view *view, bool hidden) {
	if (view->hidden == hidden) {
		return;
	}
	struct plat_server *server = view->server;
	view->hidden = hidden;
	wlr_scene_node_set_enabled(&view->scene_tree->node, !hidden);
	if (view->toplevel_handle) {
		wlr_foreign_toplevel_handle_v1_set_minimized(view->toplevel_handle, hidden);
	}
	if (hidden && server->focused_view == view) {
		set_active(view, false);
		server->focused_view = NULL;
		struct plat_view *next = view_topmost(server);
		if (next) {
			view_focus(next);
		} else if (!server->focused_layer) {
			wlr_seat_keyboard_clear_focus(server->seat);
		}
		platinum_shell_focus_changed(server);
	}
}

void view_focus(struct plat_view *view) {
	if (!view) {
		return;
	}
	struct plat_server *server = view->server;
	if (view->hidden) {
		view_set_hidden(view, false);
	}
	wlr_scene_node_raise_to_top(&view->scene_tree->node);
	wl_list_remove(&view->link);
	wl_list_insert(&server->views, &view->link);

	struct plat_view *prev = server->focused_view;
	if (prev != view) {
		if (prev) {
			set_active(prev, false);
		}
		server->focused_view = view;
		set_active(view, true);
		platinum_shell_focus_changed(server);
	}
	/* While the menu bar tracks a menu (an exclusive layer) it keeps the
	 * keyboard. A shell surface that merely had it - the desktop, after a
	 * click or a drag on it - gives it up to the window coming forward, as
	 * a click in a window already makes it (input.c), so a window opened
	 * then (an alert, say) gets its keys. */
	struct plat_layer_surface *fl = server->focused_layer;
	if (fl && fl->layer_surface->current.keyboard_interactive !=
			ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) {
		server->focused_layer = NULL;
		fl = NULL;
	}
	if (!fl) {
		input_keyboard_enter(server, view->impl->get_surface(view));
	}
}

/* Finds the topmost surface or window frame under a point. Returns the
 * window it belongs to, or NULL for the desktop and for frameless surfaces
 * (X11 menus), which still come back through *surface. */
struct plat_view *view_at(struct plat_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	*surface = NULL;
	struct wlr_scene_node *node =
		wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
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
	if (view->impl->moved) {
		view->impl->moved(view);
	}
	platinum_shell_report_position(view);
}

void view_resize_frame(struct plat_view *view, struct wlr_box box) {
	wlr_scene_node_set_position(&view->scene_tree->node, box.x, box.y);
	platinum_shell_report_position(view);
	struct decor_margins m = decor_margins(view->frame->st.style);
	view->impl->set_size(view, box.width - m.left - m.right,
		box.height - m.top - m.bottom);
}

void view_close(struct plat_view *view) {
	view->impl->close(view);
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
	 * we use the area below the menu bar with a 3 px margin. */
	const int margin = 3;
	struct wlr_box area = output_usable_area(server,
		frame.x + frame.width / 2.0, frame.y + frame.height / 2.0);
	if (!wlr_box_empty(&area)) {
		screen = area;
	}
	view->unzoomed = frame;
	view->zoomed = true;
	view_resize_frame(view, (struct wlr_box){
		.x = screen.x + margin,
		.y = screen.y + margin,
		.width = screen.width - 2 * margin - DECOR_SHADOW,
		.height = screen.height - 2 * margin - DECOR_SHADOW,
	});
}

void view_set_collapsed(struct plat_view *view, bool collapsed) {
	if (view->collapsed == collapsed) {
		return;
	}
	view->collapsed = collapsed;
	if (view->surface_tree) {
		wlr_scene_node_set_enabled(&view->surface_tree->node, !collapsed);
	}
	frame_set_collapsed(view->frame, collapsed);
	frame_commit(view->frame);
	/* TODO(phase 6): collapse/expand sounds (HIG: on by default). */
}

void view_set_pressed(struct plat_view *view, enum decor_part part) {
	frame_set_pressed(view->frame, part);
	frame_commit(view->frame);
}

static void handle_request_activate(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, handle_request_activate);
	view_focus(view);
}

static void handle_request_minimize(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, handle_request_minimize);
	struct wlr_foreign_toplevel_handle_v1_minimized_event *event = data;
	view_set_hidden(view, event->minimized);
}

static void handle_request_close(struct wl_listener *listener, void *data) {
	struct plat_view *view = wl_container_of(listener, view, handle_request_close);
	view_close(view);
}

static void create_toplevel_handle(struct plat_view *view) {
	struct plat_server *server = view->server;
	struct wlr_foreign_toplevel_handle_v1 *h =
		wlr_foreign_toplevel_handle_v1_create(server->foreign_toplevel_mgr);
	if (!h) {
		return;
	}
	view->toplevel_handle = h;
	wlr_foreign_toplevel_handle_v1_set_title(h,
		view->frame->title_str ? view->frame->title_str : "");
	wlr_foreign_toplevel_handle_v1_set_app_id(h, view->app_id ? view->app_id : "");
	struct wlr_box box = view_frame_box(view);
	struct plat_output *output =
		output_at(server, box.x + box.width / 2.0, box.y + box.height / 2.0);
	if (output) {
		wlr_foreign_toplevel_handle_v1_output_enter(h, output->wlr_output);
	}
	view->handle_request_activate.notify = handle_request_activate;
	wl_signal_add(&h->events.request_activate, &view->handle_request_activate);
	view->handle_request_minimize.notify = handle_request_minimize;
	wl_signal_add(&h->events.request_minimize, &view->handle_request_minimize);
	view->handle_request_close.notify = handle_request_close;
	wl_signal_add(&h->events.request_close, &view->handle_request_close);
}

static void destroy_toplevel_handle(struct plat_view *view) {
	if (!view->toplevel_handle) {
		return;
	}
	wl_list_remove(&view->handle_request_activate.link);
	wl_list_remove(&view->handle_request_minimize.link);
	wl_list_remove(&view->handle_request_close.link);
	wlr_foreign_toplevel_handle_v1_destroy(view->toplevel_handle);
	view->toplevel_handle = NULL;
}

void view_handle_map(struct plat_view *view) {
	view->mapped = true;
	view->hidden = false;
	wlr_scene_node_set_enabled(&view->scene_tree->node, true);
	view_update_frame(view);
	struct plat_server *server = view->server;
	struct wlr_box area = output_usable_area(server, server->cursor->x, server->cursor->y);
	struct wlr_box frame = view_frame_box(view);
	int hx, hy;
	if (platinum_shell_position_for(view->impl->get_surface(view), &hx, &hy)) {
		/* Spatial: back where its program remembers it, but always with
		 * the title bar on screen and below the menu bar. */
		if (!wlr_box_empty(&area)) {
			hx = hx > area.x + area.width - 40 ? area.x + area.width - 40 : hx;
			hx = hx + frame.width < area.x + 40 ? area.x + 40 - frame.width : hx;
			hy = hy > area.y + area.height - 30 ? area.y + area.height - 30 : hy;
			hy = hy < area.y ? area.y : hy;
		}
		view_move_to(view, hx, hy);
	} else if (view->frame->st.style == DECOR_STYLE_MOVABLE_MODAL) {
		/* Mac OS puts alerts and dialogs centred, a third of the way down. */
		view_move_to(view, area.x + (area.width - frame.width) / 2,
			area.y + (area.height - frame.height) / 3);
	}
	wl_list_remove(&view->link); /* never in the list twice */
	wl_list_insert(&view->server->views, &view->link);
	create_toplevel_handle(view);
	view_focus(view);
	platinum_shell_report_position(view);
}

void view_handle_unmap(struct plat_view *view) {
	struct plat_server *server = view->server;
	view->mapped = false;
	destroy_toplevel_handle(view);
	if (view == server->grabbed_view) {
		outline_hide(&server->outline);
		server->cursor_mode = PLAT_CURSOR_PASSTHROUGH;
		server->grabbed_view = NULL;
	}
	if (view == server->last_click_view) {
		server->last_click_view = NULL;
	}
	wl_list_remove(&view->link);
	wl_list_init(&view->link);

	/* Like the Mac, activate the next window in line. */
	if (server->focused_view == view) {
		server->focused_view = NULL;
		struct plat_view *next = view_topmost(server);
		if (next) {
			view_focus(next);
		}
		platinum_shell_focus_changed(server);
	}
}

void view_handle_destroy(struct plat_view *view) {
	if (view->mapped) {
		view_handle_unmap(view);
	}
	wl_list_remove(&view->link);
	frame_destroy(view->frame);
	free(view->app_id);
	wlr_scene_node_destroy(&view->scene_tree->node);
	free(view);
}
