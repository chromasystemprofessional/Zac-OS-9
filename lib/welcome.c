#include <cairo.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "welcome.h"

int pl_welcome_scale(int w, int h) {
	int s = w / PL_WELCOME_SCREEN_W;
	if (h / PL_WELCOME_SCREEN_H < s) {
		s = h / PL_WELCOME_SCREEN_H;
	}
	if (s > PL_WELCOME_MAX_SCALE) {
		s = PL_WELCOME_MAX_SCALE;
	}
	return s < 1 ? 1 : s;
}

static uint32_t *load_png(const char *path, int size) {
	cairo_surface_t *s = cairo_image_surface_create_from_png(path);
	if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS ||
			cairo_image_surface_get_width(s) != size ||
			cairo_image_surface_get_height(s) != size) {
		cairo_surface_destroy(s);
		return NULL;
	}
	cairo_surface_flush(s);
	const uint32_t *in = (const uint32_t *)cairo_image_surface_get_data(s);
	const int stride = cairo_image_surface_get_stride(s) / 4;
	const cairo_format_t format = cairo_image_surface_get_format(s);
	uint32_t *px = malloc(sizeof(uint32_t) * size * size);
	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			uint32_t v = in[y * stride + x];
			if (format != CAIRO_FORMAT_ARGB32) {
				v |= 0xFF000000u;
			}
			/* Cairo's pixels are premultiplied; the canvas's are not. */
			const uint32_t a = v >> 24;
			if (a && a < 255) {
				const uint32_t r = (((v >> 16) & 0xFF) * 255 + a / 2) / a;
				const uint32_t g = (((v >> 8) & 0xFF) * 255 + a / 2) / a;
				const uint32_t b = ((v & 0xFF) * 255 + a / 2) / a;
				v = a << 24 | (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 |
					(b > 255 ? 255 : b);
			}
			px[y * size + x] = v;
		}
	}
	cairo_surface_destroy(s);
	return px;
}

const uint32_t *pl_boot_art(enum pl_boot_art art, int scale) {
	static uint32_t *cache[2][PL_WELCOME_MAX_SCALE + 1];
	static bool tried[2][PL_WELCOME_MAX_SCALE + 1];
	if ((unsigned)art > PL_ART_LOGO || scale < 1 || scale > PL_WELCOME_MAX_SCALE) {
		return NULL;
	}
	if (tried[art][scale]) {
		return cache[art][scale];
	}
	tried[art][scale] = true;
	const char *name = art == PL_ART_HAPPY_ZAC ? "happy-zac" : "zacos-logo";
	const char *env = getenv("ZACOS9_BOOT_ART");
	const char *dirs[] = { env && *env ? env : NULL, ZACOS9_DATA_DIR "/boot",
		ZACOS9_SOURCE_DIR "/assets/boot" };
	for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]) && !cache[art][scale]; i++) {
		if (!dirs[i]) {
			continue;
		}
		char path[4096];
		snprintf(path, sizeof(path), "%s/%s-%dx.png", dirs[i], name, scale);
		if (access(path, R_OK) == 0) {
			cache[art][scale] = load_png(path, PL_WELCOME_ICON * scale);
		}
	}
	return cache[art][scale];
}

void pl_happy_zac_origin(int w, int h, int *x, int *y) {
	const int size = PL_WELCOME_ICON * pl_welcome_scale(w, h);
	*x = (w - size) / 2;
	*y = (h - size) / 2;
}

void pl_happy_zac_paint(struct pl_canvas *c, int w, int h) {
	pl_fill(c, 0, 0, w - 1, h - 1, C_WHITE);
	const int s = pl_welcome_scale(w, h), size = PL_WELCOME_ICON * s;
	const uint32_t *art = pl_boot_art(PL_ART_HAPPY_ZAC, s);
	if (art) {
		int x, y;
		pl_happy_zac_origin(w, h, &x, &y);
		pl_image_blend(c, x, y, art, size, size, false);
	}
}

void pl_welcome_box_origin(int w, int h, int *x, int *y) {
	const int s = pl_welcome_scale(w, h);
	*x = (w - PL_WELCOME_SCREEN_W * s) / 2 + PL_WELCOME_BOX_X * s;
	*y = (h - PL_WELCOME_SCREEN_H * s) / 2 + PL_WELCOME_BOX_Y * s;
}

/* The 1-bit title, each of its pixels a scale x scale square. */
static void text_scaled(struct pl_canvas *c, const struct plat_text *t, int ink_x, int baseline_y,
		int scale, uint32_t color) {
	if (t->ink_l < 0) {
		return;
	}
	for (int my = 0; my < t->height; my++) {
		for (int mx = t->ink_l; mx <= t->ink_r; mx++) {
			if (t->mask[my * t->stride + mx]) {
				const int x = ink_x + (mx - t->ink_l) * scale;
				const int y = baseline_y + (my - t->baseline) * scale;
				pl_fill(c, x, y, x + scale - 1, y + scale - 1, color);
			}
		}
	}
}

void pl_welcome_box_paint(struct pl_canvas *c, int bx, int by, int scale,
		const struct plat_text *title) {
	const int s = scale, x1 = bx + PL_WELCOME_BOX_W * s - 1, y1 = by + PL_WELCOME_BOX_H * s - 1;
	const int d = PL_WELCOME_SHADOW * s;
	pl_fill(c, bx + d, by + d, x1 + d, y1 + d, C_BLACK);
	pl_fill(c, bx, by, x1, y1, C_BLACK);
	pl_fill(c, bx + s, by + s, x1 - s, y1 - s, C_WHITE);

	const uint32_t *logo = pl_boot_art(PL_ART_LOGO, s);
	if (logo) {
		pl_image_blend(c, bx + PL_WELCOME_ICON_X * s, by + PL_WELCOME_ICON_Y * s, logo,
			PL_WELCOME_ICON * s, PL_WELCOME_ICON * s, false);
	}
	if (title) {
		const int tw = (title->ink_r - title->ink_l + 1) * s;
		text_scaled(c, title, bx + PL_WELCOME_TITLE_X * s - tw / 2,
			by + PL_WELCOME_BASELINE * s, s, C_BLACK);
	}
}

void pl_parade_slot(int w, int h, int i, int *x, int *y) {
	const int step = PL_PARADE_ICON + PL_PARADE_GAP;
	int per_row = (w - 2 * PL_PARADE_LEFT + PL_PARADE_GAP) / step;
	if (per_row < 1) {
		per_row = 1;
	}
	*x = PL_PARADE_LEFT + (i % per_row) * step;
	*y = h - PL_PARADE_ICON - PL_PARADE_BOTTOM - (i / per_row) * step;
}

void pl_parade_paint(struct pl_canvas *c, int w, int h, const enum pl_icon_kind *icons, int n) {
	for (int i = 0; i < n; i++) {
		int x, y;
		pl_parade_slot(w, h, i, &x, &y);
		const uint32_t *px = pl_icon(icons[i], PL_PARADE_ICON);
		for (int py = 0; py < PL_PARADE_ICON; py++) {
			for (int ix = 0; ix < PL_PARADE_ICON; ix++) {
				const uint32_t v = px[py * PL_PARADE_ICON + ix];
				if (v >> 24) {
					pl_put(c, x + ix, y + py, v);
				}
			}
		}
	}
}
