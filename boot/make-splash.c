/*
 * Draws the boot splash's pictures (boot/plymouth/zacos9.script) with the
 * same code the compositor's startup screen uses (lib/welcome.c), so the
 * splash's Welcome screen and the compositor's match pixel for pixel when
 * one hands over to the other.
 *
 *   make-splash DIR [ART]   writes into DIR, with the pictures from ART
 *                           (assets/boot) in the box:
 *     welcome-Nx.png  the Welcome to Zacintosh box with its shadow, at
 *                     scale N (lib/welcome.h), for N = 1..PL_WELCOME_MAX_SCALE
 *     pattern.png     one tile of the default desktop pattern
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
	if (argc != 2 && argc != 3) {
		fprintf(stderr, "usage: make-splash DIR [ART]\n");
		return 2;
	}
	const char *dir = argv[1];
	if (argc == 3) {
		setenv("ZACOS9_BOOT_ART", argv[2], 1);
	}
	struct pl_canvas c;
	uint32_t *px;

	/* The box, on nothing, so the splash's pattern shows around it. */
	struct plat_text *title = text_render_font(PL_WELCOME_TITLE, 1000, PL_FONT_SYSTEM);
	for (int s = 1; s <= PL_WELCOME_MAX_SCALE; s++) {
		if (!pl_boot_art(PL_ART_LOGO, s)) {
			fprintf(stderr, "make-splash: no zacos-logo-%dx.png\n", s);
			return 1;
		}
		const int bw = (PL_WELCOME_BOX_W + PL_WELCOME_SHADOW) * s;
		const int bh = (PL_WELCOME_BOX_H + PL_WELCOME_SHADOW) * s;
		px = canvas(&c, bw, bh, 0);
		pl_welcome_box_paint(&c, 0, 0, s, title);
		char name[32];
		snprintf(name, sizeof(name), "welcome-%dx.png", s);
		save(dir, name, &c, 0, 0, bw, bh);
		free(px);
	}
	text_destroy(title);

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
