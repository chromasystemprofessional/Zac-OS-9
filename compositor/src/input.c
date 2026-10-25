#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <strings.h>
#include <time.h>

#include "access.h"
#include "fkeys.h"
#include <string.h>
#include <unistd.h>
#include <libinput.h>
#include <wlr/backend/libinput.h>
#include <wlr/interfaces/wlr_keyboard.h>
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
#include "idle_policy.h"
#include "server.h"
#include "settings.h"

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

/* What clients are told of the modifiers: the keyboard's, with Sticky
 * Keys', ⌘ made Ctrl. */
static struct wlr_keyboard_modifiers seat_modifiers(struct plat_server *server,
		struct wlr_surface *surface, const struct wlr_keyboard_modifiers *in) {
	struct wlr_keyboard_modifiers merged = *in;
	access_merge_modifiers(&merged);
	return command_as_ctrl(server, surface, &merged);
}

void input_send_modifiers(struct plat_server *server) {
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (!keyboard || !keyboard->keymap) {
		return;
	}
	struct wlr_keyboard_modifiers mods = seat_modifiers(server,
		server->seat->keyboard_state.focused_surface, &keyboard->modifiers);
	wlr_seat_keyboard_notify_modifiers(server->seat, &mods);
}

void input_keyboard_enter(struct plat_server *server, struct wlr_surface *surface) {
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
	if (!surface || (lock_active(server) && !lock_owns(server, surface))) {
		return; /* locked: the keyboard is the lock's alone */
	}
	if (!keyboard) {
		wlr_seat_keyboard_notify_enter(server->seat, surface, NULL, 0, NULL);
		return;
	}
	struct wlr_keyboard_modifiers mods = seat_modifiers(server, surface, &keyboard->modifiers);
	wlr_seat_keyboard_notify_enter(server->seat, surface, keyboard->keycodes,
		keyboard->num_keycodes, &mods);
}

static void keyboard_modifiers(struct wl_listener *listener, void *data) {
	struct plat_keyboard *kb = wl_container_of(listener, kb, modifiers);
	if (!kb->wlr_keyboard->keymap) {
		return;
	}
	wlr_seat_set_keyboard(kb->server->seat, kb->wlr_keyboard);
	input_send_modifiers(kb->server);
}

/* ---- keyboard layouts ---------------------------------------------------- */

/* Every physical keyboard to the layout in use (the locked xkb group). */
static void apply_group(struct plat_server *server, struct plat_keyboard *only) {
	struct plat_keyboard *kb;
	wl_list_for_each(kb, &server->keyboards, link) {
		if (!kb->is_virtual && kb->wlr_keyboard->keymap && (!only || kb == only)) {
			const struct wlr_keyboard_modifiers m = kb->wlr_keyboard->modifiers;
			wlr_keyboard_notify_modifiers(kb->wlr_keyboard, m.depressed, m.latched, m.locked,
				(uint32_t)server->kb_current);
		}
	}
}

void input_apply_layouts(struct plat_server *server) {
	struct kb_layouts now;
	kb_layouts_load(&now);
	if (server->kb_keymap && kb_layouts_equal(&now, &server->kb_layouts)) {
		return;
	}
	struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = ctx ? kb_layouts_keymap(ctx, &now) : NULL;
	xkb_context_unref(ctx);
	if (!keymap) {
		wlr_log(WLR_ERROR, "keyboard layouts: no keymap");
		return;
	}
	xkb_keymap_unref(server->kb_keymap);
	server->kb_keymap = keymap;
	server->kb_layouts = now;
	char code[64];
	const int remembered = kb_layout_remembered(code, sizeof(code)) ?
		kb_layouts_find(&now, code) : -1;
	server->kb_current = remembered >= 0 &&
		remembered < (int)xkb_keymap_num_layouts(keymap) ? remembered : 0;
	struct plat_keyboard *kb;
	wl_list_for_each(kb, &server->keyboards, link) {
		if (!kb->is_virtual) {
			wlr_keyboard_set_keymap(kb->wlr_keyboard, keymap);
		}
	}
	apply_group(server, NULL);
	platinum_shell_layouts_changed(server);
}

void input_set_layout(struct plat_server *server, int index) {
	if (!server->kb_keymap || index < 0 || index >= (int)xkb_keymap_num_layouts(server->kb_keymap)) {
		return;
	}
	server->kb_current = index;
	apply_group(server, NULL);
	if (index < server->kb_layouts.n) {
		kb_layout_remember(server->kb_layouts.codes[index]);
	}
	platinum_shell_layouts_changed(server);
}

/* ⌘-Space (or ⌘-Option-Space) steps through the layouts, ⇧ backwards
 * (Mac OS 9's keyboard menu shortcut); with one layout it is the
 * program's. */
static bool handle_layout_switch(struct plat_server *server, uint32_t mods, xkb_keysym_t sym) {
	const int n = server->kb_keymap ? (int)xkb_keymap_num_layouts(server->kb_keymap) : 0;
	if (sym != XKB_KEY_space || n < 2 || !(mods & WLR_MODIFIER_LOGO) ||
			(mods & WLR_MODIFIER_CTRL) ||
			!(mods & WLR_MODIFIER_ALT) != !prefs_layout_switch_option()) {
		return false;
	}
	input_set_layout(server, (server->kb_current + ((mods & WLR_MODIFIER_SHIFT) ? n - 1 : 1)) % n);
	return true;
}

/* An F-key's item: a running application comes forward, as from the
 * Application menu; anything else is opened. */
static void fkey_open(struct plat_server *server, const char *target) {
	char app_id[256];
	if (fkey_app_id(target, app_id, sizeof(app_id))) {
		struct plat_view *view;
		wl_list_for_each(view, &server->views, link) {
			if (view->mapped && view->app_id && strcasecmp(view->app_id, app_id) == 0) {
				view_activate_app(server, view->app_id);
				return;
			}
		}
	}
	char command[2304];
	fkey_command(target, command, sizeof(command));
	spawn(command);
}

/* Ctrl+Alt+F1..F12 on a real screen: switch to another console. */
static bool handle_vt_switch(struct plat_server *server, xkb_keysym_t sym) {
	if (sym < XKB_KEY_XF86Switch_VT_1 || sym > XKB_KEY_XF86Switch_VT_12) {
		return false;
	}
	if (server->session) {
		input_cancel_grab(server);
		wlr_session_change_vt(server->session, sym - XKB_KEY_XF86Switch_VT_1 + 1);
	}
	return true;
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
	if (handle_vt_switch(server, sym)) {
		return true;
	}
	/* ⌘-Tab and ⇧⌘-Tab switch programs (Mac OS 9: alphabetically). */
	if (sym == XKB_KEY_Tab || sym == XKB_KEY_ISO_Left_Tab) {
		const uint32_t others = WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT;
		if ((mods & WLR_MODIFIER_LOGO) && !(mods & others)) {
			platinum_shell_switch_app(server, (mods & WLR_MODIFIER_SHIFT) ? -1 : 1);
			return true;
		}
	}
	if (handle_layout_switch(server, mods, sym)) {
		return true;
	}
	/* Zoom: ⌘-Option-K, ⌘-Option-Up and Down. */
	if (access_keybinding(server, mods, sym)) {
		return true;
	}
	/* Function keys that open things (the Keyboard panel's). */
	char target[1024];
	if (fkey_target(mods & ~(uint32_t)(WLR_MODIFIER_CAPS | WLR_MODIFIER_MOD2), /* Num Lock */
			WLR_MODIFIER_LOGO, sym, target, sizeof(target))) {
		fkey_open(server, target);
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

	/* A key that only woke a dark screen goes no further, nor its release. */
	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		if (idle_input(server, IDLE_INPUT_KEY)) {
			server->idle_swallowed_key = event->keycode + 1;
			return;
		}
	} else if (server->idle_swallowed_key == event->keycode + 1) {
		server->idle_swallowed_key = 0;
		return;
	}
	/* Easy Access first: Mouse Keys' keypad, a Slow Keys key not yet
	 * held long enough, Sticky Keys' modifier taps. */
	if (access_key(kb, event->keycode, event->state, event->time_msec)) {
		return;
	}
	input_deliver_key(kb, event->keycode, event->state, event->time_msec);
}

void input_deliver_key(struct plat_keyboard *kb, uint32_t evdev_keycode,
		enum wl_keyboard_key_state state, uint32_t time_msec) {
	struct plat_server *server = kb->server;
	uint32_t keycode = evdev_keycode + 8;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(kb->wlr_keyboard->xkb_state, keycode, &syms);
	uint32_t mods = wlr_keyboard_get_modifiers(kb->wlr_keyboard) | access_held_modifiers();

	bool handled = false;
	if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		/* Space during a drag springs the folder under it open now
		 * (the drag's grab keeps keys from programs otherwise). */
		for (int i = 0; i < nsyms && !handled && server->seat->drag && !lock_active(server); i++) {
			handled = syms[i] == XKB_KEY_space && platinum_shell_drag_spring(server->seat->drag);
		}
		/* Locked, no shortcuts but for a text console. */
		for (int i = 0; i < nsyms && !handled; i++) {
			handled = lock_active(server) ? handle_vt_switch(server, syms[i]) :
				handle_keybinding(server, mods, syms[i]);
		}
	}
	if (!handled) {
		wlr_seat_set_keyboard(server->seat, kb->wlr_keyboard);
		wlr_seat_keyboard_notify_key(server->seat, time_msec, evdev_keycode, state);
	}
	access_key_delivered(kb, evdev_keycode, state);
}

static void keyboard_destroy(struct wl_listener *listener, void *data) {
	struct plat_keyboard *kb = wl_container_of(listener, kb, destroy);
	struct plat_server *server = kb->server;
	access_keyboard_gone(kb);
	wl_list_remove(&kb->keymap.link);
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
	osk_update(server);
}

/* A virtual keyboard's keymap came: the first keyboard of a session that
 * had none (headless, or a screen-sharing host with no keyboard of its
 * own, typed on through wayvnc) becomes the seat's keyboard now, and the
 * window that has the focus is told so again: programs set up their
 * keyboard when it appeared, before there was one to give them, and
 * wlroots tells them of the focus only then. Otherwise their keys would
 * go nowhere. */
static void keyboard_keymap(struct wl_listener *listener, void *data) {
	struct plat_keyboard *kb = wl_container_of(listener, kb, keymap);
	struct plat_server *server = kb->server;
	if (!kb->wlr_keyboard->keymap || wlr_seat_get_keyboard(server->seat)) {
		return;
	}
	wlr_seat_set_keyboard(server->seat, kb->wlr_keyboard);
	struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
	if (focused) {
		wlr_seat_keyboard_clear_focus(server->seat);
		input_keyboard_enter(server, focused);
	}
}

/* Virtual keyboards bring their own keymap; physical ones get the layouts. */
static void new_keyboard(struct plat_server *server, struct wlr_input_device *device,
		bool virtual) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

	struct plat_keyboard *kb = calloc(1, sizeof(*kb));
	kb->server = server;
	kb->wlr_keyboard = wlr_keyboard;
	kb->is_virtual = virtual;
	kb->is_real = !virtual && osk_real_keyboard(device);

	if (!virtual) {
		input_apply_layouts(server);
		if (server->kb_keymap) {
			wlr_keyboard_set_keymap(wlr_keyboard, server->kb_keymap);
		}
		wlr_keyboard_set_repeat_info(wlr_keyboard, prefs_repeat_rate(), prefs_repeat_delay());
	}

	kb->modifiers.notify = keyboard_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &kb->modifiers);
	kb->key.notify = keyboard_key;
	wl_signal_add(&wlr_keyboard->events.key, &kb->key);
	wl_list_init(&kb->keymap.link);
	if (virtual) {
		kb->keymap.notify = keyboard_keymap;
		wl_signal_add(&wlr_keyboard->events.keymap, &kb->keymap);
	}
	kb->destroy.notify = keyboard_destroy;
	wl_signal_add(&device->events.destroy, &kb->destroy);

	/* A virtual keyboard has no keymap yet: it becomes the seat's keyboard
	 * when it first sends a key. */
	if (!virtual) {
		wlr_seat_set_keyboard(server->seat, wlr_keyboard);
	}
	wl_list_insert(&server->keyboards, &kb->link);
	if (!virtual) {
		apply_group(server, kb);
	}
	osk_update(server);
}

/* ---- trackpads ---------------------------------------------------------- */

/* The Trackpad panel's "Use Trackpad for": tapping to click, to drag, and
 * drag lock (libinput's tap settings); unset leaves libinput's default. */
struct plat_pointer {
	struct wl_list link;
	struct wlr_input_device *device;
	struct wl_listener destroy;
};

static void pointer_destroy(struct wl_listener *listener, void *data) {
	struct plat_pointer *p = wl_container_of(listener, p, destroy);
	wl_list_remove(&p->link);
	wl_list_remove(&p->destroy.link);
	free(p);
}

static void apply_trackpad(struct wlr_input_device *device) {
	if (!wlr_input_device_is_libinput(device)) {
		return;
	}
	struct libinput_device *li = wlr_libinput_get_device_handle(device);
	if (!li || libinput_device_config_tap_get_finger_count(li) == 0) {
		return; /* a mouse, not a trackpad */
	}
	bool on;
	if (prefs_flag("trackpad-tap", &on)) {
		libinput_device_config_tap_set_enabled(li,
			on ? LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED);
	}
	if (prefs_flag("trackpad-drag", &on)) {
		libinput_device_config_tap_set_drag_enabled(li,
			on ? LIBINPUT_CONFIG_DRAG_ENABLED : LIBINPUT_CONFIG_DRAG_DISABLED);
	}
	if (prefs_flag("trackpad-drag-lock", &on)) {
		libinput_device_config_tap_set_drag_lock_enabled(li,
			on ? LIBINPUT_CONFIG_DRAG_LOCK_ENABLED : LIBINPUT_CONFIG_DRAG_LOCK_DISABLED);
	}
}

void input_apply_trackpads(struct plat_server *server) {
	struct plat_pointer *p;
	wl_list_for_each(p, &server->pointers, link) {
		apply_trackpad(p->device);
	}
}

static void server_new_input(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		new_keyboard(server, device, false);
		break;
	case WLR_INPUT_DEVICE_POINTER: {
		wlr_cursor_attach_input_device(server->cursor, device);
		struct plat_pointer *p = calloc(1, sizeof(*p));
		p->device = device;
		p->destroy.notify = pointer_destroy;
		wl_signal_add(&device->events.destroy, &p->destroy);
		wl_list_insert(&server->pointers, &p->link);
		apply_trackpad(device);
		break;
	}
	case WLR_INPUT_DEVICE_TABLET:
		tablet_new(server, device);
		break;
	case WLR_INPUT_DEVICE_TOUCH:
		touch_new(server, device);
		break;
	default:
		break;
	}
	input_update_capabilities(server);
}

void input_update_capabilities(struct plat_server *server) {
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	if (!wl_list_empty(&server->touchscreens)) {
		caps |= WL_SEAT_CAPABILITY_TOUCH;
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
	input_update_capabilities(server);
}

static bool option_held(struct plat_server *server) {
	struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
	return kb && ((wlr_keyboard_get_modifiers(kb) | access_held_modifiers()) & WLR_MODIFIER_ALT);
}

static bool command_held(struct plat_server *server) {
	struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
	return kb && ((wlr_keyboard_get_modifiers(kb) | access_held_modifiers()) & WLR_MODIFIER_LOGO);
}

/* Option-click on a box acts on every window of the same program. */
static bool same_app(const struct plat_view *a, const struct plat_view *b) {
	return a == b || (a->app_id && b->app_id && *a->app_id && strcmp(a->app_id, b->app_id) == 0);
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

/* ---- proxy icons and spring-open windows --------------------------------- */

/* How far a pop-up's tab is dragged up before it becomes a window. */
#define POPUP_DETACH_DY 40

static uint32_t now_msec(void) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint32_t)(now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

static void proxy_done(struct plat_server *server) {
	if (server->proxy_surface) {
		wl_list_remove(&server->proxy_surface_destroy.link);
		server->proxy_surface = NULL;
	}
}

static void proxy_surface_destroy(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, proxy_surface_destroy);
	proxy_done(server);
}

static void end_grab(struct plat_server *server);

/* The proxy icon dragged: the press is handed to the window just above its
 * content (only a press it was given lets a program start a drag), and
 * the program is told to drag what the window shows. The pointer stays
 * with that window until the drag starts or the button comes up. */
static void proxy_hand_off(struct plat_server *server) {
	struct plat_view *view = server->grabbed_view;
	end_grab(server);
	struct wlr_surface *surface = view ? view->impl->get_surface(view) : NULL;
	if (!surface) {
		return;
	}
	int cx, cy;
	view_content_pos(view, &cx, &cy);
	wlr_seat_pointer_notify_enter(server->seat, surface, server->cursor->x - cx, server->cursor->y - cy);
	wlr_seat_pointer_notify_button(server->seat, now_msec(), BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
	wlr_seat_pointer_notify_frame(server->seat);
	server->proxy_surface = surface;
	server->proxy_cx = cx;
	server->proxy_cy = cy;
	server->proxy_surface_destroy.notify = proxy_surface_destroy;
	wl_signal_add(&surface->events.destroy, &server->proxy_surface_destroy);
	if (!platinum_shell_proxy_drag(view)) {
		wlr_seat_pointer_notify_button(server->seat, now_msec(), BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
		proxy_done(server);
	}
}

static bool view_still_there(struct plat_server *server, struct plat_view *view) {
	struct plat_view *v;
	wl_list_for_each(v, &server->views, link) {
		if (v == view && v->mapped) {
			return true;
		}
	}
	return false;
}

/* A drag resting on a collapsed window's title bar opens it, as a
 * spring-loaded folder opens: the icons can then be dropped in it (the
 * Mac's drop on a collapsed window's proxy icon). It collapses again when
 * the drag is over. */
static int spring_fire(void *data) {
	struct plat_server *server = data;
	struct plat_view *view = server->spring_view;
	server->spring_view = NULL;
	if (view && view_still_there(server, view) && view->collapsed && server->seat->drag) {
		view_set_collapsed(view, false);
		server->sprung_view = view;
	}
	return 0;
}

static void spring_track(struct plat_server *server, struct plat_view *view, struct wlr_surface *surface) {
	const bool over = view && !surface && view->collapsed && server->seat->drag &&
		server->cursor->y - view_frame_box(view).y < DECOR_TOP;
	if (!over) {
		if (server->spring_view) {
			server->spring_view = NULL;
			wl_event_source_timer_update(server->spring_timer, 0);
		}
		return;
	}
	if (server->spring_view != view) {
		server->spring_view = view;
		wl_event_source_timer_update(server->spring_timer, 600);
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
	case PLAT_CURSOR_PROXY:
		if (fabs(server->cursor->x - server->grab_x) + fabs(server->cursor->y - server->grab_y) >= 4) {
			proxy_hand_off(server);
		}
		break;
	case PLAT_CURSOR_POPUP_TAB: {
		const double dx = server->cursor->x - server->grab_x;
		const double dy = server->cursor->y - server->grab_y;
		if (!server->grab_moved && fabs(dx) + fabs(dy) < 4) {
			break;
		}
		server->grab_moved = true;
		if (dy < -POPUP_DETACH_DY) {
			/* Dragged up off the bottom of the screen: an ordinary window
			 * again, carried on being moved by its title bar. */
			view_popup_detach(view);
			server->cursor_mode = PLAT_CURSOR_MOVE;
			server->grab_part = DECOR_PART_DRAG;
			server->grab_box = view_frame_box(view);
			server->grab_x = server->cursor->x;
			server->grab_y = server->cursor->y;
			outline_show(&server->outline, server->grab_box);
			break;
		}
		view->popup_tab_x = server->grab_tab_x + (int)dx;
		view_popup_layout(view);
		break;
	}
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
	case PLAT_CURSOR_POPUP_TAB:
		if (server->grab_moved) {
			platinum_shell_popup_tab_moved(view);
		} else if (view->collapsed) {
			view_focus(view); /* opens it */
		} else {
			view_set_collapsed(view, true);
			if (server->focused_view == view) {
				view_clear_focus(server);
			}
		}
		break;
	case PLAT_CURSOR_TRACK_BOX: {
		enum decor_part part = server->grab_part;
		bool inside = view_part_at(view, server->cursor->x, server->cursor->y) == part;
		view_set_pressed(view, DECOR_PART_NONE);
		if (!inside) {
			break;
		}
		const bool option = option_held(server);
		struct plat_view *v, *tmp;
		if (part == DECOR_PART_CLOSE) {
			/* Option-click closes all of the program's windows. */
			if (option) {
				wl_list_for_each_safe(v, tmp, &server->views, link) {
					if (v->mapped && same_app(v, view)) {
						view_close(v);
					}
				}
			} else {
				view_close(view);
			}
		} else if (part == DECOR_PART_ZOOM) {
			/* Option-click fills the screen, whatever the program's size. */
			view_toggle_zoom(view, option);
		} else if (part == DECOR_PART_COLLAPSE) {
			bool collapse = !view->collapsed;
			if (option) {
				/* Option-click collapses (or expands) all of the program's
				 * windows (Mac OS 9: "all windows in the current program"). */
				wl_list_for_each(v, &server->views, link) {
					if (v->mapped && same_app(v, view)) {
						view_set_collapsed(v, collapse);
					}
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
	/* A pop-up window's tab: a click opens or closes it, a drag along the
	 * bottom of the screen moves it, a drag up makes it a window again. */
	if (part == DECOR_PART_TAB) {
		input_begin_grab(server, view, PLAT_CURSOR_POPUP_TAB, 0, part);
		server->grab_tab_x = view->popup_tab_x;
		return;
	}
	/* A press on the proxy icon: a click, or a drag of the folder itself. */
	if (part == DECOR_PART_DRAG && !command_held(server)) {
		const struct wlr_box frame = view_frame_box(view);
		if (decor_proxy_hit(&view->frame->st, (int)server->cursor->x - frame.x,
				(int)server->cursor->y - frame.y)) {
			view_focus(view);
			input_begin_grab(server, view, PLAT_CURSOR_PROXY, 0, part);
			return;
		}
	}
	/* ⌘-click on the title text of a window whose program asked (the
	 * Finder's folder-path menu): the program shows it; the button stays
	 * with the menu. */
	if (part == DECOR_PART_DRAG && command_held(server)) {
		const struct wlr_box frame = view_frame_box(view);
		if (decor_title_hit(&view->frame->st, (int)server->cursor->x - frame.x,
				(int)server->cursor->y - frame.y) &&
				platinum_shell_title_menu(view, option_held(server))) {
			view_focus(view);
			return;
		}
	}
	/* ⌘-drag a window behind by its title bar or edge: it moves (or, with
	 * a double-click, collapses) where it is, staying behind. */
	if (!(part == DECOR_PART_DRAG && command_held(server) && view != server->focused_view)) {
		view_focus(view);
	}

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
	case DECOR_PART_GROW_TOP_LEFT:
	case DECOR_PART_GROW_TOP_RIGHT:
		/* A pop-up stands on the screen's bottom: it grows up and out. */
		input_begin_grab(server, view, PLAT_CURSOR_RESIZE, WLR_EDGE_TOP |
			(part == DECOR_PART_GROW_TOP_LEFT ? WLR_EDGE_LEFT : WLR_EDGE_RIGHT), part);
		break;
	case DECOR_PART_DRAG: {
		/* Title-bar double-click collapses document windows only. */
		bool in_title = server->cursor->y - view_frame_box(view).y < DECOR_TOP - 2 &&
			view->frame->st.style == DECOR_STYLE_DOCUMENT;
		bool dbl = in_title && prefs_title_collapse() && server->last_click_view == view &&
			time_msec - server->last_click_msec <= (uint32_t)server->double_click_ms;
		if (dbl) {
			/* Appearance option "double-click title bar to collapse". */
			server->last_click_view = NULL;
			view_set_collapsed(view, !view->collapsed);
			break;
		}
		server->last_click_view = in_title && prefs_title_collapse() ? view : NULL;
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
			/* The Mouse panel's Thick I-Beam, for programs that ask for
			 * the text cursor by name (cursor-shape-v1). */
			const bool text = strcmp(server->client_cursor_name, "text") == 0;
			wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr,
				text && server->thick_ibeam ? "text-thick" : server->client_cursor_name);
		} else {
			wlr_cursor_set_surface(server->cursor, server->client_cursor_surface,
				server->cursor_hotspot_x, server->cursor_hotspot_y);
		}
	} else {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}
}

/* No pointer over a screen saver (locked or not, with or without the
 * saver's own say); the usual one elsewhere. */
static void refresh_cursor_or_hide(struct plat_server *server) {
	struct plat_output *under = output_at(server, server->cursor->x, server->cursor->y);
	if (under && under->idle_state == IDLE_SAVER) {
		wlr_cursor_unset_image(server->cursor);
	} else {
		input_refresh_cursor(server);
	}
}

struct surface_find {
	struct wlr_surface *surface;
	bool found;
	int x, y;
};

static void find_surface_buffer(struct wlr_scene_buffer *buffer, int sx, int sy, void *data) {
	struct surface_find *find = data;
	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(buffer);
	if (!find->found && scene_surface && scene_surface->surface == find->surface) {
		find->found = true;
		find->x = sx;
		find->y = sy;
	}
}

/* Wayland's implicit grab: while a button pressed on a program's surface is
 * down, that surface keeps the pointer, off its edge too, and gets the release.
 * The Collar moves under the pointer when ⌘-dragged; without this a quick drag
 * left it behind, so its move (and drag sound) never ended. Popup grabs and
 * drag-and-drop have their own seat grabs and go on as before. */
static bool implicit_grab_motion(struct plat_server *server, uint32_t time) {
	struct wlr_seat_pointer_state *state = &server->seat->pointer_state;
	if (state->button_count == 0 || !state->focused_surface || state->grab != state->default_grab) {
		return false;
	}
	/* Where the surface is now: it may have moved since the press. */
	struct surface_find find = { .surface = state->focused_surface };
	wlr_scene_node_for_each_buffer(&server->scene->tree.node, find_surface_buffer, &find);
	if (!find.found) {
		return false;
	}
	int scale = xwayland_surface_scale(find.surface);
	wlr_seat_pointer_notify_motion(server->seat, time,
		(server->cursor->x - find.x) * scale, (server->cursor->y - find.y) * scale);
	return true;
}

static void process_cursor_motion(struct plat_server *server, uint32_t time) {
	tracks_moved(server);
	if (access_zoom_factor() > 1.0) {
		/* Zoom follows the pointer. */
		struct plat_output *output;
		wl_list_for_each(output, &server->outputs, link) {
			wlr_output_schedule_frame(output->wlr_output);
		}
	}
	if (server->drag_icon) {
		wlr_scene_node_set_position(&server->drag_icon->node,
			(int)server->cursor->x, (int)server->cursor->y);
	}
	if (server->cursor_mode != PLAT_CURSOR_PASSTHROUGH) {
		grab_motion(server);
		return;
	}
	if (server->proxy_surface && !server->seat->drag) {
		/* Handing a proxy drag on: the pointer is that window's for now. */
		wlr_seat_pointer_notify_motion(server->seat, time, server->cursor->x - server->proxy_cx,
			server->cursor->y - server->proxy_cy);
		return;
	}

	double sx, sy;
	struct wlr_surface *surface = NULL;
	if (lock_active(server)) {
		/* Locked: only the lock's surfaces get the pointer. */
		surface = lock_surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
		if (surface) {
			wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
			wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
		} else {
			wlr_seat_pointer_clear_focus(server->seat);
		}
		refresh_cursor_or_hide(server);
		return;
	}
	if (implicit_grab_motion(server, time)) {
		refresh_cursor_or_hide(server);
		return;
	}
	struct plat_view *under = view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	spring_track(server, under, surface);
	if (surface) {
		wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
	} else {
		/* Desktop or window frame: the compositor owns the arrow. */
		wlr_seat_pointer_clear_focus(server->seat);
	}
	refresh_cursor_or_hide(server);
}

/* The pointer moved (a tablet's pen, too: tablet.c). */
void input_pointer_motion(struct plat_server *server, uint32_t time) {
	process_cursor_motion(server, time);
}

void input_pointer_refocus(struct plat_server *server) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	process_cursor_motion(server, (uint32_t)(now.tv_sec * 1000 + now.tv_nsec / 1000000));
}

static void cursor_motion(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	/* Mouse tracking speed (the Mouse control panel). */
	wlr_cursor_move(server->cursor, &event->pointer->base,
		event->delta_x * server->pointer_speed, event->delta_y * server->pointer_speed);
	idle_input(server, IDLE_INPUT_MOTION);
	process_cursor_motion(server, event->time_msec);
}

static void cursor_motion_absolute(struct wl_listener *listener, void *data) {
	struct plat_server *server =
		wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
	idle_input(server, IDLE_INPUT_MOTION);
	process_cursor_motion(server, event->time_msec);
}

static void cursor_button(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;
	input_pointer_button(server, event->time_msec, event->button, event->state);
}

/* A press's focus: the window, desktop or shell surface under the pointer.
 * True if it's the compositor's (a window frame, or a desktop hidden behind
 * a program), not the client's. */
static bool press_focus(struct plat_server *server, uint32_t time_msec, uint32_t button) {
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
		const bool desktop = layer->namespace &&
			(strcmp(layer->namespace, "zacos9-desktop") == 0 ||
			strcmp(layer->namespace, "desktop-extra") == 0);
		if (desktop && !view_desktop_shown(server)) {
			/* General Controls hid the desktop behind this program:
			 * there is nothing there to click. */
			server->swallowed_button = button;
			return true;
		}
		if (desktop) {
			/* Option-click: the program you leave hides, too. */
			struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
			struct plat_view *left = server->focused_view;
			if (keyboard && (wlr_keyboard_get_modifiers(keyboard) & WLR_MODIFIER_ALT) &&
					left && left->app_id && strcmp(left->app_id, "zacos9-finder") != 0) {
				view_hide_app(server, left->app_id);
			}
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
			frame_press(server, view, part, time_msec);
			return true;
		}
		view_focus(view);
	}
		return false;
}

/* The same, for a tablet's pen (tablet.c). */
bool input_press_focus(struct plat_server *server, uint32_t time_msec) {
	if (lock_active(server)) {
		double sx, sy;
		struct wlr_surface *surface = lock_surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
		if (surface) {
			input_keyboard_enter(server, surface);
		}
		return false;
	}
	return press_focus(server, time_msec, 0);
}

static void pointer_button(struct plat_server *server, uint32_t time_msec, uint32_t button,
		enum wl_pointer_button_state button_state) {
	const struct {
		uint32_t time_msec, button;
		enum wl_pointer_button_state state;
	} ev = { time_msec, button, button_state }, *event = &ev;

	/* A click that only woke a dark screen goes no further, nor its release. */
	if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
		if (idle_input(server, IDLE_INPUT_BUTTON)) {
			server->swallowed_button = event->button;
			return;
		}
	} else if (server->swallowed_button == event->button) {
		server->swallowed_button = 0;
		return;
	}

	if (lock_active(server)) {
		/* Locked: a click goes to the lock under the pointer, nowhere else. */
		if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
			double sx, sy;
			struct wlr_surface *surface =
				lock_surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
			if (surface) {
				input_keyboard_enter(server, surface);
			}
		}
		wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
		return;
	}

	if (server->cursor_mode != PLAT_CURSOR_PASSTHROUGH) {
		/* Grabs are compositor-owned; the client never saw the press. */
		if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
			grab_release(server);
		}
		return;
	}

	if (event->state == WL_POINTER_BUTTON_STATE_PRESSED && press_focus(server, event->time_msec, event->button)) {
		return;
	}
	wlr_seat_pointer_notify_button(server->seat, event->time_msec,
		event->button, event->state);
	if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
		proxy_done(server);
	}
}

void input_pointer_button(struct plat_server *server, uint32_t time_msec, uint32_t button,
		enum wl_pointer_button_state button_state) {
	pointer_button(server, time_msec, button, button_state);
	if (button_state == WL_POINTER_BUTTON_STATE_RELEASED) {
		access_click_done(); /* Sticky Keys: ⌘ then a click is a ⌘-click */
	}
}

static void cursor_axis(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;
	idle_input(server, IDLE_INPUT_MOTION);
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
	struct wlr_touch_point *point;
	if (wlr_seat_validate_pointer_grab_serial(server->seat, event->origin, event->serial)) {
		wlr_seat_start_pointer_drag(server->seat, event->drag, event->serial);
	} else if (wlr_seat_validate_touch_grab_serial(server->seat, event->origin,
			event->serial, &point)) {
		/* A finger's drag in an app that takes touch (touch.c). */
		wlr_seat_start_touch_drag(server->seat, event->drag, event->serial, point);
	} else {
		wlr_data_source_destroy(event->drag->source);
	}
}

static void drag_destroy(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, drag_destroy);
	/* The scene removes the icon's tree itself. */
	server->drag_icon = NULL;
	wl_list_remove(&server->drag_destroy.link);
	/* A window sprung open for the drag collapses again. */
	server->spring_view = NULL;
	wl_event_source_timer_update(server->spring_timer, 0);
	if (server->sprung_view && view_still_there(server, server->sprung_view)) {
		view_set_collapsed(server->sprung_view, true);
	}
	server->sprung_view = NULL;
	input_refresh_cursor(server);
}

static void seat_start_drag(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, start_drag);
	struct wlr_drag *drag = data;
	proxy_done(server); /* a proxy icon's drag has begun */
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
	wl_list_init(&server->pointers);
	server->spring_timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->display),
		spring_fire, server);
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
	tablet_init(server);
	touch_init(server);
	osk_init(server);
	/* There is always the pointer, from the start: a session with no
	 * mouse yet (headless, or one plugged in later) still offers one, so
	 * programs started before it set theirs up. A virtual pointer doesn't
	 * update the seat's capabilities; without this one, programs only
	 * saw a pointer once a keyboard arrived. */
	input_update_capabilities(server);
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
