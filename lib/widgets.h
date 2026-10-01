#ifndef PLATINUM_WIDGETS_H
#define PLATINUM_WIDGETS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Platinum controls drawn from docs/reference/platinum-finder.md.
 */

#include "draw.h"

/* Scroll bars are 16 px across, counting both black border lines, which
 * they share with neighbouring lines (window content edge, list header). */
#define SB_WIDTH 16
#define SB_ARROW 14 /* arrow box interior */
#define SB_THUMB 15 /* thumb interior along the bar */

struct pl_scrollbar {
	bool vertical;
	int length;   /* along the bar, including both end border lines */
	bool enabled; /* false: nothing to scroll (flat track, gray arrows) */
	int thumb;    /* thumb offset, 0 .. sb_thumb_range() */
};

/* Largest thumb offset for a bar of this length. */
int sb_thumb_range(int length);

/* Paint with the bar's top-left border pixel at (x, y). */
void pl_scrollbar_paint(struct pl_canvas *c, int x, int y,
		const struct pl_scrollbar *sb, struct pl_accent accent);

/* Push buttons (HIG figures 2-3 and 3-2): 20 px tall. A default button
 * also gets its 3 px ring drawn around the rectangle. */
#define PL_BUTTON_H 20
#define PL_BUTTON_RING 3
#define PL_BUTTON_MIN_W 58 /* HIG: OK and Cancel are 58 x 20 */

enum pl_button_flags {
	PL_BUTTON_DEFAULT = 1 << 0,
	PL_BUTTON_PRESSED = 1 << 1,
	PL_BUTTON_DISABLED = 1 << 2, /* TODO: measure; text is dimmed only */
};

void pl_button_paint(struct pl_canvas *c, int x, int y, int w,
		const struct plat_text *label, unsigned flags);

enum sb_part {
	SB_NONE,
	SB_DEC_ARROW, /* up / left */
	SB_INC_ARROW, /* down / right */
	SB_DEC_PAGE,
	SB_INC_PAGE,
	SB_THUMB_PART,
};
/* Part at bar-local offset `along` (0 = leading border). */
enum sb_part sb_hit(const struct pl_scrollbar *sb, int along);

#ifdef __cplusplus
}
#endif

#endif
