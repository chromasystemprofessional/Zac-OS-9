/*
 * Pixel tests: render Platinum frames with decor.c and compare them against
 * the Mac OS 8 HIG figures (fetched by tools/measure/fetch.sh; not in git).
 * Only frame pixels are compared. Client content and the title text are
 * excluded (the text font is still a stand-in).
 *
 * usage: test-decor FIGS_DIR     exit 77 (skip) if figures are missing
 */
#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decor.h"

struct rect {
	int x, y, w, h; /* frame-local */
};

struct fixture {
	const char *name;
	const char *figure;
	int fig_x, fig_y; /* frame origin within the figure */
	struct decor_state st;
	struct rect regions[8];
};

/* Measured in docs/reference/zacos9-window.md. */
static const struct fixture fixtures[] = {
	{
		.name = "collapsed active (fig 5-6)",
		.figure = "img-104-134.png", .fig_x = 0, .fig_y = 0,
		.st = { .width = 231, .height = 22, .active = true, .collapsed = true,
			.has_close = true, .has_zoom = true, .has_collapse = true },
		.regions = {
			{ 0, 0, 21, 23 },        /* left end: border, close box */
			{ 194, 0, 38, 23 },      /* right end: zoom, collapse, shadow */
			{ 0, 17, 232, 6 },       /* bottom bevel + shadow, full width */
			{ 0, 0, 232, 4 },        /* top bevel, full width */
		},
	},
	{
		.name = "active document window (fig 5-1)",
		.figure = "img-100-128.png", .fig_x = 3, .fig_y = 2,
		.st = { .width = 214, .height = 227, .active = true,
			.has_close = true, .has_zoom = true, .has_collapse = true,
			.has_grow = true },
		.regions = {
			{ 0, 0, 21, 26 },        /* top-left: close box, well corner */
			{ 176, 0, 39, 22 },      /* top-right: zoom, collapse, shadow */
			{ 0, 0, 215, 4 },        /* top bevel */
			{ 0, 30, 6, 150 },       /* left side */
			{ 208, 30, 7, 150 },     /* right side + shadow */
			{ 0, 221, 193, 6 },      /* bottom */
			/* Shadow row. Fig 5-1 has #777/#888 at (0..1, H) where fig 5-6
			 * and the top-right corner show background: an artifact. */
			{ 2, 227, 191, 1 },
			{ 193, 206, 22, 22 },    /* resize box + corner + shadow */
		},
	},
	{
		.name = "movable modal dialog (fig 3-2)",
		.figure = "img-059-088.png", .fig_x = 0, .fig_y = 0,
		.st = { .style = DECOR_STYLE_MOVABLE_MODAL, .width = 301, .height = 200,
			.active = true },
		.regions = {
			{ 0, 0, 302, 4 },        /* top border and highlight */
			{ 0, 17, 302, 7 },       /* bottom of title bar, body top bevel */
			{ 0, 0, 40, 24 },        /* title bar left end (stripes) */
			{ 262, 0, 40, 24 },      /* title bar right end + shadow */
			{ 0, 24, 3, 177 },       /* left bevel + shadow corner */
			{ 298, 24, 4, 177 },     /* right bevel + shadow */
			{ 0, 197, 302, 4 },      /* bottom bevel + shadow */
		},
	},
	{
		.name = "inactive document window (fig 5-1)",
		.figure = "img-100-128.png", .fig_x = 218, .fig_y = 3,
		.st = { .width = 214, .height = 225, .active = false,
			.has_close = true, .has_zoom = true, .has_collapse = true,
			.has_grow = true },
		.regions = {
			{ 0, 0, 30, 25 },        /* top-left (no boxes when inactive) */
			{ 180, 0, 35, 22 },      /* top-right + shadow start */
			{ 0, 30, 6, 150 },       /* left side */
			{ 208, 30, 7, 150 },     /* right side + shadow */
			{ 0, 219, 193, 7 },      /* bottom + shadow */
			{ 193, 204, 22, 22 },    /* blank resize cell + corner */
		},
	},
};

static uint32_t fig_pixel(cairo_surface_t *s, int x, int y) {
	const uint8_t *row = cairo_image_surface_get_data(s) +
		y * cairo_image_surface_get_stride(s);
	return ((const uint32_t *)row)[x] & 0xFFFFFFu;
}

static int run_fixture(const struct fixture *f, const char *dir) {
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", dir, f->figure);
	cairo_surface_t *fig = cairo_image_surface_create_from_png(path);
	if (cairo_surface_status(fig) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(fig);
		return -1;
	}
	cairo_surface_flush(fig);

	const int W = f->st.width + DECOR_SHADOW;
	const int H = (f->st.collapsed ? DECOR_COLLAPSED_H : f->st.height) + DECOR_SHADOW;
	uint32_t *px = calloc((size_t)W * H, sizeof(*px));
	struct pl_canvas canvas = {
		.px = px, .stride = W, .x = 0, .y = 0, .width = W, .height = H,
	};
	decor_paint(&canvas, &f->st);

	int failures = 0;
	for (int r = 0; r < 8 && f->regions[r].w; r++) {
		const struct rect *rc = &f->regions[r];
		for (int y = rc->y; y < rc->y + rc->h; y++) {
			for (int x = rc->x; x < rc->x + rc->w; x++) {
				/* Transparent pixels (client area, around the shadow) show
				 * the figure's white background. */
				uint32_t ours = x < W && y < H ? px[y * W + x] : 0;
				ours = ours >> 24 ? ours & 0xFFFFFFu : 0xFFFFFFu;
				uint32_t want = fig_pixel(fig, f->fig_x + x, f->fig_y + y);
				if (ours != want) {
					if (failures < 10) {
						fprintf(stderr, "  %s: (%d,%d) got #%06x want #%06x\n",
							f->name, x, y, ours, want);
					}
					failures++;
				}
			}
		}
	}
	free(px);
	cairo_surface_destroy(fig);
	printf("%s %s (%d mismatched pixels)\n", failures ? "FAIL" : "ok  ",
		f->name, failures);
	return failures;
}

static int check_hit(const struct decor_state *st, int x, int y,
		enum decor_part want, const char *what) {
	enum decor_part got = decor_hit(st, x, y);
	if (got != want) {
		fprintf(stderr, "FAIL hit %s at (%d,%d): got %d want %d\n",
			what, x, y, got, want);
		return 1;
	}
	return 0;
}

static int run_hit_tests(void) {
	struct decor_state st = { .width = 214, .height = 227, .active = true,
		.has_close = true, .has_zoom = true, .has_collapse = true,
		.has_grow = true };
	int fails = 0;
	fails += check_hit(&st, 10, 10, DECOR_PART_CLOSE, "close");
	fails += check_hit(&st, 214 - 30, 8, DECOR_PART_ZOOM, "zoom");
	fails += check_hit(&st, 214 - 12, 8, DECOR_PART_COLLAPSE, "collapse");
	fails += check_hit(&st, 100, 10, DECOR_PART_DRAG, "title");
	fails += check_hit(&st, 2, 100, DECOR_PART_DRAG, "left frame");
	fails += check_hit(&st, 100, 100, DECOR_PART_CLIENT, "client");
	fails += check_hit(&st, 214 - 10, 227 - 10, DECOR_PART_GROW, "grow");
	fails += check_hit(&st, 214, 100, DECOR_PART_NONE, "shadow");
	st.active = false;
	fails += check_hit(&st, 10, 10, DECOR_PART_DRAG, "inactive close");
	st.active = true;
	st.collapsed = true;
	fails += check_hit(&st, 100, 30, DECOR_PART_NONE, "below collapsed");
	printf("%s hit testing\n", fails ? "FAIL" : "ok  ");
	return fails;
}

int main(int argc, char *argv[]) {
	int fails = run_hit_tests();
	const char *dir = argc > 1 ? argv[1] : "tools/measure/figs";
	bool ran = false;
	for (size_t i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); i++) {
		int r = run_fixture(&fixtures[i], dir);
		if (r < 0) {
			printf("skip %s (run tools/measure/fetch.sh)\n", fixtures[i].name);
			continue;
		}
		ran = true;
		fails += r;
	}
	if (fails) {
		return 1;
	}
	return ran ? 0 : 77;
}
