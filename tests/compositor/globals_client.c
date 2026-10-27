/* Prints the globals zacos9-wm offers this program, one interface name a
 * line, then "output-scale N" for each screen (wl_output.scale). Run under
 * another name (a copy called zacos9-…) to see what ZacOS's own programs
 * are offered. */
#include <stdio.h>
#include <string.h>
#include <wayland-client.h>

static void output_geometry(void *data, struct wl_output *o, int32_t x, int32_t y, int32_t pw,
		int32_t ph, int32_t subpixel, const char *make, const char *model, int32_t transform) {}
static void output_mode(void *data, struct wl_output *o, uint32_t flags, int32_t w, int32_t h,
		int32_t refresh) {}
static void output_done(void *data, struct wl_output *o) {}
static void output_scale(void *data, struct wl_output *o, int32_t factor) {
	printf("output-scale %d\n", factor);
}
/* Bound at version 2: no name or description events. */
static const struct wl_output_listener output_listener = {
	.geometry = output_geometry, .mode = output_mode, .done = output_done,
	.scale = output_scale,
};

static void global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version) {
	printf("%s\n", interface);
	if (strcmp(interface, "wl_output") == 0) {
		struct wl_output *output = wl_registry_bind(registry, name, &wl_output_interface, 2);
		wl_output_add_listener(output, &output_listener, NULL);
	}
}
static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = { global, global_remove };

int main(void) {
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "globals-client: no Wayland display\n");
		return 1;
	}
	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);
	wl_display_roundtrip(display);
	fflush(stdout);
	wl_display_disconnect(display);
	return 0;
}
