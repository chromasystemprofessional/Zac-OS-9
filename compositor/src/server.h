#ifndef ZACOS9_SERVER_H
#define ZACOS9_SERVER_H

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/xwayland.h>

#include "decor.h"
#include "outline.h"
#include "window_sounds.h"

/* Mac OS default double-click time is 32 ticks (~533 ms). */
#define PLAT_DOUBLE_CLICK_MS 533
/* assets/cursors, 16 px at 1x (Mac OS cursors are 16x16). */
#define ZACOS9_CURSOR_THEME "ZacOS9"
#define ZACOS9_CURSOR_SIZE 16

enum plat_cursor_mode {
	PLAT_CURSOR_PASSTHROUGH,
	PLAT_CURSOR_MOVE,      /* dragging a window outline */
	PLAT_CURSOR_RESIZE,    /* dragging a resize outline */
	PLAT_CURSOR_TRACK_BOX, /* holding a title-bar box down */
};

struct plat_server {
	struct wl_display *display;
	/* Our own socket ("wayland-1"), once made. Not $WAYLAND_DISPLAY: nested,
	 * that names the outer session until we replace it. */
	const char *socket;
	struct wlr_backend *backend;
	struct wlr_session *session; /* on a real screen (DRM); NULL nested */
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_compositor *compositor;
	int output_scale; /* integer HiDPI scale applied to every output */
	int default_scale; /* from -S / ZACOS9_SCALE, until a panel picks one */
	bool scale_explicit; /* -S or ZACOS9_SCALE given: no automatic 2x */
	/* Mouse and Keyboard panel settings (prefs.c). */
	double pointer_speed;
	int double_click_ms;

	/* Scene layers, bottom to top:
	 *   desktop, shell[BACKGROUND], shell[BOTTOM], view_layer,
	 *   shell[TOP] (menu bar), fullscreen_layer, dialog_layer, unmanaged_layer,
	 *   shell[OVERLAY], overlay_layer */
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;
	struct wlr_scene_rect *desktop;
	struct wlr_scene_tree *shell_layers[4]; /* indexed by zwlr_layer_shell_v1_layer */
	struct wlr_scene_tree *view_layer;
	struct wlr_scene_tree *unmanaged_layer; /* X11 menus, tooltips */
	struct wlr_scene_tree *fullscreen_layer;
	struct wlr_scene_tree *dialog_layer; /* movable-modal windows above app windows */
	struct wlr_scene_tree *overlay_layer;   /* drag outlines */
	struct plat_outline outline;

	struct wlr_layer_shell_v1 *layer_shell;
	struct wl_listener new_layer_surface;
	struct wl_list layer_surfaces; /* plat_layer_surface.link */
	/* Layer surface holding keyboard focus (menus being tracked), or NULL. */
	struct plat_layer_surface *focused_layer;

	struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_mgr;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;
	struct wlr_xdg_decoration_manager_v1 *xdg_decoration_mgr;
	struct wlr_xdg_activation_v1 *xdg_activation;
	struct wl_listener request_activate;
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
	struct wl_list pending_launches;
	struct wl_event_source *launch_timer;
	bool launch_cursor_shown;
	struct wlr_surface *client_cursor_surface;
	struct wl_listener client_cursor_destroy;
	struct wl_listener cursor_client_destroy;
	struct wlr_seat_client *cursor_client;
	const char *client_cursor_name;
	int cursor_hotspot_x, cursor_hotspot_y;
	struct wl_listener request_cursor_shape;

	struct wlr_seat *seat;
	struct wl_listener new_input;
	/* Synthetic input: test automation (wlrctl) and menu commands. */
	struct wl_listener new_virtual_pointer;
	struct wl_listener new_virtual_keyboard;
	struct wl_listener request_cursor;
	struct wl_listener request_set_selection;
	struct wl_listener request_start_drag;
	struct wl_listener start_drag;
	struct wl_listener drag_destroy;
	struct wlr_scene_tree *drag_icon; /* follows the pointer during a drag */
	struct wl_list keyboards;

	/* Interactive grab state. */
	enum plat_cursor_mode cursor_mode;
	struct plat_view *grabbed_view;
	double grab_x, grab_y;       /* cursor position at grab start */
	struct wlr_box grab_box;     /* frame box at grab start */
	uint32_t resize_edges;
	enum decor_part grab_part;
	bool grab_moved;
	struct window_sound_state window_sound;

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
	/* Layout-coordinate area left for windows after shell surfaces (the
	 * menu bar) reserve their exclusive zones. */
	struct wlr_box usable_area;
	struct wlr_scene_buffer *startup_buffer;
	struct wl_listener startup_buffer_destroy;
	struct wl_listener frame;
	struct wl_listener snapshot_commit;
	struct wlr_buffer *snapshot_buffer;
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
	void (*set_fullscreen)(struct plat_view *view, bool fullscreen);
	/* The frame moved (X11 clients must be told their new position). */
	void (*moved)(struct plat_view *view);
	void (*close)(struct plat_view *view);
	/* 0 means unconstrained. */
	void (*get_size_limits)(struct plat_view *view,
		int *min_w, int *min_h, int *max_w, int *max_h);
	/* Has a parent window (dialogs, alerts). */
	bool (*has_parent)(struct plat_view *view);
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

	char *app_id;
	bool mapped;
	bool hidden;    /* "Hide <app>" from the Application menu */
	bool collapsed;
	bool zoomed;
	bool fullscreen;
	struct wlr_output *fullscreen_output;
	struct wlr_box unfullscreen;
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
	struct wl_listener set_app_id;

	/* Exposed to the menu bar via wlr-foreign-toplevel-management. */
	struct wlr_foreign_toplevel_handle_v1 *toplevel_handle;
	struct wl_listener handle_request_activate;
	struct wl_listener handle_request_minimize;
	struct wl_listener handle_request_close;
	struct wl_listener handle_request_fullscreen;

	/* Xwayland only. */
	struct wl_listener associate;
	struct wl_listener dissociate;
	struct wl_listener request_configure;
	struct wl_listener set_geometry;
	struct wl_listener request_activate;
};

struct plat_layer_surface {
	struct wl_list link;
	struct plat_server *server;
	struct wlr_layer_surface_v1 *layer_surface;
	struct wlr_scene_layer_surface_v1 *scene;
	struct wlr_scene_tree *popup_tree;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener new_popup;
	struct wl_listener destroy;
	bool mapped;
};

struct plat_keyboard {
	struct wl_list link;
	struct plat_server *server;
	struct wlr_keyboard *wlr_keyboard;
	bool is_virtual; /* scripted keyboards repeat on their own terms */
	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

/* prefs.c: control panel settings the compositor applies */
void prefs_init(struct plat_server *server);
void prefs_apply(struct plat_server *server);
void prefs_write_outputs(struct plat_server *server);
int prefs_repeat_rate(void);
int prefs_repeat_delay(void);

/* output.c */
void output_init(struct plat_server *server);
/* The main display: the one with the menu bar and the desktop icons (the
 * Monitors panel's choice, else the first). NULL with no output at all. */
struct plat_output *output_main(struct plat_server *server);
struct plat_output *output_at(struct plat_server *server, double lx, double ly);
/* Usable area of the output under (lx, ly), or of the first output. */
struct wlr_box output_usable_area(struct plat_server *server, double lx, double ly);

/* layers.c: wlr-layer-shell (menu bar and other shell surfaces) */
void layers_init(struct plat_server *server);
void layers_arrange(struct plat_output *output);
void layers_focus(struct plat_layer_surface *layer);
/* Move ZacOS 9's own shell surfaces (menu bar, desktop) to the main display. */
void layers_pin_to_main(struct plat_server *server);

/* input.c */
void input_init(struct plat_server *server);
void input_refresh_cursor(struct plat_server *server);
void input_begin_grab(struct plat_server *server, struct plat_view *view,
		enum plat_cursor_mode mode, uint32_t edges, enum decor_part part);
void input_cancel_grab(struct plat_server *server);
/* Give keyboard focus to a surface, with ⌘ (Super) reported as Ctrl. */
void input_keyboard_enter(struct plat_server *server, struct wlr_surface *surface);

/* view.c: window behaviour shared by every client type */
void view_setup(struct plat_view *view, struct plat_server *server,
		enum plat_view_type type, const struct plat_view_impl *impl);
void view_attach_surface_tree(struct plat_view *view, struct wlr_scene_tree *tree);
void view_handle_map(struct plat_view *view);
void view_handle_unmap(struct plat_view *view);
void view_handle_destroy(struct plat_view *view);
void view_update_frame(struct plat_view *view);
void view_place_new(struct plat_view *view);
/* The near-full-screen frame a new window opens in, inside a usable area. */
struct wlr_box view_default_frame_in(struct wlr_box area);
/* Whether a new window takes that default; if so its frame and content size. */
bool view_default_frame(struct plat_view *view, const char *app_id,
		struct wlr_box *frame, int *content_w, int *content_h);
void view_set_title(struct plat_view *view, const char *title);
void view_set_app_id(struct plat_view *view, const char *app_id);
void view_set_hidden(struct plat_view *view, bool hidden);
/* Frontmost visible window, or NULL. */
struct plat_view *view_topmost(struct plat_server *server);

void view_focus(struct plat_view *view);
/* No window active: the desktop (the Finder) is in front. */
void view_clear_focus(struct plat_server *server);
struct plat_view *view_at(struct plat_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy);
enum decor_part view_part_at(struct plat_view *view, double lx, double ly);
struct wlr_box view_frame_box(struct plat_view *view);
void view_content_pos(struct plat_view *view, int *x, int *y);
void view_move_to(struct plat_view *view, int x, int y);
void view_resize_frame(struct plat_view *view, struct wlr_box frame_box);
void view_close(struct plat_view *view);
void view_toggle_zoom(struct plat_view *view);
void view_set_fullscreen(struct plat_view *view, bool fullscreen, struct wlr_output *output);
void view_refresh_fullscreen(struct plat_server *server);
void view_set_collapsed(struct plat_view *view, bool collapsed);
void view_set_pressed(struct plat_view *view, enum decor_part part);
void view_min_frame_size(struct plat_view *view, int *w, int *h);

/* xdg.c */
void xdg_init(struct plat_server *server);
void xdg_attach_popup(struct plat_server *server, struct wlr_xdg_popup *popup,
	struct wlr_scene_tree *parent_tree);

/* platinum_shell.c: window style hints from Platinum's own programs */
void platinum_shell_init(struct plat_server *server);
/* The hinted decor_style for a surface, or -1. */
int platinum_shell_style_for(struct wlr_surface *surface);
/* A remembered position for a surface's window, if its program gave one. */
bool platinum_shell_position_for(struct wlr_surface *surface, int *x, int *y);
/* Tell the window's program (if it is one of ours) where it now is. */
void platinum_shell_report_position(struct plat_view *view);
/* The front window changed: tell the menu bar whose it is. */
void platinum_shell_focus_changed(struct plat_server *server);
void platinum_shell_launch_ready(struct plat_view *view);
void gtk_shell_init(struct plat_server *server);
void gtk_shell_send_active(struct wl_resource *resource, struct plat_server *server);
void platinum_shell_gtk_changed(struct plat_server *server);

/* xwayland.c */
void xwayland_init(struct plat_server *server);

#endif
