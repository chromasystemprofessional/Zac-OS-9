#ifndef PLATINUM_DRAW_H
#define PLATINUM_DRAW_H

/*
 * Pixel primitives shared by everything that draws Platinum chrome
 * (compositor frames, menu bar, menus). No antialiasing, no scaling:
 * 1990s screen pixels. Output scaling happens later, nearest-neighbour.
 */

#include <stdbool.h>
#include <stdint.h>

#include "text.h"

/* Mac 8-bit system palette grays are multiples of 0x111111. */
#define GRAY(n) (0xFF000000u | (uint32_t)(n) * 0x111111u)
#define RGB(r, g, b) (0xFF000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (b))

#define C_BLACK GRAY(0x0)
#define C_WHITE GRAY(0xF)
/* Appearance accent ("highlight") color. Highlights have a one-row bevel:
 * light top row, base body, dark bottom row (HIG figures 4-1, 4-2). */
struct pl_accent {
	uint32_t light, base, dark;
};
#define PL_ACCENT_DEFAULT ((struct pl_accent){ \
	RGB(0x66, 0x66, 0xCC), RGB(0x33, 0x33, 0x99), RGB(0x00, 0x00, 0x88) })

/* A rectangle of ARGB8888 pixels positioned in some local coordinate
 * space; drawing outside it is clipped. */
struct pl_canvas {
	uint32_t *px;
	int stride; /* in pixels */
	int x, y, width, height;
};

void pl_put(struct pl_canvas *c, int x, int y, uint32_t color);
void pl_hline(struct pl_canvas *c, int x0, int x1, int y, uint32_t color);
void pl_vline(struct pl_canvas *c, int x, int y0, int y1, uint32_t color);
void pl_fill(struct pl_canvas *c, int x0, int y0, int x1, int y1, uint32_t color);
void pl_outline(struct pl_canvas *c, int x0, int y0, int x1, int y1, uint32_t color);

/* Draw a 1-bit text mask so its baseline lands on y and its first ink
 * column on ink_x. */
void pl_text(struct pl_canvas *c, const struct plat_text *t, int ink_x,
		int baseline_y, uint32_t color);

/* Draw rows of hex-digit gray levels ('.' = skip) with the top-left at x,y. */
void pl_grays(struct pl_canvas *c, int x, int y, const char *const *rows, int nrows);

/* Draw an ARGB image (alpha 0 or 255) with the top-left at x,y. */
void pl_image(struct pl_canvas *c, int x, int y, const uint32_t *px, int w, int h);

#endif
