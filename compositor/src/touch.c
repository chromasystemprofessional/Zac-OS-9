/* Touchscreens.
 *
 * A finger on a window whose app takes wl_touch (GTK, Firefox, Chromium,
 * Qt apps from elsewhere) is that app's own touch: it scrolls with a swipe,
 * pinches, and drags. Everywhere else it is the mouse: the first finger
 * down moves the arrow there and holds the button, so window frames, the
 * desktop, the menu bar's press-drag-release menus, X11 apps and ZacOS's
 * own programs (made for the mouse: spring-loaded folders, icon drags)
 * work as they do with a mouse. Further fingers on those are ignored.
 *
 * A touchscreen covers one screen: the one its udev WL_OUTPUT names, else
 * the screen of its size (a touch monitor), else the built-in panel, else
 * the main display. */

#include <stdlib.h>
#include <string.h>
#include <linux/input-event-codes.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/util/log.h>

#include "server.h"

#define MAX_POINTS 16

struct plat_touch {
	struct wl_list link; /* plat_server.touchscreens */
	struct plat_server *server;
	struct wlr_touch *wlr_touch;
	struct wl_listener destroy;
};

/* A finger the app has: where its surface was when it went down. */
struct native_point {
	bool used;
	int32_t id;
	double origin_x, origin_y;
};

static struct native_point points[MAX_POINTS];
static int32_t mouse_id = -1;     /* the finger acting as the mouse */
static int32_t swallowed_id = -1; /* the tap that only woke the screen */

static struct native_point *point_find(int32_t id) {
	for (int i = 0; i < MAX_POINTS; i++) {
		if (points[i].used && points[i].id == id) {
			return &points[i];
		}
	}
	return NULL;
}

static struct native_point *point_add(int32_t id) {
	for (int i = 0; i < MAX_POINTS; i++) {
		if (!points[i].used) {
			points[i] = (struct native_point){ true, id, 0, 0 };
			return &points[i];
		}
	}
	return NULL;
}

/* Whether a finger here goes to the app as touch. */
static bool app_takes_touch(struct plat_server *server, struct plat_view *view,
		struct wlr_surface *surface) {
	if (!view || !surface || !wlr_surface_accepts_touch(server->seat, surface)) {
		/* The desktop and menu bar (shell surfaces), frames, and apps
		 * without wl_touch: the mouse. */
		return false;
	}
	return !(view->app_id && strncmp(view->app_id, "zacos9-", 7) == 0);
}

static void touch_down(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, touch_down);
	struct wlr_touch_down_event *event = data;

	/* A tap that only woke a dark screen goes no further, nor its lift. */
	if (idle_input(server, IDLE_INPUT_BUTTON)) {
		swallowed_id = event->touch_id;
		return;
	}
	if (mouse_id >= 0) {
		return; /* one finger is the mouse; the others rest */
	}

	wlr_cursor_warp_absolute(server->cursor, &event->touch->base, event->x, event->y);
	double lx = server->cursor->x, ly = server->cursor->y, sx = 0, sy = 0;
	struct wlr_surface *surface = NULL;
	struct plat_view *view = NULL;
	if (!lock_active(server) && server->cursor_mode == PLAT_CURSOR_PASSTHROUGH &&
			!server->drag_icon) {
		view = view_at(server, lx, ly, &surface, &sx, &sy);
	}
	struct native_point *point;
	if (app_takes_touch(server, view, surface) && (point = point_add(event->touch_id))) {
		/* The finger, not the mouse, is on this app: no arrow under it. */
		wlr_seat_pointer_clear_focus(server->seat);
		wlr_cursor_unset_image(server->cursor);
		input_press_focus(server, event->time_msec);
		point->origin_x = lx - sx;
		point->origin_y = ly - sy;
		wlr_seat_touch_notify_down(server->seat, surface, event->time_msec,
			event->touch_id, sx, sy);
		return;
	}

	mouse_id = event->touch_id;
	input_pointer_motion(server, event->time_msec);
	input_pointer_button(server, event->time_msec, BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
	wlr_seat_pointer_notify_frame(server->seat);
}

static void touch_motion(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, touch_motion);
	struct wlr_touch_motion_event *event = data;
	idle_input(server, IDLE_INPUT_MOTION);

	if (event->touch_id == mouse_id) {
		wlr_cursor_warp_absolute(server->cursor, &event->touch->base, event->x, event->y);
		input_pointer_motion(server, event->time_msec);
		wlr_seat_pointer_notify_frame(server->seat);
		return;
	}
	struct native_point *point = point_find(event->touch_id);
	if (!point) {
		return;
	}
	double lx, ly;
	wlr_cursor_absolute_to_layout_coords(server->cursor, &event->touch->base,
		event->x, event->y, &lx, &ly);
	if (server->seat->drag) {
		/* The app's own drag (a tab, a link): over whatever is under the
		 * finger now, the icon with it. */
		double sx = 0, sy = 0;
		struct wlr_surface *surface = NULL;
		view_at(server, lx, ly, &surface, &sx, &sy);
		if (surface) {
			wlr_seat_touch_point_focus(server->seat, surface, event->time_msec,
				event->touch_id, sx, sy);
		} else {
			wlr_seat_touch_point_clear_focus(server->seat, event->time_msec,
				event->touch_id);
		}
		if (server->drag_icon) {
			wlr_scene_node_set_position(&server->drag_icon->node, (int)lx, (int)ly);
		}
		wlr_seat_touch_notify_motion(server->seat, event->time_msec, event->touch_id, sx, sy);
		return;
	}
	/* A stroke stays with the surface it began on. */
	wlr_seat_touch_notify_motion(server->seat, event->time_msec, event->touch_id,
		lx - point->origin_x, ly - point->origin_y);
}

static void touch_up(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, touch_up);
	struct wlr_touch_up_event *event = data;

	if (event->touch_id == swallowed_id) {
		swallowed_id = -1;
	} else if (event->touch_id == mouse_id) {
		mouse_id = -1;
		input_pointer_button(server, event->time_msec, BTN_LEFT,
			WL_POINTER_BUTTON_STATE_RELEASED);
		wlr_seat_pointer_notify_frame(server->seat);
	} else {
		struct native_point *point = point_find(event->touch_id);
		if (point) {
			point->used = false;
			wlr_seat_touch_notify_up(server->seat, event->time_msec, event->touch_id);
		}
	}
}

static void touch_cancel(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, touch_cancel);
	struct wlr_touch_cancel_event *event = data;

	if (event->touch_id == swallowed_id) {
		swallowed_id = -1;
	} else if (event->touch_id == mouse_id) {
		mouse_id = -1;
		input_pointer_button(server, event->time_msec, BTN_LEFT,
			WL_POINTER_BUTTON_STATE_RELEASED);
		wlr_seat_pointer_notify_frame(server->seat);
	} else {
		struct native_point *point = point_find(event->touch_id);
		struct wlr_touch_point *wlr_point =
			wlr_seat_touch_get_point(server->seat, event->touch_id);
		if (point) {
			point->used = false;
		}
		if (wlr_point && wlr_point->client) {
			/* wl_touch.cancel takes back every finger the app has. */
			wlr_seat_touch_notify_cancel(server->seat, wlr_point->client);
		}
	}
}

static void touch_frame(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, touch_frame);
	wlr_seat_touch_notify_frame(server->seat);
}

static struct wlr_output *touch_output(struct plat_server *server, struct wlr_touch *touch) {
	struct plat_output *output;
	if (touch->output_name) {
		wl_list_for_each(output, &server->outputs, link) {
			if (strcmp(output->wlr_output->name, touch->output_name) == 0) {
				return output->wlr_output;
			}
		}
	}
	if (touch->width_mm > 0 && touch->height_mm > 0) {
		/* A touch monitor: the screen as big as the glass, within 10%. */
		wl_list_for_each(output, &server->outputs, link) {
			const struct wlr_output *o = output->wlr_output;
			if (o->phys_width > 0 && o->phys_height > 0 &&
					abs(o->phys_width - (int)touch->width_mm) * 10 <= o->phys_width &&
					abs(o->phys_height - (int)touch->height_mm) * 10 <= o->phys_height) {
				return output->wlr_output;
			}
		}
	}
	wl_list_for_each(output, &server->outputs, link) {
		const char *name = output->wlr_output->name;
		if (strncmp(name, "eDP", 3) == 0 || strncmp(name, "LVDS", 4) == 0 ||
				strncmp(name, "DSI", 3) == 0) {
			return output->wlr_output;
		}
	}
	struct plat_output *main = output_main(server);
	return main ? main->wlr_output : NULL;
}

void touch_map(struct plat_server *server) {
	struct plat_touch *touch;
	wl_list_for_each(touch, &server->touchscreens, link) {
		wlr_cursor_map_input_to_output(server->cursor, &touch->wlr_touch->base,
			touch_output(server, touch->wlr_touch));
	}
}

static void touch_destroy(struct wl_listener *listener, void *data) {
	struct plat_touch *touch = wl_container_of(listener, touch, destroy);
	struct plat_server *server = touch->server;
	wl_list_remove(&touch->destroy.link);
	wl_list_remove(&touch->link);
	free(touch);
	input_update_capabilities(server);
}

void touch_new(struct plat_server *server, struct wlr_input_device *device) {
	struct plat_touch *touch = calloc(1, sizeof(*touch));
	touch->server = server;
	touch->wlr_touch = wlr_touch_from_input_device(device);
	touch->destroy.notify = touch_destroy;
	wl_signal_add(&device->events.destroy, &touch->destroy);
	wl_list_insert(server->touchscreens.prev, &touch->link);
	wlr_cursor_attach_input_device(server->cursor, device);
	touch_map(server);
	wlr_log(WLR_INFO, "Touchscreen: %s", device->name ? device->name : "(unnamed)");
}

void touch_init(struct plat_server *server) {
	wl_list_init(&server->touchscreens);
	server->touch_down.notify = touch_down;
	wl_signal_add(&server->cursor->events.touch_down, &server->touch_down);
	server->touch_up.notify = touch_up;
	wl_signal_add(&server->cursor->events.touch_up, &server->touch_up);
	server->touch_motion.notify = touch_motion;
	wl_signal_add(&server->cursor->events.touch_motion, &server->touch_motion);
	server->touch_cancel.notify = touch_cancel;
	wl_signal_add(&server->cursor->events.touch_cancel, &server->touch_cancel);
	server->touch_frame.notify = touch_frame;
	wl_signal_add(&server->cursor->events.touch_frame, &server->touch_frame);
}
