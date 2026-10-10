#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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
	struct wl_listener commit;
	struct wl_listener set_geometry;
	struct wl_listener request_configure;
	struct wl_listener destroy;
	int scale;
};

/* ---- HiDPI --------------------------------------------------------------
 * Most X11 programs draw at 1x, and are shown enlarged on a HiDPI screen. A
 * program that draws at the screen's scale itself - an Electron app the
 * desktop starts with --force-device-scale-factor (lib/electron.c) - would
 * be enlarged again, so its windows get a scale of their own: their X11
 * positions and sizes are in pixels, their buffers are shown at 1/scale,
 * and pointer positions are scaled back. X11's root is in pixels for this
 * (xwayland_output.c), so 1x windows sit in its top-left part unchanged. */

static int cmdline_scale(pid_t pid) {
	char path[64];
	snprintf(path, sizeof(path), "/proc/%d/cmdline", (int)pid);
	FILE *f = fopen(path, "r");
	if (!f) {
		return 1;
	}
	char args[8192];
	size_t n = fread(args, 1, sizeof(args) - 1, f);
	fclose(f);
	/* Arguments are NUL-separated, or space-separated once a program
	 * (Chromium) has rewritten its process title. */
	for (size_t i = 0; i < n; i++) {
		if (args[i] == '\0') {
			args[i] = ' ';
		}
	}
	args[n] = '\0';
	static const char flag[] = " --force-device-scale-factor=";
	const char *arg = strstr(args, flag);
	return arg ? (int)(atof(arg + sizeof(flag) - 1) + 0.5) : 1;
}

static int client_scale(struct plat_server *server, struct wlr_xwayland_surface *xsurface) {
	if (server->output_scale <= 1) {
		return 1;
	}
	if (xsurface->pid > 0) {
		return cmdline_scale(xsurface->pid) == server->output_scale ? server->output_scale : 1;
	}
	/* A menu without _NET_WM_PID belongs to the program in front. */
	struct plat_view *front = server->focused_view;
	return front && front->type == PLAT_VIEW_XWAYLAND ? front->x11_scale : 1;
}

static int to_logical(int v, int scale) {
	return v >= 0 ? (v + scale - 1) / scale : v / scale;
}

static void scale_buffer(struct wlr_scene_buffer *buffer, int sx, int sy, void *data) {
	int scale = *(int *)data;
	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(buffer);
	if (scene_surface && buffer->buffer) {
		struct wlr_surface *surface = scene_surface->surface;
		wlr_scene_buffer_set_dest_size(buffer, to_logical(surface->current.width, scale),
			to_logical(surface->current.height, scale));
	}
}

/* The scene sizes a buffer as its surface on every commit: after it has (our
 * commit listeners come after the scene's), size it down again. */
static void scale_tree(struct wlr_scene_tree *tree, int scale) {
	if (tree && scale > 1) {
		wlr_scene_node_for_each_buffer(&tree->node, scale_buffer, &scale);
	}
}

int xwayland_surface_scale(struct wlr_surface *surface) {
	struct wlr_xwayland_surface *xsurface =
		surface ? wlr_xwayland_surface_try_from_wlr_surface(surface) : NULL;
	if (!xsurface || !xsurface->data) {
		return 1;
	}
	if (xsurface->override_redirect) {
		return ((struct plat_unmanaged *)xsurface->data)->scale;
	}
	return ((struct plat_view *)xsurface->data)->x11_scale;
}

/* ---- plat_view_impl ---------------------------------------------------- */

static struct wlr_surface *xw_get_surface(struct plat_view *view) {
	return view->xsurface->surface;
}

static void xw_get_geometry(struct plat_view *view, struct wlr_box *geo) {
	*geo = (struct wlr_box){
		.width = to_logical(view->xsurface->width, view->x11_scale),
		.height = to_logical(view->xsurface->height, view->x11_scale),
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
	int s = view->x11_scale;
	wlr_xwayland_surface_configure(view->xsurface, x * s, y * s, width * s, height * s);
}

static void xw_set_fullscreen(struct plat_view *view, bool fullscreen) {
	wlr_xwayland_surface_set_fullscreen(view->xsurface, fullscreen);
}

/* X11 clients position their own popups from their window position. */
static void xw_moved(struct plat_view *view) {
	struct wlr_box geo;
	xw_get_geometry(view, &geo);
	xw_set_size(view, geo.width, geo.height);
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
	int s = view->x11_scale;
	if (hints->flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
		*min_w = to_logical(hints->min_width, s);
		*min_h = to_logical(hints->min_height, s);
	}
	if (hints->flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE) {
		*max_w = hints->max_width / s;
		*max_h = hints->max_height / s;
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
	view->x11_scale = client_scale(view->server, view->xsurface);
	wlr_log(WLR_DEBUG, "X11 window pid %d: scale %d", (int)view->xsurface->pid,
		view->x11_scale);
	scale_tree(view->surface_tree, view->x11_scale);
	view_set_title(view, view->xsurface->title);
	view_set_app_id(view, view->xsurface->class);
	view_place_new(view);
	struct wlr_box frame;
	int width, height;
	if (!view->xsurface->fullscreen && !view->xsurface->modal &&
			view_default_frame(view, view->xsurface->class, &frame, &width, &height)) {
		view_move_to(view, frame.x, frame.y);
		xw_set_size(view, width, height);
	}
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
	scale_tree(view->surface_tree, view->x11_scale);
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
	xw_set_size(view, to_logical(event->width, view->x11_scale),
		to_logical(event->height, view->x11_scale));
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
	view->x11_scale = 1;
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
	um->scale = client_scale(um->server, um->xsurface);
	scale_tree(um->tree, um->scale);
	wlr_scene_node_set_position(&um->tree->node, um->xsurface->x / um->scale,
		um->xsurface->y / um->scale);
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
		wlr_scene_node_set_position(&um->tree->node, um->xsurface->x / um->scale,
			um->xsurface->y / um->scale);
	}
}

static void um_commit(struct wl_listener *listener, void *data) {
	struct plat_unmanaged *um = wl_container_of(listener, um, commit);
	scale_tree(um->tree, um->scale);
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
	um->commit.notify = um_commit;
	wl_signal_add(&surface->events.commit, &um->commit);
}

static void um_dissociate(struct wl_listener *listener, void *data) {
	struct plat_unmanaged *um = wl_container_of(listener, um, dissociate);
	wl_list_remove(&um->map.link);
	wl_list_remove(&um->unmap.link);
	wl_list_remove(&um->commit.link);
	wl_list_init(&um->map.link);
	wl_list_init(&um->unmap.link);
	wl_list_init(&um->commit.link);
	if (um->tree) {
		wlr_scene_node_destroy(&um->tree->node);
		um->tree = NULL;
	}
}

static void um_destroy(struct wl_listener *listener, void *data) {
	struct plat_unmanaged *um = wl_container_of(listener, um, destroy);
	wl_list_remove(&um->map.link);
	wl_list_remove(&um->unmap.link);
	wl_list_remove(&um->commit.link);
	wl_list_remove(&um->associate.link);
	wl_list_remove(&um->dissociate.link);
	wl_list_remove(&um->set_geometry.link);
	wl_list_remove(&um->request_configure.link);
	wl_list_remove(&um->destroy.link);
	if (um->tree) {
		wlr_scene_node_destroy(&um->tree->node);
	}
	um->xsurface->data = NULL;
	free(um);
}

static void new_unmanaged(struct plat_server *server, struct wlr_xwayland_surface *xsurface) {
	struct plat_unmanaged *um = calloc(1, sizeof(*um));
	um->server = server;
	um->xsurface = xsurface;
	um->scale = 1;
	xsurface->data = um;
	wl_list_init(&um->map.link);
	wl_list_init(&um->unmap.link);
	wl_list_init(&um->commit.link);

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

	/* libxcb-cursor reads X resources, not XCURSOR_THEME (Qt's X11 backend). */
	xcb_connection_t *connection = xcb_connect(server->xwayland->display_name, NULL);
	if (xcb_connection_has_error(connection)) {
		wlr_log(WLR_ERROR, "Could not set the Xwayland cursor theme");
		xcb_disconnect(connection);
	} else {
		xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
		char resources[128];
		int length = snprintf(resources, sizeof(resources),
			"Xcursor.theme: %s\nXcursor.size: %d\n", ZACOS9_CURSOR_THEME, ZACOS9_CURSOR_SIZE);
		xcb_void_cookie_t cookie = xcb_change_property_checked(connection,
			XCB_PROP_MODE_REPLACE, screen->root, XCB_ATOM_RESOURCE_MANAGER,
			XCB_ATOM_STRING, 8, length, resources);
		xcb_generic_error_t *error = xcb_request_check(connection, cookie);
		if (error) {
			wlr_log(WLR_ERROR, "Could not set Xwayland cursor resources (X11 error %u)",
				error->error_code);
			free(error);
		}
		xcb_disconnect(connection);
	}

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
