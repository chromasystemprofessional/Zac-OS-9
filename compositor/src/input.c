#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "frame.h"
#include "launch.h"
#include "server.h"

/* ---- keyboard ---------------------------------------------------------- */

/* Clients that take ⌘ (Super) shortcuts themselves: in a terminal Ctrl+C
 * interrupts, so ⌘C must stay ⌘ to copy (foot.ini binds it). */
static bool keeps_command_key(struct plat_server *server, struct wlr_surface *surface) {
	struct plat_view *view = server->focused_view;
	if (!surface || server->focused_layer || !view || !view->app_id ||
			view->impl->get_surface(view) != surface) {
		return false;
	}
	return strcmp(view->app_id, "foot") == 0 || strcmp(view->app_id, "footclient") == 0;
}

/* The Windows/Super key is the Mac's ⌘ key. Linux apps expect Ctrl for
 * the same shortcuts (⌘C = Ctrl+C), so clients see Super as Ctrl. */
static struct wlr_keyboard_modifiers command_as_ctrl(struct plat_server *server,
		struct wlr_surface *surface, const struct wlr_keyboard_modifiers *in) {
	struct wlr_keyboard_modifiers out = *in;
	if (keeps_command_key(server, surface)) {
		return out;
	}
	uint32_t *masks[] = { &out.depressed, &out.latched, &out.locked };
	for (int i = 0; i < 3; i++) {
		if (*masks[i] & WLR_MODIFIER_LOGO) {
			*masks[i] = (*masks[i] & ~WLR_MODIFIER_LOGO) | WLR_MODIFIER_CTRL;
		}
	}
	return out;
}

void input_keyboard_enter(struct plat_server *server, struct wlr_surface *surface) {
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (!surface) {
		return;
	}
	if (!keyboard) {
		wlr_seat_keyboard_notify_enter(server->seat, surface, NULL, 0, NULL);
		return;
	}
	struct wlr_keyboard_modifiers mods =
		command_as_ctrl(server, surface, &keyboard->modifiers);
	wlr_seat_keyboard_notify_enter(server->seat, surface, keyboard->keycodes,
		keyboard->num_keycodes, &mods);
}

static void keyboard_modifiers(struct wl_listener *listener, void *data) {
	struct plat_keyboard *kb = wl_container_of(listener, kb, modifiers);
	if (!kb->wlr_keyboard->keymap) {
		return;
	}
	wlr_seat_set_keyboard(kb->server->seat, kb->wlr_keyboard);
	struct wlr_keyboard_modifiers mods = command_as_ctrl(kb->server,
		kb->server->seat->keyboard_state.focused_surface, &kb->wlr_keyboard->modifiers);
	wlr_seat_keyboard_notify_modifiers(kb->server->seat, &mods);
}

/* Compositor-level shortcuts. Returns true if the key was consumed. */
static bool handle_keybinding(struct plat_server *server, uint32_t mods,
		xkb_keysym_t sym) {
	if (sym == XKB_KEY_Escape && server->grabbed_view) {
		input_cancel_grab(server);
		return true;
	}
	const uint32_t ctrl_alt = WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT;
	if ((mods & ctrl_alt) == ctrl_alt && sym == XKB_KEY_BackSpace) {
		wl_display_terminate(server->display);
		return true;
	}
	/* Ctrl+Alt+F1..F12 on a real screen: switch to another console. */
	if (sym >= XKB_KEY_XF86Switch_VT_1 && sym <= XKB_KEY_XF86Switch_VT_12) {
		if (server->session) {
			input_cancel_grab(server);
			wlr_session_change_vt(server->session, sym - XKB_KEY_XF86Switch_VT_1 + 1);
		}
		return true;
	}
	/* The Collar panel's hot key shows or hides the Collar (if one runs:
	 * --toggle just asks it over D-Bus). */
	if (prefs_collar_hotkey(mods, sym)) {
		spawn_component(NULL, "zacos9-collar", " --toggle");
		return true;
	}
	return false;
}

static void keyboard_key(struct wl_listener *listener, void *data) {
	struct plat_keyboard *kb = wl_container_of(listener, kb, key);
	struct plat_server *server = kb->server;
	struct wlr_keyboard_key_event *event = data;

	uint32_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(kb->wlr_keyboard->xkb_state, keycode, &syms);
	uint32_t mods = wlr_keyboard_get_modifiers(kb->wlr_keyboard);

	bool handled = false;
	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		for (int i = 0; i < nsyms && !handled; i++) {
			handled = handle_keybinding(server, mods, syms[i]);
		}
	}
	if (!handled) {
		wlr_seat_set_keyboard(server->seat, kb->wlr_keyboard);
		wlr_seat_keyboard_notify_key(server->seat, event->time_msec,
			event->keycode, event->state);
	}
}

static void keyboard_destroy(struct wl_listener *listener, void *data) {
	struct plat_keyboard *kb = wl_container_of(listener, kb, destroy);
	struct plat_server *server = kb->server;
	wl_list_remove(&kb->modifiers.link);
	wl_list_remove(&kb->key.link);
	wl_list_remove(&kb->destroy.link);
	wl_list_remove(&kb->link);
	/* A scripted keyboard going away hands the seat back to another
	 * keyboard (with its keymap), so clients never sit without one. */
	if (wlr_seat_get_keyboard(server->seat) == kb->wlr_keyboard) {
		wlr_seat_set_keyboard(server->seat, NULL);
		struct plat_keyboard *other;
		wl_list_for_each(other, &server->keyboards, link) {
			if (other->wlr_keyboard->keymap) {
				wlr_seat_set_keyboard(server->seat, other->wlr_keyboard);
				break;
			}
		}
	}
	free(kb);
}

/* Virtual keyboards bring their own keymap; physical ones get the default. */
static void new_keyboard(struct plat_server *server, struct wlr_input_device *device,
		bool virtual) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

	struct plat_keyboard *kb = calloc(1, sizeof(*kb));
	kb->server = server;
	kb->wlr_keyboard = wlr_keyboard;
	kb->is_virtual = virtual;

	if (!virtual) {
		struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		struct xkb_keymap *keymap =
			xkb_keymap_new_from_names(ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
		wlr_keyboard_set_keymap(wlr_keyboard, keymap);
		xkb_keymap_unref(keymap);
		xkb_context_unref(ctx);
		wlr_keyboard_set_repeat_info(wlr_keyboard, prefs_repeat_rate(), prefs_repeat_delay());
	}

	kb->modifiers.notify = keyboard_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &kb->modifiers);
	kb->key.notify = keyboard_key;
	wl_signal_add(&wlr_keyboard->events.key, &kb->key);
	kb->destroy.notify = keyboard_destroy;
	wl_signal_add(&device->events.destroy, &kb->destroy);

	/* A virtual keyboard has no keymap yet: it becomes the seat's keyboard
	 * when it first sends a key. */
	if (!virtual) {
		wlr_seat_set_keyboard(server->seat, wlr_keyboard);
	}
	wl_list_insert(&server->keyboards, &kb->link);
}

static void server_new_input(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		new_keyboard(server, device, false);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		wlr_cursor_attach_input_device(server->cursor, device);
		break;
	default:
		break;
	}

	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

static void new_virtual_pointer(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, new_virtual_pointer);
	struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
	wlr_cursor_attach_input_device(server->cursor, &event->new_pointer->pointer.base);
	if (event->suggested_output) {
		wlr_cursor_map_input_to_output(server->cursor,
			&event->new_pointer->pointer.base, event->suggested_output);
	}
}

static void new_virtual_keyboard(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, new_virtual_keyboard);
	struct wlr_virtual_keyboard_v1 *vkbd = data;
	new_keyboard(server, &vkbd->keyboard.base, true);
	wlr_seat_set_capabilities(server->seat,
		WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
}

static bool option_held(struct plat_server *server) {
	struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
	return kb && (wlr_keyboard_get_modifiers(kb) & WLR_MODIFIER_ALT);
}

/* ---- grabs: outline move/resize and title-bar box tracking --------------- */

void input_begin_grab(struct plat_server *server, struct plat_view *view,
		enum plat_cursor_mode mode, uint32_t edges, enum decor_part part) {
	if (view->fullscreen) {
		return;
	}
	if (server->grabbed_view) {
		input_cancel_grab(server);
	}
	server->cursor_mode = mode;
	server->grabbed_view = view;
	server->grab_x = server->cursor->x;
	server->grab_y = server->cursor->y;
	server->grab_box = view_frame_box(view);
	server->resize_edges = edges;
	server->grab_part = part;
	server->grab_moved = false;
	wlr_seat_pointer_clear_focus(server->seat);
	input_refresh_cursor(server);
	if (mode == PLAT_CURSOR_TRACK_BOX) {
		view_set_pressed(view, part);
	}
}

static void end_grab(struct plat_server *server) {
	outline_hide(&server->outline);
	server->cursor_mode = PLAT_CURSOR_PASSTHROUGH;
	server->grabbed_view = NULL;
	server->grab_part = DECOR_PART_NONE;
	input_refresh_cursor(server);
}

void input_cancel_grab(struct plat_server *server) {
	window_sound_finish(&server->window_sound, false);
	if (server->grabbed_view && server->cursor_mode == PLAT_CURSOR_TRACK_BOX) {
		view_set_pressed(server->grabbed_view, DECOR_PART_NONE);
	}
	end_grab(server);
}

static struct wlr_box resize_box(struct plat_server *server) {
	struct wlr_box b = server->grab_box;
	int dx = (int)(server->cursor->x - server->grab_x);
	int dy = (int)(server->cursor->y - server->grab_y);
	int min_w, min_h;
	view_min_frame_size(server->grabbed_view, &min_w, &min_h);

	uint32_t e = server->resize_edges;
	if (e & WLR_EDGE_RIGHT) {
		b.width = server->grab_box.width + dx;
	} else if (e & WLR_EDGE_LEFT) {
		b.width = server->grab_box.width - dx;
	}
	if (e & WLR_EDGE_BOTTOM) {
		b.height = server->grab_box.height + dy;
	} else if (e & WLR_EDGE_TOP) {
		b.height = server->grab_box.height - dy;
	}
	if (b.width < min_w) {
		b.width = min_w;
	}
	if (b.height < min_h) {
		b.height = min_h;
	}
	if (e & WLR_EDGE_LEFT) {
		b.x = server->grab_box.x + server->grab_box.width - b.width;
	}
	if (e & WLR_EDGE_TOP) {
		b.y = server->grab_box.y + server->grab_box.height - b.height;
	}
	return b;
}

static struct wlr_box move_box(struct plat_server *server) {
	struct wlr_box b = server->grab_box;
	b.x += (int)(server->cursor->x - server->grab_x);
	b.y += (int)(server->cursor->y - server->grab_y);
	/* Keep the title bar below the menu bar so it can't be lost. */
	struct wlr_box area = output_usable_area(server, server->cursor->x, server->cursor->y);
	if (!wlr_box_empty(&area) && b.y < area.y) {
		b.y = area.y;
	}
	return b;
}

static void grab_motion(struct plat_server *server) {
	struct plat_view *view = server->grabbed_view;
	switch (server->cursor_mode) {
	case PLAT_CURSOR_MOVE: {
		struct wlr_box box = move_box(server);
		if (box.x != server->grab_box.x || box.y != server->grab_box.y) {
			server->grab_moved = true;
			window_sound_move(&server->window_sound);
		}
		outline_show(&server->outline, box);
		break;
	}
	case PLAT_CURSOR_RESIZE:
		server->grab_moved = true;
		outline_show(&server->outline, resize_box(server));
		break;
	case PLAT_CURSOR_TRACK_BOX: {
		/* Mac controls track: highlighted only while the pointer is inside. */
		enum decor_part part = view_part_at(view, server->cursor->x, server->cursor->y);
		view_set_pressed(view, part == server->grab_part ? part : DECOR_PART_NONE);
		break;
	}
	default:
		break;
	}
}

static void grab_release(struct plat_server *server) {
	struct plat_view *view = server->grabbed_view;
	switch (server->cursor_mode) {
	case PLAT_CURSOR_MOVE:
		if (server->grab_moved) {
			struct wlr_box b = move_box(server);
			view_move_to(view, b.x, b.y);
		}
		window_sound_finish(&server->window_sound, server->grab_moved);
		break;
	case PLAT_CURSOR_RESIZE:
		if (server->grab_moved) {
			view->zoomed = false;
			view_resize_frame(view, resize_box(server));
		}
		break;
	case PLAT_CURSOR_TRACK_BOX: {
		enum decor_part part = server->grab_part;
		bool inside = view_part_at(view, server->cursor->x, server->cursor->y) == part;
		view_set_pressed(view, DECOR_PART_NONE);
		if (!inside) {
			break;
		}
		if (part == DECOR_PART_CLOSE) {
			view_close(view);
		} else if (part == DECOR_PART_ZOOM) {
			view_toggle_zoom(view);
		} else if (part == DECOR_PART_COLLAPSE) {
			bool collapse = !view->collapsed;
			if (option_held(server)) {
				/* HIG: Option-click collapses (or expands) every window. */
				struct plat_view *v;
				wl_list_for_each(v, &server->views, link) {
					view_set_collapsed(v, collapse);
				}
			} else {
				view_set_collapsed(view, collapse);
			}
		}
		break;
	}
	default:
		break;
	}
	end_grab(server);
}

/* Mouse-down on a window frame (anything but the client area). */
static void frame_press(struct plat_server *server, struct plat_view *view,
		enum decor_part part, uint32_t time_msec) {
	view_focus(view);

	switch (part) {
	case DECOR_PART_CLOSE:
	case DECOR_PART_ZOOM:
	case DECOR_PART_COLLAPSE:
		input_begin_grab(server, view, PLAT_CURSOR_TRACK_BOX, 0, part);
		break;
	case DECOR_PART_GROW:
		input_begin_grab(server, view, PLAT_CURSOR_RESIZE,
			WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM, part);
		break;
	case DECOR_PART_DRAG: {
		/* Title-bar double-click collapses document windows only. */
		bool in_title = server->cursor->y - view_frame_box(view).y < DECOR_TOP - 2 &&
			view->frame->st.style == DECOR_STYLE_DOCUMENT;
		bool dbl = in_title && server->last_click_view == view &&
			time_msec - server->last_click_msec <= (uint32_t)server->double_click_ms;
		if (dbl) {
			/* Appearance option "double-click title bar to collapse". */
			server->last_click_view = NULL;
			view_set_collapsed(view, !view->collapsed);
			break;
		}
		server->last_click_view = in_title ? view : NULL;
		server->last_click_msec = time_msec;
		input_begin_grab(server, view, PLAT_CURSOR_MOVE, 0, part);
		break;
	}
	default:
		break;
	}
}

/* ---- pointer ----------------------------------------------------------- */

void input_refresh_cursor(struct plat_server *server) {
	bool busy = launch_pending(server) &&
		server->cursor_mode == PLAT_CURSOR_PASSTHROUGH && !server->drag_icon;
	if (busy) {
		if (!server->launch_cursor_shown) {
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "watch");
		}
		server->launch_cursor_shown = true;
		return;
	}
	server->launch_cursor_shown = false;
	if (server->cursor_client &&
			server->cursor_client == server->seat->pointer_state.focused_client) {
		if (server->client_cursor_name) {
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr,
				server->client_cursor_name);
		} else {
			wlr_cursor_set_surface(server->cursor, server->client_cursor_surface,
				server->cursor_hotspot_x, server->cursor_hotspot_y);
		}
	} else {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}
}

static void process_cursor_motion(struct plat_server *server, uint32_t time) {
	if (server->drag_icon) {
		wlr_scene_node_set_position(&server->drag_icon->node,
			(int)server->cursor->x, (int)server->cursor->y);
	}
	if (server->cursor_mode != PLAT_CURSOR_PASSTHROUGH) {
		grab_motion(server);
		return;
	}

	double sx, sy;
	struct wlr_surface *surface = NULL;
	view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	if (surface) {
		wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
	} else {
		/* Desktop or window frame: the compositor owns the arrow. */
		wlr_seat_pointer_clear_focus(server->seat);
	}
	input_refresh_cursor(server);
}

static void cursor_motion(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	/* Mouse tracking speed (the Mouse control panel). */
	wlr_cursor_move(server->cursor, &event->pointer->base,
		event->delta_x * server->pointer_speed, event->delta_y * server->pointer_speed);
	process_cursor_motion(server, event->time_msec);
}

static void cursor_motion_absolute(struct wl_listener *listener, void *data) {
	struct plat_server *server =
		wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
	process_cursor_motion(server, event->time_msec);
}

static void cursor_button(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;

	if (server->cursor_mode != PLAT_CURSOR_PASSTHROUGH) {
		/* Grabs are compositor-owned; the client never saw the press. */
		if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
			grab_release(server);
		}
		return;
	}

	if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
		double sx, sy;
		struct wlr_surface *surface = NULL;
		struct plat_view *view = view_at(server, server->cursor->x,
			server->cursor->y, &surface, &sx, &sy);
		struct wlr_layer_surface_v1 *layer = surface ?
			wlr_layer_surface_v1_try_from_wlr_surface(surface) : NULL;
		if (layer) {
			/* Shell surfaces (the menu bar) never activate windows, but a
			 * click on the desktop (the Finder's, on any display) puts the
			 * Finder in front, its menus in the menu bar. */
			if (layer->namespace && (strcmp(layer->namespace, "zacos9-desktop") == 0 ||
					strcmp(layer->namespace, "desktop-extra") == 0)) {
				view_clear_focus(server);
			}
			if (layer->current.keyboard_interactive !=
					ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
				layers_focus(layer->data);
			}
		} else {
			struct plat_layer_surface *fl = server->focused_layer;
			if (fl && fl->layer_surface->current.keyboard_interactive !=
					ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) {
				server->focused_layer = NULL;
			}
			if (view && !surface) {
				enum decor_part part =
					view_part_at(view, server->cursor->x, server->cursor->y);
				frame_press(server, view, part, event->time_msec);
				return;
			}
			view_focus(view);
		}
	}
	wlr_seat_pointer_notify_button(server->seat, event->time_msec,
		event->button, event->state);
}

static void cursor_axis(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;
	wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
		event->orientation, event->delta, event->delta_discrete,
		event->source, event->relative_direction);
}

static void cursor_frame(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, cursor_frame);
	wlr_seat_pointer_notify_frame(server->seat);
}

/* ---- seat requests ----------------------------------------------------- */

static void client_cursor_destroy(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, client_cursor_destroy);
	wl_list_remove(&server->client_cursor_destroy.link);
	server->client_cursor_surface = NULL;
	input_refresh_cursor(server);
}

static void clear_client_cursor(struct plat_server *server) {
	if (server->client_cursor_surface) {
		wl_list_remove(&server->client_cursor_destroy.link);
		server->client_cursor_surface = NULL;
	}
}

static void cursor_client_destroy(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, cursor_client_destroy);
	wl_list_remove(&server->cursor_client_destroy.link);
	clear_client_cursor(server);
	server->cursor_client = NULL;
	server->client_cursor_name = NULL;
	input_refresh_cursor(server);
}

static void remember_cursor_client(struct plat_server *server, struct wlr_seat_client *client) {
	if (server->cursor_client == client) {
		return;
	}
	if (server->cursor_client) {
		wl_list_remove(&server->cursor_client_destroy.link);
	}
	server->cursor_client = client;
	server->cursor_client_destroy.notify = cursor_client_destroy;
	wl_signal_add(&client->events.destroy, &server->cursor_client_destroy);
}

static void seat_request_cursor(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	if (server->seat->pointer_state.focused_client == event->seat_client) {
		clear_client_cursor(server);
		remember_cursor_client(server, event->seat_client);
		server->client_cursor_name = NULL;
		server->client_cursor_surface = event->surface;
		server->cursor_hotspot_x = event->hotspot_x;
		server->cursor_hotspot_y = event->hotspot_y;
		if (event->surface) {
			server->client_cursor_destroy.notify = client_cursor_destroy;
			wl_signal_add(&event->surface->events.destroy, &server->client_cursor_destroy);
		}
		input_refresh_cursor(server);
	}
}

static void seat_request_cursor_shape(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, request_cursor_shape);
	struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;
	if (event->device_type != WLR_CURSOR_SHAPE_MANAGER_V1_DEVICE_TYPE_POINTER ||
			server->seat->pointer_state.focused_client != event->seat_client) {
		return;
	}
	clear_client_cursor(server);
	remember_cursor_client(server, event->seat_client);
	server->client_cursor_name = wlr_cursor_shape_v1_name(event->shape);
	input_refresh_cursor(server);
}

static void seat_request_set_selection(struct wl_listener *listener, void *data) {
	struct plat_server *server =
		wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

/* ---- drag and drop ------------------------------------------------------ */

static void seat_request_start_drag(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, request_start_drag);
	struct wlr_seat_request_start_drag_event *event = data;
	if (wlr_seat_validate_pointer_grab_serial(server->seat, event->origin, event->serial)) {
		wlr_seat_start_pointer_drag(server->seat, event->drag, event->serial);
	} else {
		wlr_data_source_destroy(event->drag->source);
	}
}

static void drag_destroy(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, drag_destroy);
	/* The scene removes the icon's tree itself. */
	server->drag_icon = NULL;
	wl_list_remove(&server->drag_destroy.link);
	input_refresh_cursor(server);
}

static void seat_start_drag(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, start_drag);
	struct wlr_drag *drag = data;
	if (drag->icon) {
		/* Above windows and menus, like the Mac's drag outlines. */
		server->drag_icon = wlr_scene_drag_icon_create(server->overlay_layer, drag->icon);
		wlr_scene_node_set_position(&server->drag_icon->node,
			(int)server->cursor->x, (int)server->cursor->y);
	}
	server->drag_destroy.notify = drag_destroy;
	wl_signal_add(&drag->events.destroy, &server->drag_destroy);
	input_refresh_cursor(server);
}

/* The ZacOS9 cursor theme (assets/cursors) sits next to our binary in
 * the build tree, or under share/icons when installed. Point Xcursor at
 * it for us and, through the environment, for every client we start. */
static void use_zacos9_cursors(void) {
	char exe[PATH_MAX];
	ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (len > 0) {
		exe[len] = '\0';
		char *slash = strrchr(exe, '/');
		if (slash) {
			*slash = '\0';
			const char *old = getenv("XCURSOR_PATH");
			char path[3 * PATH_MAX];
			snprintf(path, sizeof(path), "%s:%s/../share/icons%s%s", exe, exe,
				old ? ":" : ":~/.local/share/icons:~/.icons:/usr/share/icons:/usr/share/pixmaps",
				old ? old : "");
			setenv("XCURSOR_PATH", path, 1);
		}
	}
	setenv("XCURSOR_THEME", ZACOS9_CURSOR_THEME, 1);
	char size[8];
	snprintf(size, sizeof(size), "%d", ZACOS9_CURSOR_SIZE);
	setenv("XCURSOR_SIZE", size, 1);
}

void input_init(struct plat_server *server) {
	server->cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server->cursor, server->output_layout);
	use_zacos9_cursors();
	server->cursor_mgr = wlr_xcursor_manager_create(ZACOS9_CURSOR_THEME, ZACOS9_CURSOR_SIZE);
	server->cursor_mode = PLAT_CURSOR_PASSTHROUGH;
	launch_init(server);
	struct wlr_cursor_shape_manager_v1 *shapes =
		wlr_cursor_shape_manager_v1_create(server->display, 1);
	if (!shapes) {
		wlr_log(WLR_ERROR, "Could not create cursor-shape manager");
		exit(EXIT_FAILURE);
	}
	server->request_cursor_shape.notify = seat_request_cursor_shape;
	wl_signal_add(&shapes->events.request_set_shape, &server->request_cursor_shape);

	server->cursor_motion.notify = cursor_motion;
	wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);
	server->cursor_motion_absolute.notify = cursor_motion_absolute;
	wl_signal_add(&server->cursor->events.motion_absolute,
		&server->cursor_motion_absolute);
	server->cursor_button.notify = cursor_button;
	wl_signal_add(&server->cursor->events.button, &server->cursor_button);
	server->cursor_axis.notify = cursor_axis;
	wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);
	server->cursor_frame.notify = cursor_frame;
	wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);

	wl_list_init(&server->keyboards);
	server->new_input.notify = server_new_input;
	wl_signal_add(&server->backend->events.new_input, &server->new_input);

	struct wlr_virtual_pointer_manager_v1 *vptr =
		wlr_virtual_pointer_manager_v1_create(server->display);
	server->new_virtual_pointer.notify = new_virtual_pointer;
	wl_signal_add(&vptr->events.new_virtual_pointer, &server->new_virtual_pointer);
	struct wlr_virtual_keyboard_manager_v1 *vkbd =
		wlr_virtual_keyboard_manager_v1_create(server->display);
	server->new_virtual_keyboard.notify = new_virtual_keyboard;
	wl_signal_add(&vkbd->events.new_virtual_keyboard, &server->new_virtual_keyboard);

	server->seat = wlr_seat_create(server->display, "seat0");
	server->request_cursor.notify = seat_request_cursor;
	wl_signal_add(&server->seat->events.request_set_cursor, &server->request_cursor);
	server->request_set_selection.notify = seat_request_set_selection;
	wl_signal_add(&server->seat->events.request_set_selection,
		&server->request_set_selection);
	server->request_start_drag.notify = seat_request_start_drag;
	wl_signal_add(&server->seat->events.request_start_drag, &server->request_start_drag);
	server->start_drag.notify = seat_start_drag;
	wl_signal_add(&server->seat->events.start_drag, &server->start_drag);
}
