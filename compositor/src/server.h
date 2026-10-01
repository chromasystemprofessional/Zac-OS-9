#ifndef PLATINUM_SERVER_H
#define PLATINUM_SERVER_H

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>

enum plat_cursor_mode {
	PLAT_CURSOR_PASSTHROUGH,
	PLAT_CURSOR_MOVE,
	PLAT_CURSOR_RESIZE,
};

struct plat_server {
	struct wl_display *display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;

	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;
	struct wlr_scene_rect *desktop;
	struct wlr_scene_tree *view_layer;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;
	struct wl_list views; /* plat_view.link, front-most first */

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

	enum plat_cursor_mode cursor_mode;
	struct plat_view *grabbed_view;
	double grab_x, grab_y;
	struct wlr_box grab_geobox;
	uint32_t resize_edges;

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

struct plat_view {
	struct wl_list link;
	struct plat_server *server;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
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

/* view.c */
void view_init(struct plat_server *server);
void view_focus(struct plat_view *view, struct wlr_surface *surface);
struct plat_view *view_at(struct plat_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy);
void view_begin_interactive(struct plat_view *view,
		enum plat_cursor_mode mode, uint32_t edges);

#endif
