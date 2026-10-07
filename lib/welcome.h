#ifndef ZACOS9_WELCOME_H
#define ZACOS9_WELCOME_H

#include "draw.h"
#include "icons.h"
#include "text.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The picture on the Welcome screen (compositor/src/startup.c): a modern
 * computer - a slim display on a stand, showing a ZacOS 9 desktop, with a
 * keyboard and a mouse - where Mac OS's own Welcome screen showed a
 * classic Mac. Drawn as vectors (cairo), so it is crisp at any size.
 */
#define PL_WELCOME_ART_W 176 /* its design size, in pixels at scale 1 */
#define PL_WELCOME_ART_H 132

/* Paints it with its top left at (x, y) in `c`'s coordinates, scaled by
 * `scale` (1.0 is PL_WELCOME_ART_W x PL_WELCOME_ART_H), over what is there. */
void pl_welcome_art(struct pl_canvas *c, int x, int y, double scale);

/*
 * The whole Welcome box, as the startup screen shows it - by the compositor
 * (compositor/src/startup.c) and, drawn ahead of time into pictures, by the
 * boot splash (boot/make-splash.c, boot/plymouth/zacos9.script) - so the
 * two match pixel for pixel. A raised Platinum box with the title, the
 * picture in a white well, the status line and a progress bar; its shadow
 * falls 2 px right and below, outside PL_WELCOME_BOX_W x PL_WELCOME_BOX_H.
 */
#define PL_WELCOME_BOX_W 320
#define PL_WELCOME_WELL_W 204
#define PL_WELCOME_WELL_H 156
#define PL_WELCOME_WELL_TOP 42
#define PL_WELCOME_BAR_W 220
#define PL_WELCOME_BOX_H (PL_WELCOME_WELL_TOP + PL_WELCOME_WELL_H + 26 + 10 + 12 + 24)

/* Top-left of the box centred on a w x h screen. */
void pl_welcome_box_origin(int w, int h, int *x, int *y);
/* The progress bar's frame (PL_PROGRESS_H tall), for a box at (bx, by). */
void pl_welcome_bar_origin(int bx, int by, int *x, int *y);
void pl_welcome_box_paint(struct pl_canvas *c, int bx, int by, const struct plat_text *title,
	const struct plat_text *status, double fraction, struct pl_accent accent);

/* The extensions' icons along the bottom of the screen, left to right,
 * starting a new row above when one is full, as Mac OS did. */
#define PL_PARADE_ICON   32
#define PL_PARADE_GAP    8   /* pixels between icons */
#define PL_PARADE_BOTTOM 12  /* gap from the screen's bottom */
#define PL_PARADE_LEFT   16  /* gap from the screen's left */
void pl_parade_slot(int w, int h, int i, int *x, int *y);
void pl_parade_paint(struct pl_canvas *c, int w, int h, const enum pl_icon_kind *icons, int n);

#ifdef __cplusplus
}
#endif

#endif
