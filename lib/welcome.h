#ifndef ZACOS9_WELCOME_H
#define ZACOS9_WELCOME_H

#include "draw.h"
#include "icons.h"
#include "text.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The startup screen, after the Macintosh's of 1984 (measured from a
 * screenshot of its 512x342 screen; nothing of Apple's is used): first
 * Happy Zac on white, then the "Welcome to Zacintosh." box - white, a black
 * line round it and a solid shadow 2 px right and below - with the ZacOS
 * logo at its left. Drawn by the compositor (compositor/src/startup.c) and,
 * ahead of time into pictures, by the boot splash (boot/make-splash.c,
 * boot/plymouth/zacos9.script), and by GRUB (boot/zacos9-bootlogo), so they
 * match pixel for pixel. Everything is that 1984 screen scaled by a whole
 * number for the display (pl_welcome_scale).
 */
#define PL_WELCOME_SCREEN_W 512 /* the 1984 screen */
#define PL_WELCOME_SCREEN_H 342
#define PL_WELCOME_MAX_SCALE 6  /* the largest scale there are pictures for */
#define PL_WELCOME_BOX_X 32     /* the box on that screen, its line included */
#define PL_WELCOME_BOX_Y 64
#define PL_WELCOME_BOX_W 448
#define PL_WELCOME_BOX_H 126
#define PL_WELCOME_SHADOW 2     /* the shadow's offset and width */
#define PL_WELCOME_ICON 32      /* Happy Zac and the logo, square */
#define PL_WELCOME_ICON_X 24    /* the logo, from the box's corner */
#define PL_WELCOME_ICON_Y 25
#define PL_WELCOME_TITLE_X 236  /* the middle of the title's ink, from the box's left */
#define PL_WELCOME_BASELINE 42  /* the title's baseline, from the box's top */
#define PL_WELCOME_TITLE "Welcome to Zacintosh."

/* The scale for a w x h display: the largest whole number the 1984 screen
 * fits in that many times, from 1 to PL_WELCOME_MAX_SCALE. */
int pl_welcome_scale(int w, int h);

/* The pictures (assets/boot), PL_WELCOME_ICON * scale pixels square, ARGB
 * with straight alpha; NULL if missing. Loaded once from $ZACOS9_BOOT_ART,
 * the installed /usr/share/zacos9/boot or the source tree. */
enum pl_boot_art { PL_ART_HAPPY_ZAC, PL_ART_LOGO };
const uint32_t *pl_boot_art(enum pl_boot_art art, int scale);

/* Happy Zac's top-left, centred on a w x h display; and Happy Zac itself on
 * white, filling the canvas. */
void pl_happy_zac_origin(int w, int h, int *x, int *y);
void pl_happy_zac_paint(struct pl_canvas *c, int w, int h);

/* Top-left of the box (its line) on a w x h display: where it was on the
 * 1984 screen, that screen centred on this one. */
void pl_welcome_box_origin(int w, int h, int *x, int *y);
/* The box, at `scale`, its corner at (bx, by), and its shadow outside
 * PL_WELCOME_BOX_W x PL_WELCOME_BOX_H * scale. */
void pl_welcome_box_paint(struct pl_canvas *c, int bx, int by, int scale,
	const struct plat_text *title);

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
