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

/* Small-bevel button (HIG figures 2-12 to 2-14, de-stretched): a 2 px
 * bevel, raised or pressed, any size. Used by the Finder's button view. */
void pl_bevel_button_paint(struct pl_canvas *c, int x, int y, int w, int h, bool pressed);

/* Primary group box (HIG figures 2-37, 2-38): an engraved rectangle
 * (#888 with a white line below and right of it). A title sits on the
 * top line, its ink PL_GROUP_TITLE_X in, with 4 px of the line cleared
 * either side. Its baseline is the top line's row. */
#define PL_GROUP_TITLE_X 13
void pl_group_box_paint(struct pl_canvas *c, int x0, int y0, int x1, int y1,
		const struct plat_text *title);

/* List box (HIG figure 2-25): a black frame around white rows of 16 px
 * (system font, baseline at +11, ink 3 px in from the frame), the
 * selected row filled with the highlight colour, a vertical scroll bar
 * sharing the frame's right edge, and when focused a 2 px accent ring. */
#define PL_LIST_ROW_H 16
struct pl_list {
	const struct plat_text *const *rows;
	int n;
	int selected; /* -1 for none */
	int top;      /* first visible row */
	bool focused;
};
/* x0..x1, y0..y1 is the black frame. */
void pl_list_paint(struct pl_canvas *c, int x0, int y0, int x1, int y1,
		const struct pl_list *list, struct pl_accent accent, uint32_t highlight);
int pl_list_visible_rows(int y0, int y1);
/* Row under (x, y), or -1 (the scroll bar and frame are not rows). */
int pl_list_row_at(int x0, int y0, int x1, int y1, const struct pl_list *list, int x, int y);
/* The list's scroll bar, for hit testing with sb_hit(); along = y - y0. */
struct pl_scrollbar pl_list_scrollbar(int y0, int y1, const struct pl_list *list);

/* Tab control (HIG figures 2-30, 2-33): tabs 21 rows tall with slanted
 * sides above a raised pane. (x0, y0) is the top-left of the area; the
 * pane's top line is at y0 + PL_TAB_H. The selected tab is in front and
 * opens into the pane. */
#define PL_TAB_H 21
void pl_tabs_paint(struct pl_canvas *c, int x0, int y0, int x1, int y1,
		const struct plat_text *const *labels, int n, int selected);
/* The tab under (x, y), or -1. */
int pl_tabs_hit(int x0, int y0, const struct plat_text *const *labels, int n, int x, int y);

/* Determinate progress indicator (HIG figures 2-45, 3-31): 12 px tall
 * between its black lines, `w` wide including them, with a one-pixel
 * bevel outside. `fraction` is 0..1. */
#define PL_PROGRESS_H 12
void pl_progress_paint(struct pl_canvas *c, int x, int y, int w, double fraction,
		struct pl_accent accent);

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
