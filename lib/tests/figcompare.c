#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>

#include "figcompare.h"

static const uint32_t (*color_map)[2];
static int color_map_n;
static int black_only;

void fig_set_black_only(int on) {
	black_only = on;
}

void fig_set_color_map(const uint32_t (*pairs)[2], int n) {
	color_map = pairs;
	color_map_n = n;
}

static uint32_t mapped(uint32_t c) {
	for (int i = 0; i < color_map_n; i++) {
		if (color_map[i][0] == c) {
			return color_map[i][1];
		}
	}
	return c;
}

int fig_compare(const char *name, const char *dir, const char *figure,
		int fig_x, int fig_y, const uint32_t *px, int w, int h,
		const struct fig_rect *regions, int max_regions) {
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", dir, figure);
	cairo_surface_t *fig = cairo_image_surface_create_from_png(path);
	if (cairo_surface_status(fig) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(fig);
		printf("skip %s (run tools/measure/fetch.sh)\n", name);
		return -1;
	}
	cairo_surface_flush(fig);
	const uint8_t *data = cairo_image_surface_get_data(fig);
	int fig_stride = cairo_image_surface_get_stride(fig);
	int fig_w = cairo_image_surface_get_width(fig);
	int fig_h = cairo_image_surface_get_height(fig);

	int failures = 0;
	for (int r = 0; r < max_regions && regions[r].w; r++) {
		const struct fig_rect *rc = &regions[r];
		for (int y = rc->y; y < rc->y + rc->h; y++) {
			for (int x = rc->x; x < rc->x + rc->w; x++) {
				int fx = fig_x + x, fy = fig_y + y;
				if (fx < 0 || fy < 0 || fx >= fig_w || fy >= fig_h) {
					continue;
				}
				uint32_t ours = x >= 0 && y >= 0 && x < w && y < h ? px[y * w + x] : 0;
				ours = ours >> 24 ? ours & 0xFFFFFFu : 0xFFFFFFu;
				uint32_t want = mapped(
					((const uint32_t *)(data + fy * fig_stride))[fx] & 0xFFFFFFu);
				if (black_only) {
					/* Near-black (#222 corner pixels) counts as black. */
					ours = (ours & 0xFF) > 0x40 ? 0xFFFFFFu : 0;
					want = (want & 0xFF) > 0x40 ? 0xFFFFFFu : 0;
				}
				if (ours != want) {
					if (failures < (getenv("FIG_VERBOSE") ? 1000 : 10)) {
						fprintf(stderr, "  %s: (%d,%d) got #%06x want #%06x\n",
							name, x, y, ours, want);
					}
					failures++;
				}
			}
		}
	}
	cairo_surface_destroy(fig);
	printf("%s %s (%d mismatched pixels)\n", failures ? "FAIL" : "ok  ",
		name, failures);
	return failures;
}

int fig_exit_status(int failures, int ran) {
	if (failures) {
		return 1;
	}
	return ran ? 0 : 77;
}
