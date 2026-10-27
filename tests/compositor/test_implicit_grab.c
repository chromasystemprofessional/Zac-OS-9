#include <assert.h>

#include "../../compositor/src/input.c"

/* The pressed surface moves while the button is held, as The Collar does when
 * ⌘-dragged; the pointer stays with it, in its new surface-local coordinates. */

static struct wlr_surface held_surface, other_surface;
static struct wlr_scene_surface held_scene = { .surface = &held_surface };
static struct wlr_scene_surface other_scene = { .surface = &other_surface };
static struct wlr_scene_buffer held_buffer, other_buffer;
static int held_x, held_y;
static bool held_shown = true;
static double motion_x, motion_y;
static int motions;

struct wlr_scene_surface *wlr_scene_surface_try_from_buffer(struct wlr_scene_buffer *buffer) {
	return buffer == &held_buffer ? &held_scene : buffer == &other_buffer ? &other_scene : NULL;
}

void wlr_scene_node_for_each_buffer(struct wlr_scene_node *node,
		wlr_scene_buffer_iterator_func_t iterator, void *data) {
	iterator(&other_buffer, 0, 0, data);
	if (held_shown) {
		iterator(&held_buffer, held_x, held_y, data);
	}
}

void wlr_seat_pointer_notify_motion(struct wlr_seat *seat, uint32_t time, double sx, double sy) {
	motion_x = sx;
	motion_y = sy;
	motions++;
}

int xwayland_surface_scale(struct wlr_surface *surface) {
	return 1;
}

int main(void) {
	struct wlr_scene scene = {0};
	struct wlr_cursor cursor = { .x = 50, .y = 300 };
	struct wlr_seat_pointer_grab default_grab = {0}, popup_grab = {0};
	struct wlr_seat seat = {0};
	seat.pointer_state.default_grab = &default_grab;
	seat.pointer_state.grab = &default_grab;
	seat.pointer_state.focused_surface = &held_surface;
	struct plat_server server = { .scene = &scene, .cursor = &cursor, .seat = &seat };

	/* No button down: the surface under the pointer gets it, as usual. */
	assert(!implicit_grab_motion(&server, 0));
	assert(motions == 0);

	/* Held, and the surface has moved up under a pointer now well above it. */
	seat.pointer_state.button_count = 1;
	held_x = 0;
	held_y = 400;
	assert(implicit_grab_motion(&server, 0));
	assert(motions == 1 && motion_x == 50 && motion_y == -100);

	/* A popup's grab decides for itself. */
	seat.pointer_state.grab = &popup_grab;
	assert(!implicit_grab_motion(&server, 0));
	seat.pointer_state.grab = &default_grab;

	/* The surface went away (unmapped): back to the usual path. */
	held_shown = false;
	assert(!implicit_grab_motion(&server, 0));
	assert(motions == 1);
	return 0;
}
