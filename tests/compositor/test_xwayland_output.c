/*
 * Outputs as Xwayland sees them (xwayland_output.c): each reaches from its
 * layout corner to its pixel corner, so 1x X11 windows (in layout units) and
 * HiDPI ones (in pixels) on it are both inside an X11 output, where X11 lets
 * the pointer go. Before, a screen away from the layout's origin left its 1x
 * windows outside every output, and clicks on them were lost.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "server.h"

static bool contains(const struct wlr_box *outer, const struct wlr_box *inner) {
	return inner->x >= outer->x && inner->y >= outer->y &&
		inner->x + inner->width <= outer->x + outer->width &&
		inner->y + inner->height <= outer->y + outer->height;
}

static bool point_in(const struct wlr_box *box, int x, int y) {
	return x >= box->x && y >= box->y && x < box->x + box->width && y < box->y + box->height;
}

/* The X11 box holds both the layout box and the pixel box. */
static struct wlr_box check(struct wlr_box layout, int scale) {
	struct wlr_box x11;
	xwayland_output_box(&layout, scale, &x11);
	const struct wlr_box pixels = { layout.x * scale, layout.y * scale,
		layout.width * scale, layout.height * scale };
	assert(contains(&x11, &layout));
	assert(contains(&x11, &pixels));
	return x11;
}

int main(void) {
	/* At scale 1 nothing changes. */
	struct wlr_box one = check((struct wlr_box){ 640, 140, 1280, 1024 }, 1);
	assert(one.x == 640 && one.y == 140 && one.width == 1280 && one.height == 1024);

	/* At the layout's origin, the pixel box alone, as before. */
	struct wlr_box origin = check((struct wlr_box){ 0, 0, 1920, 1080 }, 2);
	assert(origin.x == 0 && origin.y == 0 && origin.width == 3840 && origin.height == 2160);

	/* A 4K screen at scale 2 with a 1280x1024 one beside it, lower down:
	 * a 1x window's menu bar at (700, 150) on the small screen was outside
	 * every X11 output (the small one started at 1280, 280 in pixels). */
	struct wlr_box small = check((struct wlr_box){ 640, 140, 640, 512 }, 2);
	struct wlr_box big = check((struct wlr_box){ 1280, 0, 1920, 1080 }, 2);
	assert(point_in(&small, 700, 150));
	assert(!point_in(&(struct wlr_box){ 1280, 280, 1280, 1024 }, 700, 150));
	/* And a 1x window on the big screen at layout (1400, 60). */
	assert(point_in(&big, 1400, 60));

	/* A screen left of or above the origin. */
	check((struct wlr_box){ -640, -100, 640, 512 }, 2);
	check((struct wlr_box){ -1920, 0, 1920, 1080 }, 3);

	puts("xwayland-output: ok");
	return 0;
}
