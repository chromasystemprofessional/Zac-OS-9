#include <stdlib.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/util/edges.h>
#include <xkbcommon/xkbcommon.h>

#include "server.h"

/* ---- keyboard ---------------------------------------------------------- */

static void keyboard_modifiers(struct wl_listener *listener, void *data) {
	struct plat_keyboard *kb = wl_container_of(listener, kb, modifiers);
	wlr_seat_set_keyboard(kb->server->seat, kb->wlr_keyboard);
	wlr_seat_keyboard_notify_modifiers(kb->server->seat,
		&kb->wlr_keyboard->modifiers);
}

/* Compositor-level shortcuts. Returns true if the key was consumed. */
static bool handle_keybinding(struct plat_server *server, uint32_t mods,
		xkb_keysym_t sym) {
	const uint32_t ctrl_alt = WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT;
	if ((mods & ctrl_alt) == ctrl_alt && sym == XKB_KEY_BackSpace) {
		wl_display_terminate(server->display);
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
	wl_list_remove(&kb->modifiers.link);
	wl_list_remove(&kb->key.link);
	wl_list_remove(&kb->destroy.link);
	wl_list_remove(&kb->link);
	free(kb);
}

static void new_keyboard(struct plat_server *server, struct wlr_input_device *device) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

	struct plat_keyboard *kb = calloc(1, sizeof(*kb));
	kb->server = server;
	kb->wlr_keyboard = wlr_keyboard;

	struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap =
		xkb_keymap_new_from_names(ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
	wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(ctx);
	wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

	kb->modifiers.notify = keyboard_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &kb->modifiers);
	kb->key.notify = keyboard_key;
	wl_signal_add(&wlr_keyboard->events.key, &kb->key);
	kb->destroy.notify = keyboard_destroy;
	wl_signal_add(&device->events.destroy, &kb->destroy);

	wlr_seat_set_keyboard(server->seat, wlr_keyboard);
	wl_list_insert(&server->keyboards, &kb->link);
}

static void server_new_input(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		new_keyboard(server, device);
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

/* ---- pointer ----------------------------------------------------------- */

static void process_move(struct plat_server *server) {
	struct plat_view *view = server->grabbed_view;
	wlr_scene_node_set_position(&view->scene_tree->node,
		server->cursor->x - server->grab_x,
		server->cursor->y - server->grab_y);
}

static void process_resize(struct plat_server *server) {
	struct plat_view *view = server->grabbed_view;
	double border_x = server->cursor->x - server->grab_x;
	double border_y = server->cursor->y - server->grab_y;
	int left = server->grab_geobox.x;
	int right = server->grab_geobox.x + server->grab_geobox.width;
	int top = server->grab_geobox.y;
	int bottom = server->grab_geobox.y + server->grab_geobox.height;

	if (server->resize_edges & WLR_EDGE_TOP) {
		top = border_y;
		if (top >= bottom) {
			top = bottom - 1;
		}
	} else if (server->resize_edges & WLR_EDGE_BOTTOM) {
		bottom = border_y;
		if (bottom <= top) {
			bottom = top + 1;
		}
	}
	if (server->resize_edges & WLR_EDGE_LEFT) {
		left = border_x;
		if (left >= right) {
			left = right - 1;
		}
	} else if (server->resize_edges & WLR_EDGE_RIGHT) {
		right = border_x;
		if (right <= left) {
			right = left + 1;
		}
	}

	struct wlr_box geo;
	wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo);
	wlr_scene_node_set_position(&view->scene_tree->node,
		left - geo.x, top - geo.y);
	wlr_xdg_toplevel_set_size(view->xdg_toplevel, right - left, bottom - top);
}

static void process_cursor_motion(struct plat_server *server, uint32_t time) {
	if (server->cursor_mode == PLAT_CURSOR_MOVE) {
		process_move(server);
		return;
	}
	if (server->cursor_mode == PLAT_CURSOR_RESIZE) {
		process_resize(server);
		return;
	}

	double sx, sy;
	struct wlr_surface *surface = NULL;
	struct plat_view *view = view_at(server, server->cursor->x, server->cursor->y,
		&surface, &sx, &sy);
	if (!view) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}
	if (surface) {
		wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
	} else {
		wlr_seat_pointer_clear_focus(server->seat);
	}
}

static void cursor_motion(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	wlr_cursor_move(server->cursor, &event->pointer->base,
		event->delta_x, event->delta_y);
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

	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		server->cursor_mode = PLAT_CURSOR_PASSTHROUGH;
		server->grabbed_view = NULL;
	} else {
		double sx, sy;
		struct wlr_surface *surface = NULL;
		struct plat_view *view = view_at(server, server->cursor->x,
			server->cursor->y, &surface, &sx, &sy);
		view_focus(view, surface);

		/* Temporary dev aid until Phase 1 draws title bars: Alt-drag moves. */
		struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
		if (view && kb && (wlr_keyboard_get_modifiers(kb) & WLR_MODIFIER_ALT)) {
			view_begin_interactive(view, PLAT_CURSOR_MOVE, 0);
			return;
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

static void seat_request_cursor(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	if (server->seat->pointer_state.focused_client == event->seat_client) {
		wlr_cursor_set_surface(server->cursor, event->surface,
			event->hotspot_x, event->hotspot_y);
	}
}

static void seat_request_set_selection(struct wl_listener *listener, void *data) {
	struct plat_server *server =
		wl_container_of(listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

void input_init(struct plat_server *server) {
	server->cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server->cursor, server->output_layout);
	/* TODO(phase 6): ship an original Platinum-style arrow xcursor theme. */
	server->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
	server->cursor_mode = PLAT_CURSOR_PASSTHROUGH;

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

	server->seat = wlr_seat_create(server->display, "seat0");
	server->request_cursor.notify = seat_request_cursor;
	wl_signal_add(&server->seat->events.request_set_cursor, &server->request_cursor);
	server->request_set_selection.notify = seat_request_set_selection;
	wl_signal_add(&server->seat->events.request_set_selection,
		&server->request_set_selection);
}
