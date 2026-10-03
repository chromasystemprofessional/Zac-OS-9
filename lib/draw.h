#ifndef PLATINUM_DRAW_H
#define PLATINUM_DRAW_H

#ifdef __cplusplus
extern "C" {
#endif

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
/* Appearance accent color: a ramp of shades (docs/reference/platinum-finder.md).
 * Scroll thumbs use the light end; menu highlights the dark end (top row
 * `dark`, body `shadow`, bottom row `deep`). */
struct pl_accent {
	uint32_t corner;  /* scroll thumb top-left pixel */
	uint32_t grip_hi; /* scroll grip highlight */
	uint32_t light;   /* scroll thumb edges, grip ridges */
	uint32_t body;    /* scroll thumb */
	uint32_t dark;    /* thumb edges; menu highlight top row */
	uint32_t shadow;  /* grip shadow; menu highlight body */
	uint32_t deep;    /* menu highlight bottom row */
};
/* Lavender, the Mac OS 8 default (HIG figures 2-24, 4-1). */
#define PL_ACCENT_DEFAULT ((struct pl_accent){ \
	.corner = RGB(0xEE, 0xEE, 0xEE), .grip_hi = RGB(0xEE, 0xEE, 0xEE), \
	.light = RGB(0xCC, 0xCC, 0xFF), .body = RGB(0x99, 0x99, 0xFF), \
	.dark = RGB(0x66, 0x66, 0xCC), .shadow = RGB(0x33, 0x33, 0x99), \
	.deep = RGB(0x00, 0x00, 0x88) })
/* Green (HIG figure 2-26). TODO: its menu-highlight bottom row is unknown. */
#define PL_ACCENT_GREEN ((struct pl_accent){ \
	.corner = RGB(0xFF, 0xFF, 0xFF), .grip_hi = RGB(0xCC, 0xFF, 0xCC), \
	.light = RGB(0x66, 0xFF, 0x99), .body = RGB(0x33, 0xCC, 0x66), \
	.dark = RGB(0x33, 0x99, 0x66), .shadow = RGB(0x00, 0x66, 0x33), \
	.deep = RGB(0x00, 0x33, 0x00) })

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

/* Draw an ARGB image with real (0-255) alpha, blended against what is
 * already on the canvas: a resolved application icon, not 1990s pixel
 * art. `selected` applies the same darkening pl_icon_paint_label does. */
void pl_image_blend(struct pl_canvas *c, int x, int y, const uint32_t *px, int w, int h,
		bool selected);

#ifdef __cplusplus
}
#endif

#endif
