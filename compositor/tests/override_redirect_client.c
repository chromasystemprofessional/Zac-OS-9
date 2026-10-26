/*
 * X11 windows that switch override-redirect between being made and being
 * shown, as Tk's menus do (an ordinary window, made override-redirect just
 * before it's mapped). zacos9-wm used to keep such a window as an ordinary
 * one and abort in wlr_xwayland_surface_restack() when it took the focus.
 *
 * Each window is shown, hidden, switched the other way and shown again, so
 * both directions are covered, for a window whose surface already exists.
 * Prints "ok" and exits 0 if every window ends up viewable; the X
 * connection breaking (the compositor, and Xwayland with it, gone) fails.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <xcb/xcb.h>

static xcb_connection_t *c;

static void settle(void) {
	xcb_flush(c);
	nanosleep(&(struct timespec){ .tv_nsec = 400 * 1000 * 1000 }, NULL);
}

static void set_override_redirect(xcb_window_t w, bool on) {
	const uint32_t value = on;
	xcb_change_window_attributes(c, w, XCB_CW_OVERRIDE_REDIRECT, &value);
}

static bool viewable(xcb_window_t w) {
	xcb_get_window_attributes_reply_t *a =
		xcb_get_window_attributes_reply(c, xcb_get_window_attributes(c, w), NULL);
	const bool ok = a && a->map_state == XCB_MAP_STATE_VIEWABLE;
	free(a);
	return ok;
}

/* Made with `start`, switched to `!start`, shown; then hidden, switched
 * back and shown again. */
static bool cycle(xcb_screen_t *screen, bool start, int x) {
	const xcb_window_t w = xcb_generate_id(c);
	const uint32_t values[] = { screen->white_pixel, start };
	xcb_create_window(c, XCB_COPY_FROM_PARENT, w, screen->root, x, 100, 160, 120, 0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
		XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT, values);
	set_override_redirect(w, !start);
	xcb_map_window(c, w);
	settle();
	if (!viewable(w)) {
		fprintf(stderr, "window made %s, then switched, isn't shown\n",
			start ? "override-redirect" : "ordinary");
		return false;
	}
	xcb_unmap_window(c, w);
	settle();
	set_override_redirect(w, start);
	xcb_map_window(c, w);
	settle();
	if (!viewable(w)) {
		fprintf(stderr, "window switched back to %s isn't shown\n",
			start ? "override-redirect" : "ordinary");
		return false;
	}
	xcb_destroy_window(c, w);
	settle();
	return true;
}

int main(void) {
	c = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(c)) {
		fprintf(stderr, "can't connect to $DISPLAY\n");
		return 1;
	}
	xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
	const bool ok = cycle(screen, false, 60) && cycle(screen, true, 300);
	if (xcb_connection_has_error(c)) {
		fprintf(stderr, "the X connection broke: did the compositor die?\n");
		return 1;
	}
	xcb_disconnect(c);
	if (!ok) {
		return 1;
	}
	printf("ok\n");
	return 0;
}
