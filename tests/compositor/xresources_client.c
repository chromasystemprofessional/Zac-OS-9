/* Prints the X root window's RESOURCE_MANAGER property, for
 * test_xresources.py: what X11 programs read at start-up. */
#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>

int main(void) {
	xcb_connection_t *connection = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(connection)) {
		fprintf(stderr, "no X display\n");
		xcb_disconnect(connection);
		return 2;
	}
	xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
	xcb_get_property_cookie_t cookie = xcb_get_property(connection, 0, screen->root,
		XCB_ATOM_RESOURCE_MANAGER, XCB_GET_PROPERTY_TYPE_ANY, 0, 1 << 18);
	xcb_get_property_reply_t *reply = xcb_get_property_reply(connection, cookie, NULL);
	if (!reply) {
		xcb_disconnect(connection);
		return 1;
	}
	fwrite(xcb_get_property_value(reply), 1, xcb_get_property_value_length(reply), stdout);
	free(reply);
	xcb_disconnect(connection);
	return 0;
}
