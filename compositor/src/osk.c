/* The on-screen keyboard: shown while a text field has the keyboard focus
 * on a computer with a touchscreen and no keyboard attached (a tablet, a
 * convertible's screen on its own). Not on one with neither: a screen-
 * sharing host or a test session has no keyboard, and no fingers either.
 *
 * Programs say they have a text field through text-input-v3 (Qt, GTK and
 * so Firefox; ZacOS's own fields through panelkit's PanelEdit): it is
 * told which surface has the keyboard focus here, and enables itself
 * while a field there has it. A keyboard is one that isn't virtual and
 * has letter keys (not a power button or a laptop lid's "Video Bus").
 *
 * zacos9-keyboard (shell/keyboard/keyboard.c) does the rest: it is
 * started when the keyboard is wanted and follows
 * $XDG_RUNTIME_DIR/zacos9-keyboard-$WAYLAND_DISPLAY ("shown 1" or
 * "shown 0"), leaving when it says 0. It types through virtual-keyboard,
 * on a layer surface that never takes the keyboard focus, so the field
 * keeps it. Platinum's own programs also say over platinum_shell_v1
 * (text_field) as their focus moves: Qt's text-input stays enabled from
 * a field to a button in the same window. A field losing the focus to
 * another hides it only after a
 * moment (HIDE_DELAY_MS), so going from field to field doesn't flicker.
 * Its hide key just leaves: it is back when a field next takes the focus.
 *
 * desktop.conf's "onscreen-keyboard": unset as above; "true" for every
 * text field, a keyboard or not; "false" never. */

#include <libinput.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/util/log.h>

#include "server.h"

#define HIDE_DELAY_MS 300

struct plat_text_input {
	struct wl_list link; /* plat_server.text_inputs */
	struct plat_server *server;
	struct wlr_text_input_v3 *input;
	struct wl_listener enable;
	struct wl_listener disable;
	struct wl_listener destroy;
};

static void write_state(struct plat_server *server, bool shown) {
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	if (!runtime || !server->socket) {
		return;
	}
	char path[1024], tmp[1100];
	snprintf(path, sizeof(path), "%s/zacos9-keyboard-%s", runtime, server->socket);
	snprintf(tmp, sizeof(tmp), "%s.new", path);
	FILE *f = fopen(tmp, "w");
	if (!f) {
		return;
	}
	fprintf(f, "shown %d\n", shown ? 1 : 0);
	fclose(f);
	rename(tmp, path);
}

bool osk_real_keyboard(struct wlr_input_device *device) {
	if (!wlr_input_device_is_libinput(device)) {
		return true; /* a nested session's: the host's keyboard */
	}
	struct libinput_device *li = wlr_libinput_get_device_handle(device);
	return li && libinput_device_keyboard_has_key(li, KEY_A) == 1;
}

static bool keyboard_attached(struct plat_server *server) {
	struct plat_keyboard *kb;
	wl_list_for_each(kb, &server->keyboards, link) {
		if (kb->is_real) {
			return true;
		}
	}
	return false;
}

/* A field has the focus: as its program says (our own), else the
 * focused surface's text input is enabled. */
static bool field_focused(struct plat_server *server) {
	struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
	const int says = focused ? platinum_shell_text_field(wl_resource_get_client(focused->resource)) : -1;
	if (says >= 0) {
		return says == 1;
	}
	struct plat_text_input *ti;
	wl_list_for_each(ti, &server->text_inputs, link) {
		if (focused && ti->input->focused_surface == focused && ti->input->current_enabled) {
			return true;
		}
	}
	return false;
}

/* `again`: a field has just taken the focus, so a keyboard put away with
 * its hide key comes back. */
static void show(struct plat_server *server, bool shown, bool again) {
	if (server->osk_pid > 0 && waitpid(server->osk_pid, NULL, WNOHANG) == server->osk_pid) {
		server->osk_pid = 0; /* it left */
	}
	if (shown == server->osk_shown && !(shown && again)) {
		return;
	}
	if (shown != server->osk_shown) {
		server->osk_shown = shown;
		write_state(server, shown);
	}
	if (shown && server->osk_pid <= 0) {
		server->osk_pid = spawn_component("ZACOS9_KEYBOARD", "zacos9-keyboard", "");
	}
}

/* After the hide, zacos9-keyboard leaves on its own: waited for here,
 * rather than at the next show, so it doesn't linger as a zombie. */
static int reap(void *data) {
	struct plat_server *server = data;
	if (server->osk_pid <= 0 || server->osk_shown) {
		return 0;
	}
	if (waitpid(server->osk_pid, NULL, WNOHANG) == server->osk_pid) {
		server->osk_pid = 0;
	} else {
		wl_event_source_timer_update(server->osk_reap_timer, 200);
	}
	return 0;
}

static int hide_now(void *data) {
	struct plat_server *server = data;
	show(server, false, false);
	wl_event_source_timer_update(server->osk_reap_timer, 100);
	return 0;
}

static void update(struct plat_server *server, bool again) {
	if (!server->osk_hide_timer) {
		return; /* before osk_init */
	}
	bool on = false;
	const bool set = prefs_flag("onscreen-keyboard", &on);
	const bool wanted = field_focused(server) && !lock_active(server) &&
		(set ? on : !wl_list_empty(&server->touchscreens) && !keyboard_attached(server));
	if (wanted) {
		wl_event_source_timer_update(server->osk_hide_timer, 0);
		show(server, true, again);
	} else if (server->osk_shown) {
		wl_event_source_timer_update(server->osk_hide_timer, HIDE_DELAY_MS);
	}
}

void osk_update(struct plat_server *server) {
	update(server, false);
}

void osk_text_field(struct plat_server *server, bool focused) {
	update(server, focused);
}

static void text_input_enable(struct wl_listener *listener, void *data) {
	struct plat_text_input *ti = wl_container_of(listener, ti, enable);
	update(ti->server, true);
}

static void text_input_disable(struct wl_listener *listener, void *data) {
	struct plat_text_input *ti = wl_container_of(listener, ti, disable);
	osk_update(ti->server);
}

static void text_input_destroy(struct wl_listener *listener, void *data) {
	struct plat_text_input *ti = wl_container_of(listener, ti, destroy);
	struct plat_server *server = ti->server;
	wl_list_remove(&ti->enable.link);
	wl_list_remove(&ti->disable.link);
	wl_list_remove(&ti->destroy.link);
	wl_list_remove(&ti->link);
	free(ti);
	osk_update(server);
}

/* Tell a text input of the focus, if the focus is its program's. */
static void text_input_focus(struct wlr_text_input_v3 *input, struct wlr_surface *surface) {
	if (input->focused_surface == surface) {
		return;
	}
	if (input->focused_surface) {
		wlr_text_input_v3_send_leave(input);
	}
	if (surface && wl_resource_get_client(input->resource) == wl_resource_get_client(surface->resource)) {
		wlr_text_input_v3_send_enter(input, surface);
	}
}

static void new_text_input(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, new_text_input);
	struct wlr_text_input_v3 *input = data;
	if (input->seat != server->seat) {
		return;
	}
	struct plat_text_input *ti = calloc(1, sizeof(*ti));
	ti->server = server;
	ti->input = input;
	ti->enable.notify = text_input_enable;
	wl_signal_add(&input->events.enable, &ti->enable);
	ti->disable.notify = text_input_disable;
	wl_signal_add(&input->events.disable, &ti->disable);
	ti->destroy.notify = text_input_destroy;
	wl_signal_add(&input->events.destroy, &ti->destroy);
	wl_list_insert(&server->text_inputs, &ti->link);
	text_input_focus(input, server->seat->keyboard_state.focused_surface);
}

static void keyboard_focus_change(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, osk_focus_change);
	struct wlr_seat_keyboard_focus_change_event *event = data;
	struct plat_text_input *ti;
	wl_list_for_each(ti, &server->text_inputs, link) {
		text_input_focus(ti->input, event->new_surface);
	}
	update(server, true);
}

void osk_init(struct plat_server *server) {
	wl_list_init(&server->text_inputs);
	server->text_input_manager = wlr_text_input_manager_v3_create(server->display);
	server->new_text_input.notify = new_text_input;
	wl_signal_add(&server->text_input_manager->events.text_input, &server->new_text_input);
	server->osk_focus_change.notify = keyboard_focus_change;
	wl_signal_add(&server->seat->keyboard_state.events.focus_change, &server->osk_focus_change);
	struct wl_event_loop *loop = wl_display_get_event_loop(server->display);
	server->osk_hide_timer = wl_event_loop_add_timer(loop, hide_now, server);
	server->osk_reap_timer = wl_event_loop_add_timer(loop, reap, server);
	write_state(server, false);
}
