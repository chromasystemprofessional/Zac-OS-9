#include <stdlib.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/util/edges.h>
#include <xkbcommon/xkbcommon.h>

#include "server.h"

/* ---- keyboard ---------------------------------------------------------- */

/* The Windows/Super key is the Mac's ⌘ key. Linux apps expect Ctrl for
 * the same shortcuts (⌘C = Ctrl+C), so clients see Super as Ctrl. */
static struct wlr_keyboard_modifiers command_as_ctrl(
		const struct wlr_keyboard_modifiers *in) {
	struct wlr_keyboard_modifiers out = *in;
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
	struct wlr_keyboard_modifiers mods = command_as_ctrl(&keyboard->modifiers);
	wlr_seat_keyboard_notify_enter(server->seat, surface, keyboard->keycodes,
		keyboard->num_keycodes, &mods);
}

static void keyboard_modifiers(struct wl_listener *listener, void *data) {
	struct plat_keyboard *kb = wl_container_of(listener, kb, modifiers);
	wlr_seat_set_keyboard(kb->server->seat, kb->wlr_keyboard);
	struct wlr_keyboard_modifiers mods = command_as_ctrl(&kb->wlr_keyboard->modifiers);
	wlr_seat_keyboard_notify_modifiers(kb->server->seat, &mods);
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

/* Virtual keyboards bring their own keymap; physical ones get the default. */
static void new_keyboard(struct plat_server *server, struct wlr_input_device *device,
		bool virtual) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

	struct plat_keyboard *kb = calloc(1, sizeof(*kb));
	kb->server = server;
	kb->wlr_keyboard = wlr_keyboard;

	if (!virtual) {
		struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		struct xkb_keymap *keymap =
			xkb_keymap_new_from_names(ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
		wlr_keyboard_set_keymap(wlr_keyboard, keymap);
		xkb_keymap_unref(keymap);
		xkb_context_unref(ctx);
		wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);
	}

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
	server->cursor_mode = mode;
	server->grabbed_view = view;
	server->grab_x = server->cursor->x;
	server->grab_y = server->cursor->y;
	server->grab_box = view_frame_box(view);
	server->resize_edges = edges;
	server->grab_part = part;
	server->grab_moved = false;
	wlr_seat_pointer_clear_focus(server->seat);
	if (mode == PLAT_CURSOR_TRACK_BOX) {
		view_set_pressed(view, part);
	}
}

static void end_grab(struct plat_server *server) {
	outline_hide(&server->outline);
	server->cursor_mode = PLAT_CURSOR_PASSTHROUGH;
	server->grabbed_view = NULL;
	server->grab_part = DECOR_PART_NONE;
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
	case PLAT_CURSOR_MOVE:
		server->grab_moved = true;
		outline_show(&server->outline, move_box(server));
		break;
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
		bool in_title = server->cursor->y - view_frame_box(view).y < DECOR_TOP - 2;
		bool dbl = in_title && server->last_click_view == view &&
			time_msec - server->last_click_msec <= PLAT_DOUBLE_CLICK_MS;
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
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
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
			/* Shell surfaces (the menu bar) never activate windows. */
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
