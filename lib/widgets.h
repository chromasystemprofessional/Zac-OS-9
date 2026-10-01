#ifndef PLATINUM_WIDGETS_H
#define PLATINUM_WIDGETS_H

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

#endif
