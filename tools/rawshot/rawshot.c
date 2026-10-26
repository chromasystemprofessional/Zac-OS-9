/*
 * rawshot: one screen's pixels exactly as zacos9-wm shows them, for UI
 * tests, through zacos-snapshot-v1. grim makes its image at a size from
 * the screen's logical size, which at 1.5x or 1.75x is not a whole number
 * of pixels, so it resamples (slightly blurring every edge); this doesn't.
 *
 *   rawshot OUTPUT FILE.ppm     e.g. rawshot HEADLESS-1 shot.ppm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "zacos-snapshot-v1-client-protocol.h"

static struct zacos_snapshot_manager_v1 *manager;
static struct wl_output *outputs[16];
static char *names[16];
static int n_outputs;
static const char *path;
static int status = -1; /* -1 waiting, 0 written, 1 failed */

static void output_geometry(void *data, struct wl_output *o, int32_t x, int32_t y, int32_t pw,
		int32_t ph, int32_t subpixel, const char *make, const char *model, int32_t transform) {}
static void output_mode(void *data, struct wl_output *o, uint32_t flags, int32_t w, int32_t h,
		int32_t refresh) {}
static void output_done(void *data, struct wl_output *o) {}
static void output_scale(void *data, struct wl_output *o, int32_t factor) {}
static void output_name(void *data, struct wl_output *o, const char *name) {
	names[(intptr_t)data] = strdup(name);
}
static void output_description(void *data, struct wl_output *o, const char *description) {}
static const struct wl_output_listener output_listener = {
	output_geometry, output_mode, output_done, output_scale, output_name, output_description,
};

static void global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version) {
	if (strcmp(interface, zacos_snapshot_manager_v1_interface.name) == 0) {
		manager = wl_registry_bind(registry, name, &zacos_snapshot_manager_v1_interface, 1);
	} else if (strcmp(interface, wl_output_interface.name) == 0 && version >= 4 && n_outputs < 16) {
		outputs[n_outputs] = wl_registry_bind(registry, name, &wl_output_interface, 4);
		wl_output_add_listener(outputs[n_outputs], &output_listener, (void *)(intptr_t)n_outputs);
		n_outputs++;
	}
}
static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = { global, global_remove };

static void frame_image(void *data, struct zacos_snapshot_frame_v1 *frame, int32_t fd,
		uint32_t width, uint32_t height, uint32_t stride, uint32_t transform, int32_t x,
		int32_t y, uint32_t logical_width, uint32_t logical_height) {
	const size_t size = (size_t)stride * height;
	const uint8_t *px = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	FILE *f = px != MAP_FAILED ? fopen(path, "wb") : NULL;
	if (!f) {
		fprintf(stderr, "rawshot: can't read the image or write %s\n", path);
		status = 1;
		return;
	}
	/* Native-endian ARGB8888 to PPM's RGB. */
	fprintf(f, "P6\n%u %u\n255\n", width, height);
	for (uint32_t j = 0; j < height; j++) {
		const uint32_t *row = (const uint32_t *)(px + (size_t)j * stride);
		for (uint32_t i = 0; i < width; i++) {
			const uint8_t rgb[3] = { row[i] >> 16, row[i] >> 8, row[i] };
			fwrite(rgb, 1, 3, f);
		}
	}
	status = fclose(f) == 0 ? 0 : 1;
	munmap((void *)px, size);
}

static void frame_failed(void *data, struct zacos_snapshot_frame_v1 *frame, const char *message) {
	fprintf(stderr, "rawshot: %s\n", message);
	status = 1;
}

static const struct zacos_snapshot_frame_v1_listener frame_listener = { frame_image, frame_failed };

int main(int argc, char **argv) {
	if (argc != 3) {
		fprintf(stderr, "usage: rawshot OUTPUT FILE.ppm\n");
		return 2;
	}
	path = argv[2];
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "rawshot: no Wayland display\n");
		return 1;
	}
	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);
	wl_display_roundtrip(display); /* the outputs' names */
	struct wl_output *output = NULL;
	for (int i = 0; i < n_outputs; i++) {
		if (names[i] && strcmp(names[i], argv[1]) == 0) {
			output = outputs[i];
		}
	}
	if (!manager || !output) {
		fprintf(stderr, "rawshot: no %s\n", manager ? argv[1] : "zacos-snapshot-v1");
		return 1;
	}
	struct zacos_snapshot_frame_v1 *frame = zacos_snapshot_manager_v1_capture(manager, output);
	zacos_snapshot_frame_v1_add_listener(frame, &frame_listener, NULL);
	while (status < 0 && wl_display_dispatch(display) >= 0) {
	}
	return status == 0 ? 0 : 1;
}
