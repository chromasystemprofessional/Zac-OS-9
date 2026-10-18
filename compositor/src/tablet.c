/* Graphics tablets (Wacom Intuos and the like).
 *
 * The pen moves the one cursor. Over a window whose app speaks
 * zwp_tablet_v2 (Krita, GIMP, Inkscape, GTK and Qt apps) it gets the pen
 * itself, pressure and tilt and all; everywhere else (window frames, the
 * desktop, X11 apps) the pen is a mouse: the tip clicks, the lower barrel
 * button is the right button and the upper one the middle.
 *
 * The tablet covers one screen: the Tablet panel's "tablet-display" (a
 * screen's name, or "all" for every screen together), else the main
 * display. "tablet-left-handed=1" turns it round (ExpressKeys on the
 * right), libinput rotating the tablet's area half a turn. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/input-event-codes.h>
#include <libinput.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/util/log.h>

#include "server.h"
#include "settings.h"

struct plat_tablet {
	struct wl_list link; /* plat_server.tablets */
	struct plat_server *server;
	struct wlr_tablet *wlr_tablet;
	struct wlr_tablet_v2_tablet *v2;
	struct wl_listener destroy;
};

/* Pen buttons that went to the pointer, so their release does too. */
enum {
	EMULATED_TIP = 1 << 0,
	EMULATED_LOWER = 1 << 1,
	EMULATED_UPPER = 1 << 2,
	EMULATED_OTHER = 1 << 3,
};

struct plat_tablet_tool {
	struct plat_server *server;
	struct wlr_tablet_v2_tablet_tool *v2;
	double x, y;           /* last absolute position, 0..1 on the tablet */
	double tilt_x, tilt_y;
	double origin_x, origin_y; /* layout position of the focused surface */
	uint32_t emulated;     /* EMULATED_* held as mouse buttons */
	bool tip_swallowed;    /* the tip only woke a dark screen */
	struct wl_listener set_cursor;
	struct wl_listener destroy;
};

static void tool_destroy(struct wl_listener *listener, void *data) {
	struct plat_tablet_tool *tool = wl_container_of(listener, tool, destroy);
	wl_list_remove(&tool->destroy.link);
	wl_list_remove(&tool->set_cursor.link);
	free(tool);
}

/* An app with the pen over it chooses the cursor, as with the mouse. */
static void tool_set_cursor(struct wl_listener *listener, void *data) {
	struct plat_tablet_tool *tool = wl_container_of(listener, tool, set_cursor);
	struct wlr_tablet_v2_event_cursor *event = data;
	struct wlr_surface *focused = tool->v2->focused_surface;
	if (!focused || wl_resource_get_client(focused->resource) != event->seat_client->client) {
		return;
	}
	wlr_cursor_set_surface(tool->server->cursor, event->surface,
		event->hotspot_x, event->hotspot_y);
}

static struct plat_tablet_tool *get_tool(struct plat_server *server,
		struct wlr_tablet_tool *wlr_tool) {
	if (wlr_tool->data) {
		return wlr_tool->data;
	}
	struct plat_tablet_tool *tool = calloc(1, sizeof(*tool));
	tool->server = server;
	/* Ours first: wlroots frees the v2 tool on this same signal. */
	tool->destroy.notify = tool_destroy;
	wl_signal_add(&wlr_tool->events.destroy, &tool->destroy);
	tool->v2 = wlr_tablet_tool_create(server->tablet_manager, server->seat, wlr_tool);
	tool->set_cursor.notify = tool_set_cursor;
	wl_signal_add(&tool->v2->events.set_cursor, &tool->set_cursor);
	wlr_tool->data = tool;
	return tool;
}

/* The pen moved to server->cursor: to the app under it if that app takes
 * tablets, else as the mouse. */
static void tool_motion(struct plat_tablet_tool *tool, struct plat_tablet *tablet,
		uint32_t time) {
	struct plat_server *server = tool->server;
	double lx = server->cursor->x, ly = server->cursor->y;
	if (tool->v2->is_down && tool->v2->focused_surface) {
		/* A stroke stays with the surface it began on. */
		wlr_tablet_v2_tablet_tool_notify_motion(tool->v2, lx - tool->origin_x, ly - tool->origin_y);
		return;
	}

	double sx = 0, sy = 0;
	struct wlr_surface *surface = NULL;
	if (!tool->emulated && server->cursor_mode == PLAT_CURSOR_PASSTHROUGH &&
			!server->drag_icon && tablet && tablet->v2) {
		if (lock_active(server)) {
			surface = lock_surface_at(server, lx, ly, &sx, &sy);
		} else {
			view_at(server, lx, ly, &surface, &sx, &sy);
		}
		if (surface && !wlr_surface_accepts_tablet_v2(tablet->v2, surface)) {
			surface = NULL;
		}
	}

	if (surface) {
		if (tool->v2->focused_surface != surface) {
			/* The pen, not the mouse, is over this app now. */
			wlr_seat_pointer_clear_focus(server->seat);
			input_refresh_cursor(server);
			wlr_tablet_v2_tablet_tool_notify_proximity_in(tool->v2, tablet->v2, surface);
		}
		tool->origin_x = lx - sx;
		tool->origin_y = ly - sy;
		wlr_tablet_v2_tablet_tool_notify_motion(tool->v2, sx, sy);
		return;
	}
	if (tool->v2->focused_surface) {
		wlr_tablet_v2_tablet_tool_notify_proximity_out(tool->v2);
	}
	input_pointer_motion(server, time);
	wlr_seat_pointer_notify_frame(server->seat);
}

static void move_cursor(struct plat_server *server, struct wlr_tablet *wlr_tablet,
		struct plat_tablet_tool *tool) {
	wlr_cursor_warp_absolute(server->cursor, &wlr_tablet->base, tool->x, tool->y);
}

static void tool_axis(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, tablet_tool_axis);
	struct wlr_tablet_tool_axis_event *event = data;
	struct plat_tablet_tool *tool = get_tool(server, event->tool);

	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_X) {
		tool->x = event->x;
	}
	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_Y) {
		tool->y = event->y;
	}
	if (event->updated_axes & (WLR_TABLET_TOOL_AXIS_X | WLR_TABLET_TOOL_AXIS_Y)) {
		move_cursor(server, event->tablet, tool);
		idle_input(server, IDLE_INPUT_MOTION);
		tool_motion(tool, event->tablet->data, event->time_msec);
	}
	if (!tool->v2->focused_surface) {
		return; /* a mouse has no pressure */
	}

	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_PRESSURE) {
		wlr_tablet_v2_tablet_tool_notify_pressure(tool->v2, event->pressure);
	}
	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_DISTANCE) {
		wlr_tablet_v2_tablet_tool_notify_distance(tool->v2, event->distance);
	}
	if (event->updated_axes & (WLR_TABLET_TOOL_AXIS_TILT_X | WLR_TABLET_TOOL_AXIS_TILT_Y)) {
		if (event->updated_axes & WLR_TABLET_TOOL_AXIS_TILT_X) {
			tool->tilt_x = event->tilt_x;
		}
		if (event->updated_axes & WLR_TABLET_TOOL_AXIS_TILT_Y) {
			tool->tilt_y = event->tilt_y;
		}
		wlr_tablet_v2_tablet_tool_notify_tilt(tool->v2, tool->tilt_x, tool->tilt_y);
	}
	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_ROTATION) {
		wlr_tablet_v2_tablet_tool_notify_rotation(tool->v2, event->rotation);
	}
	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_SLIDER) {
		wlr_tablet_v2_tablet_tool_notify_slider(tool->v2, event->slider);
	}
	if (event->updated_axes & WLR_TABLET_TOOL_AXIS_WHEEL) {
		wlr_tablet_v2_tablet_tool_notify_wheel(tool->v2, event->wheel_delta, 0);
	}
}

static void tool_proximity(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, tablet_tool_proximity);
	struct wlr_tablet_tool_proximity_event *event = data;
	struct plat_tablet_tool *tool = get_tool(server, event->tool);

	if (event->state == WLR_TABLET_TOOL_PROXIMITY_IN) {
		tool->x = event->x;
		tool->y = event->y;
		move_cursor(server, event->tablet, tool);
		idle_input(server, IDLE_INPUT_MOTION);
		tool_motion(tool, event->tablet->data, event->time_msec);
		return;
	}
	if (tool->v2->focused_surface) {
		/* Pen lifted away: the mouse has the app again. */
		wlr_tablet_v2_tablet_tool_notify_proximity_out(tool->v2);
		input_pointer_refocus(server);
	}
}

/* Press or release a pen button as the mouse button it stands for. */
static void emulate_button(struct plat_tablet_tool *tool, uint32_t bit, uint32_t button,
		bool pressed, uint32_t time) {
	if (pressed) {
		tool->emulated |= bit;
	} else {
		tool->emulated &= ~bit;
	}
	input_pointer_button(tool->server, time, button,
		pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
	wlr_seat_pointer_notify_frame(tool->server->seat);
}

static void tool_tip(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, tablet_tool_tip);
	struct wlr_tablet_tool_tip_event *event = data;
	struct plat_tablet_tool *tool = get_tool(server, event->tool);

	if (event->state == WLR_TABLET_TOOL_TIP_DOWN) {
		/* A tap that only woke a dark screen goes no further, nor its lift. */
		if (idle_input(server, IDLE_INPUT_BUTTON)) {
			tool->tip_swallowed = true;
			return;
		}
		if (tool->v2->focused_surface && !tool->emulated) {
			input_press_focus(server, event->time_msec);
			wlr_tablet_v2_tablet_tool_notify_down(tool->v2);
		} else {
			emulate_button(tool, EMULATED_TIP, BTN_LEFT, true, event->time_msec);
		}
		return;
	}

	if (tool->tip_swallowed) {
		tool->tip_swallowed = false;
	} else if (tool->emulated & EMULATED_TIP) {
		emulate_button(tool, EMULATED_TIP, BTN_LEFT, false, event->time_msec);
	} else if (tool->v2->is_down) {
		wlr_tablet_v2_tablet_tool_notify_up(tool->v2);
		/* The stroke may have ended over another window. */
		tool_motion(tool, event->tablet->data, event->time_msec);
	}
}

static void tool_button(struct wl_listener *listener, void *data) {
	struct plat_server *server = wl_container_of(listener, server, tablet_tool_button);
	struct wlr_tablet_tool_button_event *event = data;
	struct plat_tablet_tool *tool = get_tool(server, event->tool);
	bool pressed = event->state == WLR_BUTTON_PRESSED;

	uint32_t bit = EMULATED_OTHER, button = BTN_SIDE;
	if (event->button == BTN_STYLUS) {
		bit = EMULATED_LOWER;
		button = BTN_RIGHT;
	} else if (event->button == BTN_STYLUS2) {
		bit = EMULATED_UPPER;
		button = BTN_MIDDLE;
	}

	bool native = pressed ? tool->v2->focused_surface && !tool->emulated :
		!(tool->emulated & bit);
	if (native) {
		wlr_tablet_v2_tablet_tool_notify_button(tool->v2, event->button,
			pressed ? ZWP_TABLET_PAD_V2_BUTTON_STATE_PRESSED :
				ZWP_TABLET_PAD_V2_BUTTON_STATE_RELEASED);
	} else {
		emulate_button(tool, bit, button, pressed, event->time_msec);
	}
}

static void tablet_destroy(struct wl_listener *listener, void *data) {
	struct plat_tablet *tablet = wl_container_of(listener, tablet, destroy);
	struct plat_server *server = tablet->server;
	wl_list_remove(&tablet->destroy.link);
	wl_list_remove(&tablet->link);
	free(tablet);
	prefs_write_outputs(server); /* the Tablet panel shows it gone */
}

void tablet_map(struct plat_server *server) {
	char want[128];
	bool all = false;
	struct wlr_output *target = NULL;
	if (pl_setting("tablet-display", want, sizeof(want)) && *want) {
		all = strcmp(want, "all") == 0;
		struct plat_output *output;
		wl_list_for_each(output, &server->outputs, link) {
			if (strcmp(output->wlr_output->name, want) == 0) {
				target = output->wlr_output;
			}
		}
	}
	if (!all && !target) {
		/* Unset, or that screen isn't connected: the main display. */
		struct plat_output *main = output_main(server);
		target = main ? main->wlr_output : NULL;
	}
	char hand[8];
	const bool left_handed = pl_setting("tablet-left-handed", hand, sizeof(hand)) &&
		strcmp(hand, "1") == 0;
	struct plat_tablet *tablet;
	wl_list_for_each(tablet, &server->tablets, link) {
		struct wlr_input_device *device = &tablet->wlr_tablet->base;
		wlr_cursor_map_input_to_output(server->cursor, device, all ? NULL : target);
		if (wlr_input_device_is_libinput(device)) {
			struct libinput_device *li = wlr_libinput_get_device_handle(device);
			if (libinput_device_config_left_handed_is_available(li)) {
				libinput_device_config_left_handed_set(li, left_handed);
			}
		}
	}
}

void tablet_write_status(struct plat_server *server, FILE *f) {
	struct plat_tablet *tablet;
	wl_list_for_each(tablet, &server->tablets, link) {
		const char *name = tablet->wlr_tablet->base.name;
		fprintf(f, "tablet %s\n", name ? name : "Tablet");
	}
}

void tablet_new(struct plat_server *server, struct wlr_input_device *device) {
	struct wlr_tablet *wlr_tablet = wlr_tablet_from_input_device(device);
	struct plat_tablet *tablet = calloc(1, sizeof(*tablet));
	tablet->server = server;
	tablet->wlr_tablet = wlr_tablet;
	tablet->v2 = wlr_tablet_create(server->tablet_manager, server->seat, device);
	tablet->destroy.notify = tablet_destroy;
	wl_signal_add(&device->events.destroy, &tablet->destroy);
	wlr_tablet->data = tablet;
	wl_list_insert(server->tablets.prev, &tablet->link);
	wlr_cursor_attach_input_device(server->cursor, device);
	tablet_map(server);
	prefs_write_outputs(server);
	wlr_log(WLR_INFO, "Tablet: %s", device->name ? device->name : "(unnamed)");
}

void tablet_init(struct plat_server *server) {
	wl_list_init(&server->tablets);
	server->tablet_manager = wlr_tablet_v2_create(server->display);
	server->tablet_tool_axis.notify = tool_axis;
	wl_signal_add(&server->cursor->events.tablet_tool_axis, &server->tablet_tool_axis);
	server->tablet_tool_proximity.notify = tool_proximity;
	wl_signal_add(&server->cursor->events.tablet_tool_proximity,
		&server->tablet_tool_proximity);
	server->tablet_tool_tip.notify = tool_tip;
	wl_signal_add(&server->cursor->events.tablet_tool_tip, &server->tablet_tool_tip);
	server->tablet_tool_button.notify = tool_button;
	wl_signal_add(&server->cursor->events.tablet_tool_button, &server->tablet_tool_button);
}
