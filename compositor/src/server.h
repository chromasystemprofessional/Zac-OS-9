#ifndef ZACOS9_SERVER_H
#define ZACOS9_SERVER_H

#include <stdio.h>
#include <sys/types.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <xkbcommon/xkbcommon.h>
#include "kblayout.h"
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
	PLAT_CURSOR_PROXY,     /* pressed on a proxy icon: a click, or a drag to hand on */
	PLAT_CURSOR_POPUP_TAB, /* pressed on a pop-up window's tab: a click opens or
	                        * closes it, a drag along moves it, up detaches it */
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
	bool mouse_tracks; /* a trail of arrows behind the pointer (tracks.c) */
	bool thick_ibeam;  /* the text cursor, when asked for by name, is thicker */

	/* Scene layers, bottom to top:
	 *   desktop, shell[BACKGROUND], shell[BOTTOM], view_layer, popup_layer,
	 *   shell[TOP] (menu bar), fullscreen_layer, dialog_layer, unmanaged_layer,
	 *   shell[OVERLAY], overlay_layer */
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;
	struct wlr_scene_rect *desktop;
	struct wlr_scene_tree *shell_layers[4]; /* indexed by zwlr_layer_shell_v1_layer */
	struct wlr_scene_tree *view_layer;
	struct wlr_scene_tree *popup_layer; /* Sniffer pop-up windows, above the others */
	struct wlr_scene_tree *unmanaged_layer; /* X11 menus, tooltips */
	struct wlr_scene_tree *fullscreen_layer;
	struct wlr_scene_tree *dialog_layer; /* movable-modal windows above app windows */
	struct wlr_scene_tree *overlay_layer;   /* drag outlines */
	struct wlr_scene_tree *lock_layer;  /* lock.c: the lock, over black */
	struct wlr_scene_tree *saver_layer; /* zacos9-screensaver, even over the lock */
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
	struct wl_global *xwayland_xdg_output;
	struct wl_list xwayland_xdg_outputs; /* zxdg_output_v1 resources */
	struct wl_listener xwayland_layout_change;
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
	struct wl_list pointers; /* plat_pointer.link: real pointing devices */
	/* Keyboard layouts (input.c, kblayout.c): one keymap for every
	 * physical keyboard, a group each, and the group in use. */
	struct kb_layouts kb_layouts;
	struct xkb_keymap *kb_keymap;
	int kb_current;
	/* Graphics tablets (tablet.c): pressure and tilt to apps that take
	 * zwp_tablet_v2, a mouse to every other. */
	struct wlr_tablet_manager_v2 *tablet_manager;
	struct wl_list tablets; /* tablet.c plat_tablet.link */
	struct wl_listener tablet_tool_axis;
	struct wl_listener tablet_tool_proximity;
	struct wl_listener tablet_tool_tip;
	struct wl_listener tablet_tool_button;

	/* Interactive grab state. */
	enum plat_cursor_mode cursor_mode;
	/* A proxy icon's drag being handed to its program: the pointer stays
	 * with this surface until the drag starts or the button comes up. */
	struct wlr_surface *proxy_surface;
	int proxy_cx, proxy_cy; /* its content's place on screen */
	struct wl_listener proxy_surface_destroy;
	/* A drag resting on a collapsed window's title bar springs it open;
	 * it collapses again when the drag ends. */
	struct plat_view *spring_view, *sprung_view;
	struct wl_event_source *spring_timer;
	struct plat_view *grabbed_view;
	double grab_x, grab_y;       /* cursor position at grab start */
	struct wlr_box grab_box;     /* frame box at grab start */
	uint32_t resize_edges;
	enum decor_part grab_part;
	bool grab_moved;
	int grab_tab_x;              /* a pop-up's tab at grab start */
	struct window_sound_state window_sound;

	/* Double-click detection on title bars. */
	uint32_t last_click_msec;
	struct plat_view *last_click_view;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;

	/* idle.c: screen saver, display and system sleep. */
	struct wlr_idle_inhibit_manager_v1 *idle_inhibit;
	struct wl_listener new_idle_inhibitor;
	struct wlr_idle_notifier_v1 *idle_notifier;
	struct wl_event_source *idle_timer;
	long idle_last_input; /* ms, CLOCK_MONOTONIC */
	long idle_slept_at;   /* when the system last went to sleep */
	int idle_reported;    /* screens in the state file */
	pid_t idle_saver_pid; /* zacos9-screensaver, while it runs */
	bool idle_refocus_pending; /* the pointer looked at again shortly */
	struct wl_list keep_awake; /* idle.c keep_awake.link: apps' D-Bus requests */
	bool suspend_after_lock; /* system sleep waits for the lock */

	/* lock.c: ext-session-lock-v1. */
	struct wlr_session_lock_manager_v1 *lock_mgr;
	struct wl_listener lock_new;
	struct wlr_session_lock_v1 *lock; /* the lock client's, while it lives */
	struct wl_listener lock_new_surface, lock_unlock, lock_destroy;
	bool locked; /* stays true if the lock client dies without unlocking */
	struct wl_list lock_surfaces; /* lock.c lock_surface.link */
	struct wl_list lock_waiters;  /* wl_callbacks for platinum_shell_v1.lock */
	struct wl_event_source *lock_restart;
	pid_t lock_pid;
	/* A press that only woke a dark screen; its release is swallowed too. */
	/* A press that went nowhere (it woke the screen, or hit a hidden
	 * desktop): its release goes nowhere either. */
	uint32_t swallowed_button, idle_swallowed_key;
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
	int idle_state; /* enum idle_state (idle_policy.h) */
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
	bool collapsed; /* a pop-up window: closed, only its tab shown */
	/* A Sniffer pop-up window (DECOR_STYLE_POPUP), docked at the bottom of
	 * the screen holding (popup_out_x, popup_out_y). */
	bool popup;
	int popup_tab_x; /* the tab's left edge, layout coordinates */
	int popup_out_x, popup_out_y;
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
	int x11_scale; /* X11 pixels per layout unit (xwayland.c) */
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
bool prefs_collar_hotkey(uint32_t mods, xkb_keysym_t sym);
/* General Controls' "Show Desktop when in background" ("show-desktop"). */
bool prefs_show_desktop(void);
/* Appearance > Options: a double-click on a title bar collapses the
 * window (on unless "title-collapse" is "false"). */
bool prefs_title_collapse(void);
bool prefs_layout_switch_option(void);
/* A true/false setting: false if it isn't set. */
bool prefs_flag(const char *key, bool *on);

/* main.c: run a shell command, in its own session; its pid, or -1. */
pid_t spawn(const char *command);
/* main.c: start a shell component (zacos9-menubar...) with extra arguments. */
pid_t spawn_component(const char *env_var, const char *program, const char *args);

/* output.c */
void output_init(struct plat_server *server);
/* The main display: the one with the menu bar and the desktop icons (the
 * Monitors panel's choice, else the first). NULL with no output at all. */
struct plat_output *output_main(struct plat_server *server);
struct plat_output *output_at(struct plat_server *server, double lx, double ly);
/* Usable area of the output under (lx, ly), or of the first output. */
struct wlr_box output_usable_area(struct plat_server *server, double lx, double ly);

/* idle.c: screen saver, display and system sleep */
enum idle_input { IDLE_INPUT_MOTION, IDLE_INPUT_BUTTON, IDLE_INPUT_KEY };
void idle_init(struct plat_server *server);
/* Input happened (or a screen arrived): every screen wakes. True if this
 * press only woke a dark screen and should reach no one. */
bool idle_input(struct plat_server *server, enum idle_input kind);
/* Something that keeps a screen awake changed (a window went full screen,
 * a screen went away, the settings): look again. */
void idle_update(struct plat_server *server);
/* zacos9-screensaver's surfaces show only on screens in its turn. */
void idle_show_savers(struct plat_server *server);
/* platinum_shell_v1.keep_awake: an app asked over D-Bus to stay awake. */
void idle_keep_awake(struct plat_server *server, struct wl_client *client, uint32_t id,
	uint32_t pid, const char *app_id, uint32_t what);

/* lock.c: locking the screens (ext-session-lock-v1) */
void lock_init(struct plat_server *server);
bool lock_active(struct plat_server *server);
/* "Ask for my password to wake" is on. */
bool lock_required(void);
/* Start zacos9-lock, unless locked already. */
void lock_start(struct plat_server *server);
/* platinum_shell_v1.lock: the callback is done once locked (at once if
 * not to be: `always`, or else only when a password is required). */
void lock_request(struct plat_server *server, struct wl_client *client, uint32_t id, bool always);
/* Whether a surface is one of the lock's; which one is under a point. */
bool lock_owns(struct plat_server *server, struct wlr_surface *surface);
struct wlr_surface *lock_surface_at(struct plat_server *server, double lx, double ly,
	double *sx, double *sy);
/* The keyboard to the lock (the one on the screen under the pointer). */
void lock_focus(struct plat_server *server);
/* idle.c: the lock came or went (the state file says). */
void idle_lock_changed(struct plat_server *server);

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
/* What's under the pointer changed with no motion (a screen saver or the
 * lock came or went): its focus and image again. */
void input_pointer_refocus(struct plat_server *server);
/* Tells the focused client the modifiers again (Sticky Keys changed them). */
void input_send_modifiers(struct plat_server *server);
/* Give keyboard focus to a surface, with ⌘ (Super) reported as Ctrl. */
void input_keyboard_enter(struct plat_server *server, struct wlr_surface *surface);
/* Keyboard layouts: (re)build the keymap if the Keyboard panel's choice
 * changed; use layout `index` (⌘-Space, the keyboard menu). */
void input_apply_layouts(struct plat_server *server);
void input_set_layout(struct plat_server *server, int index);
/* A key, after Easy Access (access.c): to the compositor's shortcuts or the
 * focused client. */
void input_deliver_key(struct plat_keyboard *kb, uint32_t evdev_keycode,
	enum wl_keyboard_key_state state, uint32_t time_msec);
/* A pointer button, as if from a mouse (Mouse Keys). */
void input_pointer_button(struct plat_server *server, uint32_t time_msec, uint32_t button,
	enum wl_pointer_button_state state);
/* The Trackpad panel's settings, to every trackpad. */
void input_apply_trackpads(struct plat_server *server);
/* The pointer moved to server->cursor: grabs, focus, image (no frame). */
void input_pointer_motion(struct plat_server *server, uint32_t time);
/* Mouse-down activation under the pointer. True if it hit a window frame,
 * the compositor's to track, not the client's. */
bool input_press_focus(struct plat_server *server, uint32_t time_msec);

/* tablet.c: graphics tablets (Wacom and the like) */
void tablet_init(struct plat_server *server);
void tablet_new(struct plat_server *server, struct wlr_input_device *device);
/* Put each tablet on its screen (the Tablet panel's choice, else the main
 * display); again whenever the screens or that choice change. */
void tablet_map(struct plat_server *server);
/* "tablet NAME" lines for zacos9-outputs, one per tablet. */
void tablet_write_status(struct plat_server *server, FILE *f);

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
/* Every window of `app_id` forward (front one active), or hidden. */
void view_activate_app(struct plat_server *server, const char *app_id);
void view_close_front_app_window(struct plat_server *server, const char *app_id);
void view_hide_app(struct plat_server *server, const char *app_id);
/* False while General Controls hides the desktop behind another program. */
bool view_desktop_shown(struct plat_server *server);
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
/* The zoom box: to the program's standard size (kept on screen), or with
 * `fill` (Option-click, a client's maximize) the whole screen; again, back. */
void view_toggle_zoom(struct plat_view *view, bool fill);
void view_set_fullscreen(struct plat_view *view, bool fullscreen, struct wlr_output *output);
void view_refresh_fullscreen(struct plat_server *server);
void view_set_collapsed(struct plat_view *view, bool collapsed);
/* Pop-up windows (view.c): dock the window at its screen's bottom, its
 * tab where it was put; close every open one but `except`; turn one into
 * an ordinary window under the pointer (its tab dragged up). */
void view_popup_layout(struct plat_view *view);
void view_popups_close(struct plat_server *server, struct plat_view *except);
void view_popup_detach(struct plat_view *view);
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
/* A pop-up window's remembered tab (layout x), if its program gave one. */
bool platinum_shell_popup_tab_for(struct wlr_surface *surface, int *x);
/* Tell the program its pop-up's tab moved, or that it was dragged up into
 * an ordinary window (which it now is). */
void platinum_shell_popup_tab_moved(struct plat_view *view);
void platinum_shell_popup_detached(struct plat_view *view);
/* A remembered position for a surface's window, if its program gave one. */
bool platinum_shell_position_for(struct wlr_surface *surface, int *x, int *y);
/* The content size the zoom box gives this window, if its program said. */
bool platinum_shell_standard_size_for(struct wlr_surface *surface, int *w, int *h);
/* ⌘-click on a title: tell the program, if it asked (true), with Option. */
bool platinum_shell_title_menu(struct plat_view *view, bool option);
/* Tell the window's program (if it is one of ours) where it now is. */
void platinum_shell_report_position(struct plat_view *view);
/* The front window changed: tell the menu bar whose it is. */
void platinum_shell_focus_changed(struct plat_server *server);
/* ⌘-Tab (1) or ⇧⌘-Tab (-1): ask the menu bar to switch programs. */
void platinum_shell_switch_app(struct plat_server *server, int step);
/* The keyboard layouts or the one in use changed: tell the menu bar. */
void platinum_shell_layouts_changed(struct plat_server *server);
/* Sticky Keys turned on or off, or holds other modifiers. */
void platinum_shell_sticky_changed(struct plat_server *server);
/* Zoom: the part of a width x height screen shown magnified `factor` times
 * about (px, py), so that point stays put. */
void output_zoom_box(int width, int height, double px, double py, double factor, struct wlr_fbox *box);
/* The window's proxy icon (pl_icon_kind + 1, 0 for none), and telling its
 * program it is being dragged (false if it can't be). */
int platinum_shell_proxy_for(struct wlr_surface *surface);
bool platinum_shell_proxy_drag(struct plat_view *view);
/* Mouse Tracks (tracks.c): call after the pointer moves. */
void tracks_init(struct plat_server *server);
void tracks_moved(struct plat_server *server);
/* Space during a drag: tell the program under it to spring its folder
 * open now (false if it can't be told). */
bool platinum_shell_drag_spring(struct wlr_drag *drag);
void platinum_shell_launch_ready(struct plat_view *view);
/* The process behind a window (X11: its _NET_WM_PID), or 0. */
uint32_t view_pid(struct plat_view *view);
void gtk_shell_init(struct plat_server *server);
void gtk_shell_send_active(struct wl_resource *resource, struct plat_server *server);
void platinum_shell_gtk_changed(struct plat_server *server);

/* xwayland.c */
void xwayland_init(struct plat_server *server);
/* X11 pixels per layout unit for an X11 surface; 1 for any other. */
int xwayland_surface_scale(struct wlr_surface *surface);
/* xwayland_output.c: outputs as Xwayland sees them, in pixels. */
void xwayland_output_init(struct plat_server *server);

#endif
