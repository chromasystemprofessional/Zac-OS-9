#include <cairo.h>
#include <math.h>
#include <stdint.h>

#include "welcome.h"
#include "widgets.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void rgba(cairo_t *cr, uint32_t hex, double a) {
	cairo_set_source_rgba(cr, ((hex >> 16) & 0xFF) / 255.0, ((hex >> 8) & 0xFF) / 255.0,
		(hex & 0xFF) / 255.0, a);
}

static void rrect(cairo_t *cr, double x, double y, double w, double h, double r) {
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
	cairo_close_path(cr);
}

static void ellipse(cairo_t *cr, double cx, double cy, double rx, double ry) {
	cairo_save(cr);
	cairo_translate(cr, cx, cy);
	cairo_scale(cr, rx, ry);
	cairo_new_sub_path(cr);
	cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI);
	cairo_restore(cr);
}

/* Fills the current path with a top-to-bottom gradient between two colours. */
static void fill_vgrad(cairo_t *cr, double y0, double y1, uint32_t top, uint32_t bottom) {
	cairo_pattern_t *g = cairo_pattern_create_linear(0, y0, 0, y1);
	cairo_pattern_add_color_stop_rgb(g, 0, ((top >> 16) & 0xFF) / 255.0,
		((top >> 8) & 0xFF) / 255.0, (top & 0xFF) / 255.0);
	cairo_pattern_add_color_stop_rgb(g, 1, ((bottom >> 16) & 0xFF) / 255.0,
		((bottom >> 8) & 0xFF) / 255.0, (bottom & 0xFF) / 255.0);
	cairo_set_source(cr, g);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(g);
}

static void outline(cairo_t *cr, uint32_t hex, double width) {
	rgba(cr, hex, 1);
	cairo_set_line_width(cr, width);
	cairo_stroke(cr);
}

/* A Platinum window of the little desktop: title bar with stripes and a
 * close box, white inside. */
static void window(cairo_t *cr, double x, double y, double w, double h) {
	cairo_rectangle(cr, x, y, w, h);
	rgba(cr, 0xFFFFFF, 1);
	cairo_fill_preserve(cr);
	outline(cr, 0x222222, 0.8);
	cairo_rectangle(cr, x, y, w, 7);
	rgba(cr, 0xD2D2D2, 1);
	cairo_fill_preserve(cr);
	outline(cr, 0x222222, 0.8);
	cairo_set_line_width(cr, 0.5);
	for (int k = 0; k < 3; k++) {
		cairo_move_to(cr, x + 8, y + 1.7 + k * 1.6);
		cairo_line_to(cr, x + w - 3, y + 1.7 + k * 1.6);
	}
	rgba(cr, 0x000000, 0.35);
	cairo_stroke(cr);
	cairo_rectangle(cr, x + 2.2, y + 1.7, 3.6, 3.6);
	rgba(cr, 0xFFFFFF, 1);
	cairo_fill_preserve(cr);
	outline(cr, 0x222222, 0.5);
}

static void desktop(cairo_t *cr) {
	const double sx = 15.5, sy = 13.5, sw = 145, sh = 72;
	cairo_save(cr);
	rrect(cr, sx, sy, sw, sh, 1.2);
	cairo_clip(cr);

	/* The lavender desktop pattern. */
	cairo_rectangle(cr, sx, sy, sw, sh);
	fill_vgrad(cr, sy, sy + sh, 0x8E8EF2, 0x6868CE);
	cairo_new_path(cr);
	cairo_set_line_width(cr, 0.8);
	for (double i = -sh; i < sw; i += 3) {
		cairo_move_to(cr, sx + i, sy + sh);
		cairo_line_to(cr, sx + i + sh, sy);
	}
	rgba(cr, 0xFFFFFF, 0.07);
	cairo_stroke(cr);
	for (double i = -sh + 1.5; i < sw; i += 6) {
		cairo_move_to(cr, sx + i, sy + sh);
		cairo_line_to(cr, sx + i + sh, sy);
	}
	rgba(cr, 0x000028, 0.07);
	cairo_stroke(cr);

	/* Menu bar: the logo, menu titles, the clock. */
	cairo_rectangle(cr, sx, sy, sw, 7);
	rgba(cr, 0xEDEDED, 1);
	cairo_fill(cr);
	cairo_move_to(cr, sx, sy + 7);
	cairo_line_to(cr, sx + sw, sy + 7);
	rgba(cr, 0x333333, 1);
	cairo_set_line_width(cr, 0.6);
	cairo_stroke(cr);
	const uint32_t logo[3] = { 0xF0507A, 0x40B060, 0x5070F0 };
	for (int i = 0; i < 3; i++) {
		cairo_new_path(cr);
		cairo_arc(cr, sx + 5.5, sy + 3.5, 1.7, i * 2 * M_PI / 3 - M_PI / 2,
			(i + 1) * 2 * M_PI / 3 - M_PI / 2);
		rgba(cr, logo[i], 1);
		cairo_set_line_width(cr, 1.5);
		cairo_stroke(cr);
	}
	const double menus[3][2] = { { 13.5, 8 }, { 24.5, 9 }, { 36.5, 7 } };
	for (int i = 0; i < 3; i++) {
		rrect(cr, sx + menus[i][0], sy + 2.4, menus[i][1], 2.4, 1);
		rgba(cr, 0x444444, 1);
		cairo_fill(cr);
	}
	rrect(cr, sx + sw - 17, sy + 2.4, 12, 2.4, 1);
	rgba(cr, 0x444444, 1);
	cairo_fill(cr);

	/* A Finder window behind, with folders in it; a document window in front. */
	window(cr, 28, 26, 66, 38);
	for (int i = 0; i < 4; i++) {
		rrect(cr, 34 + i * 14, 39, 8, 6, 1);
		rgba(cr, 0xA9A9FF, 1);
		cairo_fill_preserve(cr);
		outline(cr, 0x333333, 0.4);
		rrect(cr, 34 + i * 14, 47.5, 8, 1.4, 0.7);
		rgba(cr, 0x777777, 1);
		cairo_fill(cr);
	}
	window(cr, 62, 40, 76, 40);
	for (int j = 0; j < 6; j++) {
		rrect(cr, 67, 52 + j * 4.6, j % 2 ? 54 : 62, 1.6, 0.8);
		rgba(cr, 0x9A9AA6, 1);
		cairo_fill(cr);
	}

	/* Desktop icons: the disk and the Trash. */
	rrect(cr, 144, 25, 11, 7, 1.2);
	rgba(cr, 0xD8D8E0, 1);
	cairo_fill_preserve(cr);
	outline(cr, 0x333333, 0.5);
	rrect(cr, 143, 34, 13, 1.6, 0.8);
	rgba(cr, 0xFFFFFF, 0.85);
	cairo_fill(cr);
	rrect(cr, 146, 66, 7, 9, 1);
	rgba(cr, 0xD8D8E0, 1);
	cairo_fill_preserve(cr);
	outline(cr, 0x333333, 0.5);
	cairo_move_to(cr, 145.3, 67.6);
	cairo_line_to(cr, 153.7, 67.6);
	rgba(cr, 0x333333, 1);
	cairo_set_line_width(cr, 0.5);
	cairo_stroke(cr);

	/* The glass catching the light. */
	cairo_move_to(cr, sx, sy);
	cairo_line_to(cr, sx + 76, sy);
	cairo_line_to(cr, sx + 36, sy + sh);
	cairo_line_to(cr, sx, sy + sh);
	cairo_close_path(cr);
	rgba(cr, 0xFFFFFF, 0.09);
	cairo_fill(cr);
	cairo_restore(cr);
}

void pl_welcome_art(struct pl_canvas *c, int x, int y, double scale) {
	cairo_surface_t *surface = cairo_image_surface_create_for_data(
		(unsigned char *)c->px, CAIRO_FORMAT_ARGB32, c->width, c->height, c->stride * 4);
	cairo_t *cr = cairo_create(surface);
	cairo_translate(cr, x - c->x, y - c->y);
	cairo_scale(cr, scale, scale);
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);

	/* The shadow it stands in. */
	for (int i = 0; i < 4; i++) {
		ellipse(cr, 88, 121.5, 66 - i * 5, 6 - i * 0.9);
		rgba(cr, 0x000000, 0.05 * (i + 1));
		cairo_fill(cr);
	}

	/* The stand: a slim neck on an oval foot. */
	ellipse(cr, 88, 115.5, 31, 4.6);
	fill_vgrad(cr, 111, 120, 0xF2F2F6, 0xB4B4BC);
	outline(cr, 0x7C7C86, 0.8);
	cairo_move_to(cr, 79, 95);
	cairo_line_to(cr, 97, 95);
	cairo_line_to(cr, 101, 115);
	cairo_line_to(cr, 75, 115);
	cairo_close_path(cr);
	cairo_pattern_t *neck = cairo_pattern_create_linear(75, 0, 101, 0);
	cairo_pattern_add_color_stop_rgb(neck, 0, 0.70, 0.70, 0.74);
	cairo_pattern_add_color_stop_rgb(neck, 0.5, 0.96, 0.96, 0.98);
	cairo_pattern_add_color_stop_rgb(neck, 1, 0.66, 0.66, 0.70);
	cairo_set_source(cr, neck);
	cairo_fill_preserve(cr);
	cairo_pattern_destroy(neck);
	outline(cr, 0x7C7C86, 0.8);

	/* The display: a silver case round dark glass, a ZacOS 9 desktop in it. */
	rrect(cr, 8, 6, 160, 92, 7);
	fill_vgrad(cr, 6, 98, 0xF7F7FB, 0xCBCBD3);
	outline(cr, 0x62626C, 1);
	cairo_move_to(cr, 12, 7.3);
	cairo_line_to(cr, 164, 7.3);
	rgba(cr, 0xFFFFFF, 0.8);
	cairo_set_line_width(cr, 0.7);
	cairo_stroke(cr);
	rrect(cr, 14, 12, 148, 74, 2);
	rgba(cr, 0x0A0A12, 1);
	cairo_fill(cr);
	desktop(cr);
	ellipse(cr, 88, 92.2, 1.6, 1.6);
	rgba(cr, 0x9A9AA4, 1);
	cairo_fill(cr);

	/* A slim keyboard and a mouse in front. */
	rrect(cr, 32, 121.8, 90, 9, 2.6);
	fill_vgrad(cr, 121.8, 130.8, 0xF4F4F8, 0xC9C9D1);
	outline(cr, 0x6E6E78, 0.8);
	rgba(cr, 0x3C3C48, 0.55);
	for (int row = 0; row < 2; row++) {
		for (int k = 0; k < 20; k++) {
			rrect(cr, 36 + k * 4.1, 123.4 + row * 2.8, 3.2, 1.6, 0.5);
			cairo_fill(cr);
		}
	}
	rrect(cr, 50, 128.7, 42, 1.7, 0.7);
	cairo_fill(cr);
	rrect(cr, 130, 122, 11, 8.8, 4.4);
	fill_vgrad(cr, 122, 130.8, 0xF4F4F8, 0xC4C4CC);
	outline(cr, 0x6E6E78, 0.8);
	cairo_move_to(cr, 135.5, 122.4);
	cairo_line_to(cr, 135.5, 126.4);
	rgba(cr, 0x7A7A84, 1);
	cairo_set_line_width(cr, 0.5);
	cairo_stroke(cr);

	cairo_destroy(cr);
	cairo_surface_flush(surface);
	cairo_surface_destroy(surface);
}

void pl_welcome_box_origin(int w, int h, int *x, int *y) {
	*x = (w - PL_WELCOME_BOX_W) / 2;
	*y = (h - PL_WELCOME_BOX_H) / 2;
}

void pl_welcome_bar_origin(int bx, int by, int *x, int *y) {
	*x = bx + (PL_WELCOME_BOX_W - PL_WELCOME_BAR_W) / 2;
	*y = by + PL_WELCOME_WELL_TOP + PL_WELCOME_WELL_H + 34;
}

void pl_welcome_box_paint(struct pl_canvas *c, int bx, int by, const struct plat_text *title,
		const struct plat_text *status, double fraction, struct pl_accent accent) {
	const int x1 = bx + PL_WELCOME_BOX_W - 1, y1 = by + PL_WELCOME_BOX_H - 1;
	pl_fill(c, bx + 2, by + 2, x1 + 2, y1 + 2, GRAY(0x2));
	pl_fill(c, bx, by, x1, y1, GRAY(0xD));
	pl_outline(c, bx, by, x1, y1, C_BLACK);
	pl_hline(c, bx + 1, x1 - 1, by + 1, C_WHITE);
	pl_vline(c, bx + 1, by + 1, y1 - 1, C_WHITE);
	pl_hline(c, bx + 2, x1 - 1, y1 - 1, GRAY(0x9));
	pl_vline(c, x1 - 1, by + 2, y1 - 1, GRAY(0x9));

	const int tw = title->ink_r - title->ink_l + 1;
	pl_text(c, title, bx + (PL_WELCOME_BOX_W - tw) / 2, by + 28, C_BLACK);

	/* The computer, in a white well. */
	const int wx = bx + (PL_WELCOME_BOX_W - PL_WELCOME_WELL_W) / 2;
	const int wy = by + PL_WELCOME_WELL_TOP;
	pl_outline(c, wx - 1, wy - 1, wx + PL_WELCOME_WELL_W, wy + PL_WELCOME_WELL_H, GRAY(0x6));
	pl_fill(c, wx, wy, wx + PL_WELCOME_WELL_W - 1, wy + PL_WELCOME_WELL_H - 1, C_WHITE);
	pl_welcome_art(c, wx + (PL_WELCOME_WELL_W - PL_WELCOME_ART_W) / 2,
		wy + (PL_WELCOME_WELL_H - PL_WELCOME_ART_H) / 2, 1.0);

	/* What it is doing, and how far it has got. */
	const int sw = status->ink_r - status->ink_l + 1;
	pl_text(c, status, bx + (PL_WELCOME_BOX_W - sw) / 2, wy + PL_WELCOME_WELL_H + 24, C_BLACK);
	int barx, bary;
	pl_welcome_bar_origin(bx, by, &barx, &bary);
	pl_progress_paint(c, barx, bary, PL_WELCOME_BAR_W, fraction, accent);
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
