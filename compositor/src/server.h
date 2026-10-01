#ifndef PLATINUM_SERVER_H
#define PLATINUM_SERVER_H

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/xwayland.h>

#include "decor.h"
#include "outline.h"

/* Height reserved for the Mac OS menu bar (Phase 2). */
#define PLAT_MENUBAR_H 20
/* Mac OS default double-click time is 32 ticks (~533 ms). */
#define PLAT_DOUBLE_CLICK_MS 533

enum plat_cursor_mode {
	PLAT_CURSOR_PASSTHROUGH,
	PLAT_CURSOR_MOVE,      /* dragging a window outline */
	PLAT_CURSOR_RESIZE,    /* dragging a resize outline */
	PLAT_CURSOR_TRACK_BOX, /* holding a title-bar box down */
};

struct plat_server {
	struct wl_display *display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_compositor *compositor;
	int output_scale; /* integer HiDPI scale applied to every output */

	/* Scene layers, bottom to top. */
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;
	struct wlr_scene_rect *desktop;
	struct wlr_scene_tree *view_layer;
	struct wlr_scene_tree *unmanaged_layer; /* X11 menus, tooltips */
	struct wlr_scene_tree *overlay_layer;
	struct plat_outline outline;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;
	struct wlr_xdg_decoration_manager_v1 *xdg_decoration_mgr;
	struct wl_listener new_xdg_decoration;

	struct wlr_xwayland *xwayland;
	struct wl_listener xwayland_ready;
	struct wl_listener new_xwayland_surface;

	struct wl_list views; /* plat_view.link, front-most first */
	struct plat_view *focused_view;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wlr_seat *seat;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener request_set_selection;
	struct wl_list keyboards;

	/* Interactive grab state. */
	enum plat_cursor_mode cursor_mode;
	struct plat_view *grabbed_view;
	double grab_x, grab_y;       /* cursor position at grab start */
	struct wlr_box grab_box;     /* frame box at grab start */
	uint32_t resize_edges;
	enum decor_part grab_part;
	bool grab_moved;

	/* Double-click detection on title bars. */
	uint32_t last_click_msec;
	struct plat_view *last_click_view;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;
};

struct plat_output {
	struct wl_list link;
	struct plat_server *server;
	struct wlr_output *wlr_output;
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
};

struct plat_view;

/* What differs between Wayland (xdg-shell) and X11 (Xwayland) windows. */
struct plat_view_impl {
	struct wlr_surface *(*get_surface)(struct plat_view *view);
	/* Visible content box within the surface tree (excludes client shadows). */
	void (*get_geometry)(struct plat_view *view, struct wlr_box *geo);
	void (*set_activated)(struct plat_view *view, bool activated);
	/* Ask the client for a new content size. */
	void (*set_size)(struct plat_view *view, int width, int height);
	/* The frame moved (X11 clients must be told their new position). */
	void (*moved)(struct plat_view *view);
	void (*close)(struct plat_view *view);
	/* 0 means unconstrained. */
	void (*get_size_limits)(struct plat_view *view,
		int *min_w, int *min_h, int *max_w, int *max_h);
};

enum plat_view_type {
	PLAT_VIEW_XDG,
	PLAT_VIEW_XWAYLAND,
};

struct plat_view {
	struct wl_list link;
	struct plat_server *server;
	enum plat_view_type type;
	const struct plat_view_impl *impl;
	union {
		struct wlr_xdg_toplevel *xdg_toplevel;
		struct wlr_xwayland_surface *xsurface;
	};

	/* Positioned at the frame's outer top-left; node.data = this view. */
	struct wlr_scene_tree *scene_tree;
	struct wlr_scene_tree *surface_tree; /* NULL until an X11 window has a surface */
	struct plat_frame *frame;
	struct wlr_xdg_toplevel_decoration_v1 *decoration;

	bool mapped;
	bool collapsed;
	bool zoomed;
	struct wlr_box unzoomed; /* frame box to restore from zoom */

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener set_title;
	/* Xwayland only. */
	struct wl_listener associate;
	struct wl_listener dissociate;
	struct wl_listener request_configure;
	struct wl_listener request_activate;
};

struct plat_keyboard {
	struct wl_list link;
	struct plat_server *server;
	struct wlr_keyboard *wlr_keyboard;
	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

/* output.c */
void output_init(struct plat_server *server);

/* input.c */
void input_init(struct plat_server *server);
void input_begin_grab(struct plat_server *server, struct plat_view *view,
		enum plat_cursor_mode mode, uint32_t edges, enum decor_part part);

/* view.c: window behaviour shared by every client type */
void view_setup(struct plat_view *view, struct plat_server *server,
		enum plat_view_type type, const struct plat_view_impl *impl);
void view_attach_surface_tree(struct plat_view *view, struct wlr_scene_tree *tree);
void view_handle_map(struct plat_view *view);
void view_handle_unmap(struct plat_view *view);
void view_handle_destroy(struct plat_view *view);
void view_update_frame(struct plat_view *view);
void view_place_new(struct plat_view *view);
void view_set_title(struct plat_view *view, const char *title);

void view_focus(struct plat_view *view);
struct plat_view *view_at(struct plat_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy);
enum decor_part view_part_at(struct plat_view *view, double lx, double ly);
struct wlr_box view_frame_box(struct plat_view *view);
void view_content_pos(struct plat_view *view, int *x, int *y);
void view_move_to(struct plat_view *view, int x, int y);
void view_resize_frame(struct plat_view *view, struct wlr_box frame_box);
void view_close(struct plat_view *view);
void view_toggle_zoom(struct plat_view *view);
void view_set_collapsed(struct plat_view *view, bool collapsed);
void view_set_pressed(struct plat_view *view, enum decor_part part);
void view_min_frame_size(struct plat_view *view, int *w, int *h);

/* xdg.c */
void xdg_init(struct plat_server *server);

/* xwayland.c */
void xwayland_init(struct plat_server *server);

#endif
