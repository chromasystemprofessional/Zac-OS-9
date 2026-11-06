/*
 * Input methods (Fcitx, IBus, ...): the relay between a program's text
 * field (text-input-v3, tracked in osk.c for the on-screen keyboard)
 * and an input method (input-method-v2), which no program talks to
 * directly. The compositor advertises the input-method global; when an
 * input method connects, this file
 *
 *   - tells it when a text field of the focused program is in use
 *     (activate), and passes the field's surrounding text, content type
 *     and cursor rectangle on;
 *   - applies what it answers with -- preedit (the underlined text
 *     being composed), the committed string, surrounding deletions --
 *     to the field;
 *   - gives it the keyboard while it asks (the grab): keys go to the
 *     input method first, and the ones it doesn't use come back on the
 *     input method's own virtual keyboard, to the program as usual;
 *   - puts its candidate window (the popup surface) under the field's
 *     text cursor, in the pop-up layer.
 *
 * One input method for the seat: a second one is told it is
 * unavailable. With none connected, nothing here runs and text fields
 * behave as they always did.
 */
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "server.h"

struct plat_im_popup {
	struct wl_list link; /* plat_input_method.popups */
	struct plat_input_method *im;
	struct wlr_input_popup_surface_v2 *popup;
	struct wlr_scene_tree *tree;
	struct wl_listener destroy;
};

struct plat_input_method {
	struct plat_server *server;
	struct wlr_input_method_v2 *ime;
	struct wl_listener commit;
	struct wl_listener grab_keyboard;
	struct wl_listener new_popup;
	struct wl_listener destroy;
	struct wlr_input_method_keyboard_grab_v2 *grab;
	struct wl_listener grab_destroy;
	struct wl_list popups; /* struct plat_im_popup.link */
};

/* The text input being relayed: the enabled one belonging to the
 * surface that has the keyboard focus. */
static struct plat_text_input *relayed_text_input(struct plat_server *server) {
	struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
	if (!focused) {
		return NULL;
	}
	struct plat_text_input *ti;
	wl_list_for_each(ti, &server->text_inputs, link) {
		if (ti->input->current_enabled && ti->input->focused_surface == focused) {
			return ti;
		}
	}
	return NULL;
}

/* A surface's place in the layout: walk the scene for its buffer. */
struct surface_at {
	struct wlr_surface *surface;
	int x, y;
	bool found;
};

static void find_surface(struct wlr_scene_buffer *buffer, int sx, int sy, void *data) {
	struct surface_at *at = data;
	if (at->found) {
		return;
	}
	const struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(buffer);
	if (scene_surface && scene_surface->surface == at->surface) {
		at->x = sx;
		at->y = sy;
		at->found = true;
	}
}

static bool surface_position(struct plat_server *server, struct wlr_surface *surface,
		int *x, int *y) {
	struct surface_at at = { .surface = surface };
	wlr_scene_node_for_each_buffer(&server->scene->tree.node, find_surface, &at);
	if (at.found) {
		*x = at.x;
		*y = at.y;
	}
	return at.found;
}

/* ---- the candidate window ------------------------------------------------- */

static void popup_place(struct plat_server *server, struct plat_im_popup *popup) {
	struct plat_text_input *ti = server->im_text_input;
	if (!ti || !ti->input->focused_surface) {
		return;
	}
	int fx, fy;
	if (!surface_position(server, ti->input->focused_surface, &fx, &fy)) {
		return;
	}
	const struct wlr_box *rect = &ti->input->current.cursor_rectangle;
	/* Under the text cursor; the input method hears the rectangle in
	 * the field's own coordinates, as the protocol has it. */
	wlr_scene_node_set_position(&popup->tree->node, fx + rect->x, fy + rect->y + rect->height);
	struct wlr_box box = *rect;
	wlr_input_popup_surface_v2_send_text_input_rectangle(popup->popup, &box);
}

static void popup_destroy(struct wl_listener *listener, void *data) {
	struct plat_im_popup *popup = wl_container_of(listener, popup, destroy);
	wl_list_remove(&popup->destroy.link);
	wl_list_remove(&popup->link);
	if (popup->tree) {
		wlr_scene_node_destroy(&popup->tree->node);
	}
	free(popup);
}

static void handle_new_popup(struct wl_listener *listener, void *data) {
	struct plat_input_method *im = wl_container_of(listener, im, new_popup);
	struct wlr_input_popup_surface_v2 *popup_surface = data;
	struct plat_im_popup *popup = calloc(1, sizeof(*popup));
	popup->im = im;
	popup->popup = popup_surface;
	popup->tree = wlr_scene_subsurface_tree_create(im->server->popup_layer, popup_surface->surface);
	popup->destroy.notify = popup_destroy;
	wl_signal_add(&popup_surface->events.destroy, &popup->destroy);
	wl_list_insert(&im->popups, &popup->link);
	popup_place(im->server, popup);
}

/* ---- the keyboard grab ---------------------------------------------------- */

static void grab_gone(struct plat_input_method *im) {
	if (!im->grab) {
		return;
	}
	wl_list_remove(&im->grab_destroy.link);
	im->grab = NULL;
}

static void grab_destroy(struct wl_listener *listener, void *data) {
	struct plat_input_method *im = wl_container_of(listener, im, grab_destroy);
	grab_gone(im);
}

static void handle_grab_keyboard(struct wl_listener *listener, void *data) {
	struct plat_input_method *im = wl_container_of(listener, im, grab_keyboard);
	struct wlr_input_method_keyboard_grab_v2 *grab = data;
	grab_gone(im);
	im->grab = grab;
	im->grab_destroy.notify = grab_destroy;
	wl_signal_add(&grab->events.destroy, &im->grab_destroy);
	struct wlr_keyboard *seat_keyboard = wlr_seat_get_keyboard(im->server->seat);
	if (seat_keyboard) {
		wlr_input_method_keyboard_grab_v2_set_keyboard(grab, seat_keyboard);
	}
}

/* Keys the input method itself types, on its own virtual keyboard, are
 * the ones it chose not to use (or composed for the program): those go
 * to the program, not back into the grab. */
static bool from_input_method(struct plat_input_method *im, struct wlr_keyboard *keyboard) {
	struct wlr_virtual_keyboard_v1 *vkbd =
		wlr_input_device_get_virtual_keyboard(&keyboard->base);
	return vkbd && wl_resource_get_client(vkbd->resource) ==
		wl_resource_get_client(im->ime->resource);
}

/* Keys on their way to a program pass the input method first while it
 * holds the grab; input.c asks before delivering to the seat. */
bool input_method_key(struct plat_server *server, struct wlr_keyboard *keyboard,
		uint32_t evdev_keycode, enum wl_keyboard_key_state state, uint32_t time_msec) {
	struct plat_input_method *im = server->input_method;
	if (!im || !im->grab || !im->ime->active || !server->im_text_input ||
			from_input_method(im, keyboard)) {
		return false;
	}
	wlr_input_method_keyboard_grab_v2_set_keyboard(im->grab, keyboard);
	wlr_input_method_keyboard_grab_v2_send_key(im->grab, time_msec, evdev_keycode, state);
	return true;
}

void input_method_modifiers(struct plat_server *server, struct wlr_keyboard *keyboard) {
	struct plat_input_method *im = server->input_method;
	if (!im || !im->grab || !im->ime->active || !server->im_text_input ||
			from_input_method(im, keyboard)) {
		return;
	}
	wlr_input_method_keyboard_grab_v2_send_modifiers(im->grab, &keyboard->modifiers);
}

/* ---- state between the field and the input method ------------------------- */

/* The field's state, to the input method. */
static void send_field_state(struct plat_server *server, struct plat_text_input *ti) {
	struct plat_input_method *im = server->input_method;
	if (!im || !im->ime->active) {
		return;
	}
	const struct wlr_text_input_v3_state *state = &ti->input->current;
	if (state->features & WLR_TEXT_INPUT_V3_FEATURE_SURROUNDING_TEXT) {
		wlr_input_method_v2_send_surrounding_text(im->ime,
			state->surrounding.text ? state->surrounding.text : "",
			(int32_t)state->surrounding.cursor, (int32_t)state->surrounding.anchor);
	}
	if (state->features & WLR_TEXT_INPUT_V3_FEATURE_CONTENT_TYPE) {
		wlr_input_method_v2_send_content_type(im->ime,
			state->content_type.hint, state->content_type.purpose);
	}
	wlr_input_method_v2_send_text_change_cause(im->ime, state->text_change_cause);
	wlr_input_method_v2_send_done(im->ime);
	/* A moved cursor moves the candidate window. */
	struct plat_im_popup *popup;
	wl_list_for_each(popup, &im->popups, link) {
		popup_place(server, popup);
	}
}

void input_method_text_input_commit(struct plat_server *server, struct plat_text_input *ti) {
	if (ti == server->im_text_input) {
		send_field_state(server, ti);
	}
}

/* What the input method answered with, to the field. */
static void handle_ime_commit(struct wl_listener *listener, void *data) {
	struct plat_input_method *im = wl_container_of(listener, im, commit);
	struct plat_server *server = im->server;
	struct plat_text_input *ti = server->im_text_input;
	if (!ti || !im->ime->active) {
		return;
	}
	const struct wlr_input_method_v2_state *state = &im->ime->current;
	if (state->commit_text) {
		wlr_text_input_v3_send_commit_string(ti->input, state->commit_text);
	}
	if (state->preedit.text) {
		wlr_text_input_v3_send_preedit_string(ti->input, state->preedit.text,
			state->preedit.cursor_begin, state->preedit.cursor_end);
	}
	if (state->delete.before_length || state->delete.after_length) {
		wlr_text_input_v3_send_delete_surrounding_text(ti->input,
			state->delete.before_length, state->delete.after_length);
	}
	wlr_text_input_v3_send_done(ti->input);
}

/* Which field is being relayed, and whether the input method is told
 * so: called when fields are enabled, disabled, destroyed and when
 * the keyboard focus moves (osk.c and input.c call it). */
void input_method_update_active(struct plat_server *server) {
	struct plat_text_input *ti = relayed_text_input(server);
	server->im_text_input = ti;
	struct plat_input_method *im = server->input_method;
	if (!im) {
		return;
	}
	if (ti) {
		if (!im->ime->active) {
			wlr_input_method_v2_send_activate(im->ime);
		}
		send_field_state(server, ti);
	} else if (im->ime->active) {
		wlr_input_method_v2_send_deactivate(im->ime);
	}
}

/* ---- the input method itself ---------------------------------------------- */

static void handle_ime_destroy(struct wl_listener *listener, void *data) {
	struct plat_input_method *im = wl_container_of(listener, im, destroy);
	struct plat_server *server = im->server;
	grab_gone(im);
	struct plat_im_popup *popup, *tmp;
	wl_list_for_each_safe(popup, tmp, &im->popups, link) {
		popup_destroy(&popup->destroy, NULL);
	}
	wl_list_remove(&im->commit.link);
	wl_list_remove(&im->grab_keyboard.link);
	wl_list_remove(&im->new_popup.link);
	wl_list_remove(&im->destroy.link);
	if (server->input_method == im) {
		server->input_method = NULL;
	}
	free(im);
}

static void handle_new_input_method(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, new_input_method);
	struct wlr_input_method_v2 *ime = data;
	if (ime->seat != server->seat) {
		return;
	}
	if (server->input_method) {
		wlr_log(WLR_INFO, "input method: a second one turned away");
		wlr_input_method_v2_send_unavailable(ime);
		return;
	}
	struct plat_input_method *im = calloc(1, sizeof(*im));
	im->server = server;
	im->ime = ime;
	wl_list_init(&im->popups);
	im->commit.notify = handle_ime_commit;
	wl_signal_add(&ime->events.commit, &im->commit);
	im->grab_keyboard.notify = handle_grab_keyboard;
	wl_signal_add(&ime->events.grab_keyboard, &im->grab_keyboard);
	im->new_popup.notify = handle_new_popup;
	wl_signal_add(&ime->events.new_popup_surface, &im->new_popup);
	im->destroy.notify = handle_ime_destroy;
	wl_signal_add(&ime->events.destroy, &im->destroy);
	server->input_method = im;
	wlr_log(WLR_INFO, "input method: connected");
	input_method_update_active(server);
}

void input_method_init(struct plat_server *server) {
	server->input_method_manager = wlr_input_method_manager_v2_create(server->display);
	server->new_input_method.notify = handle_new_input_method;
	wl_signal_add(&server->input_method_manager->events.input_method, &server->new_input_method);
}
