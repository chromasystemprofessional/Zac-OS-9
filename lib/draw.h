#ifndef ZACOS9_DRAW_H
#define ZACOS9_DRAW_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pixel primitives shared by everything that draws Platinum chrome
 * (compositor frames, menu bar, menus). No antialiasing, no scaling:
 * 1990s screen pixels. Output scaling happens later, nearest-neighbour:
 * by a whole number, or at 1.25, 1.5 and 1.75 through pl_snap below.
 */

#include <stdbool.h>
#include <stdint.h>

#include "text.h"

/* Mac 8-bit system palette grays are multiples of 0x111111. */
#define GRAY(n) (0xFF000000u | (uint32_t)(n) * 0x111111u)
#define RGB(r, g, b) (0xFF000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (b))

#define C_BLACK GRAY(0x0)
#define C_WHITE GRAY(0xF)
/* Appearance accent color: a ramp of shades (docs/reference/zacos9-finder.md).
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

struct pl_text_log;

/* A rectangle of ARGB8888 pixels positioned in some local coordinate
 * space; drawing outside it is clipped. With a text log, the text drawn
 * on it is also remembered, to be drawn again at a 1.25x, 1.5x or 1.75x
 * screen's pixels in its own font (pl_snap_scale_canvas). */
struct pl_canvas {
	uint32_t *px;
	int stride; /* in pixels */
	int x, y, width, height;
	struct pl_text_log *log; /* NULL: none */
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

/*
 * Fractional scales, in quarters (1.25 = 5, 1.5 = 6, 1.75 = 7; 2 = 8).
 * A 1x pixel u logical pixels from a screen's origin falls on the screen's
 * pixels [pl_snap(u), pl_snap(u + 1)): one or two of them, never a part,
 * the same wherever it is drawn, and by the rounding zacos9-wm places
 * buffers with (wlroots' round(u * scale)), so a buffer made this way is
 * shown pixel for pixel. Every 4 / gcd(quarters, 4) logical pixels the
 * pattern repeats.
 */
int pl_snap(int u, int quarters);
/* The 1x pixel the screen's pixel d shows. */
int pl_unsnap(int d, int quarters);
/* A 1x image (w x h) whose top left is (ux, uy) logical pixels from the
 * screen's origin, at the screen's scale: dst is pl_snap(ux + w) -
 * pl_snap(ux) by pl_snap(uy + h) - pl_snap(uy) pixels, each the 1x pixel
 * it shows. */
void pl_snap_scale(const uint32_t *src, int w, int h, int ux, int uy, int quarters,
	uint32_t *dst);

/* A log of the text drawn into a buffer (base, `stride` pixels a row) by
 * pl_text on canvases that carry it, and of the pixels it covered. */
struct pl_text_log *pl_text_log_new(const uint32_t *base, int stride);
void pl_text_log_free(struct pl_text_log *log);
/* pl_snap_scale of a whole canvas's buffer (stride == width), its text
 * drawn again: the pixels the 1x letters covered as they were (unless
 * painted over since), enlarged, then each text in the font for that
 * screen (text_render_scaled) where its 1x pen and baseline fall. Even
 * strokes where enlarged 1x letters would have some one pixel thick and
 * some two. Without a log (or with ZACOS9_SCALED_FONTS=0 in the
 * environment, to compare pictures), pl_snap_scale. */
void pl_snap_scale_canvas(const struct pl_canvas *c, int ux, int uy, int quarters, uint32_t *dst);

#ifdef __cplusplus
}
#endif

#endif
