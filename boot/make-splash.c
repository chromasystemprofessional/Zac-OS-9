/*
 * Draws the boot splash's pictures (boot/plymouth/zacos9.script) with the
 * same code the compositor's startup screen uses (lib/welcome.c), so the
 * splash's Welcome screen and the compositor's match pixel for pixel when
 * one hands over to the other.
 *
 *   make-splash DIR   writes into DIR:
 *     welcome.png   the Welcome box, its bar empty, with its shadow
 *     bar-fill.png  the bar's inside, full (the splash crops it)
 *     bar-end.png   the end of a part-full bar: its last two columns, the
 *                   black line and the well's two shadow columns, laid over
 *                   at fill - 2 (cropped short of the bar's right end)
 *     pattern.png   one tile of the default desktop pattern
 *     parade.png    every extension's icon in a row, in lib/extensions.c's
 *                   order; boot/zacos9-parade names them by that index
 */
#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "draw.h"
#include "extensions.h"
#include "patterns.h"
#include "text.h"
#include "welcome.h"
#include "widgets.h"

static uint32_t *canvas(struct pl_canvas *c, int w, int h, uint32_t fill) {
	uint32_t *px = malloc(sizeof(uint32_t) * w * h);
	for (int i = 0; i < w * h; i++) {
		px[i] = fill;
	}
	*c = (struct pl_canvas){ .px = px, .stride = w, .width = w, .height = h };
	return px;
}

/* Writes the part of `c` from (x, y), w x h, as a PNG. */
static void save(const char *dir, const char *name, const struct pl_canvas *c, int x, int y,
		int w, int h) {
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	uint32_t *out = (uint32_t *)cairo_image_surface_get_data(s);
	const int stride = cairo_image_surface_get_stride(s) / 4;
	for (int py = 0; py < h; py++) {
		for (int px = 0; px < w; px++) {
			const uint32_t v = c->px[(y + py) * c->stride + x + px];
			const uint32_t a = v >> 24;
			const uint32_t r = ((v >> 16) & 0xFF) * a / 255, g = ((v >> 8) & 0xFF) * a / 255,
				b = (v & 0xFF) * a / 255;
			out[py * stride + px] = a << 24 | r << 16 | g << 8 | b;
		}
	}
	cairo_surface_mark_dirty(s);
	char path[4096];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	if (cairo_surface_write_to_png(s, path) != CAIRO_STATUS_SUCCESS) {
		fprintf(stderr, "make-splash: cannot write %s\n", path);
		exit(1);
	}
	cairo_surface_destroy(s);
}

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "usage: make-splash DIR\n");
		return 2;
	}
	const char *dir = argv[1];
	struct pl_canvas c;
	const struct pl_accent accent = PL_ACCENT_DEFAULT;

	/* The box, on nothing, so the splash's pattern shows around it. */
	const int bw = PL_WELCOME_BOX_W + 2, bh = PL_WELCOME_BOX_H + 2;
	uint32_t *px = canvas(&c, bw, bh, 0);
	struct plat_text *title = text_render_font("Welcome to ZacOS 9", 1000, PL_FONT_SYSTEM);
	struct plat_text *status = text_render_font("Starting Up\xe2\x80\xa6", 1000, PL_FONT_VIEWS);
	pl_welcome_box_paint(&c, 0, 0, title, status, 0, accent);
	save(dir, "welcome.png", &c, 0, 0, bw, bh);
	free(px);

	/* The bar, full and part full, alone. */
	const int barw = PL_WELCOME_BAR_W, inner = barw - 2;
	px = canvas(&c, barw + 2, PL_PROGRESS_H + 2, 0);
	pl_progress_paint(&c, 1, 1, barw, 1.0, accent);
	save(dir, "bar-fill.png", &c, 2, 2, inner, PL_PROGRESS_H - 2);
	const int fill = inner / 2;
	pl_progress_paint(&c, 1, 1, barw, (double)fill / inner, accent);
	save(dir, "bar-end.png", &c, 2 + fill - 2, 2, 5, PL_PROGRESS_H - 2);
	free(px);

	int pw, ph;
	pl_pattern_size(0, &pw, &ph);
	px = canvas(&c, pw, ph, 0);
	pl_pattern_fill(&c, 0, 0, 0, pw - 1, ph - 1);
	save(dir, "pattern.png", &c, 0, 0, pw, ph);
	free(px);

	const int n = pl_n_extensions;
	px = canvas(&c, PL_PARADE_ICON * n, PL_PARADE_ICON, 0);
	for (int i = 0; i < n; i++) {
		const uint32_t *icon = pl_icon(pl_extensions[i].icon, PL_PARADE_ICON);
		for (int y = 0; y < PL_PARADE_ICON; y++) {
			for (int x = 0; x < PL_PARADE_ICON; x++) {
				const uint32_t v = icon[y * PL_PARADE_ICON + x];
				if (v >> 24) {
					pl_put(&c, i * PL_PARADE_ICON + x, y, v | 0xFF000000u);
				}
			}
		}
	}
	save(dir, "parade.png", &c, 0, 0, PL_PARADE_ICON * n, PL_PARADE_ICON);
	free(px);
	return 0;
}
