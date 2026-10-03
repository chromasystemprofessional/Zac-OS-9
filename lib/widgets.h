#ifndef ZACOS9_WIDGETS_H
#define ZACOS9_WIDGETS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Platinum controls drawn from docs/reference/zacos9-finder.md.
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
 * (to `bg`) either side. Its baseline is the top line's row.
 * Layout (HIG figure 3-29): items 10 px from the sides and bottom and
 * 12 px from the inside top; boxes 10 px apart side by side, 12 px
 * apart one above the other. */
#define PL_GROUP_TITLE_X 13
#define PL_GROUP_MARGIN 10
#define PL_GROUP_MARGIN_TOP 12
void pl_group_box_paint(struct pl_canvas *c, int x0, int y0, int x1, int y1,
		const struct plat_text *title, uint32_t bg);

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

/* Checkbox (HIG figure 2-8, and the checked boxes of figure 6-1): a 12 px
 * box whose check mark pokes 2 px out of its top right. The label's ink
 * starts PL_CHECKBOX_LABEL_X from the box, baseline 9 rows below its top. */
#define PL_CHECKBOX_SIZE 12
#define PL_CHECKBOX_LABEL_X 17
#define PL_CHECKBOX_BASELINE 9
enum pl_check { PL_CHECK_OFF, PL_CHECK_ON, PL_CHECK_MIXED };
void pl_checkbox_paint(struct pl_canvas *c, int x, int y, enum pl_check value,
		bool pressed, bool enabled, const struct plat_text *label);

/* Radio button (HIG figures 2-4, 2-5): a 12 px shaded bead, a dark dot
 * when on. Labels sit as for checkboxes. */
#define PL_RADIO_SIZE 12
void pl_radio_paint(struct pl_canvas *c, int x, int y, bool on, bool pressed, bool enabled,
		const struct plat_text *label);

/* Horizontal slider (HIG figures 2-17, 2-18): a recessed track with
 * rounded ends and a 15 px accent thumb pointing down at it, with tick
 * marks below. (x, y) is the left end of the track at the thumb's top
 * row; `w` the track's length. `value` is 0..steps-1. */
#define PL_SLIDER_THUMB_W 15
#define PL_SLIDER_H 23 /* thumb and ticks */
void pl_slider_paint(struct pl_canvas *c, int x, int y, int w, int steps, int value,
		bool enabled, struct pl_accent accent);
/* The thumb's left x for a value. */
int pl_slider_thumb_x(int x, int w, int steps, int value);
/* The value nearest to pointer x (for clicks and drags). */
int pl_slider_value_at(int x, int w, int steps, int px);

/* Little arrows (HIG figures 2-20, 2-22): two stacked 13 x 12 buttons
 * sharing a line, 13 x 23 in all, with clipped corners. */
#define PL_ARROWS_W 13
#define PL_ARROWS_H 23
enum pl_arrows_part { PL_ARROWS_NONE, PL_ARROWS_UP, PL_ARROWS_DOWN };
void pl_little_arrows_paint(struct pl_canvas *c, int x, int y, enum pl_arrows_part pressed,
		bool enabled);
enum pl_arrows_part pl_little_arrows_hit(int x, int y, int px, int py);

/* Edit text frame (HIG figures 2-28, 3-30): a white field in a black
 * frame (22 px tall as standard), #888 just outside its top and left,
 * white outside its bottom and right. */
#define PL_EDIT_H 22
void pl_edit_frame_paint(struct pl_canvas *c, int x0, int y0, int x1, int y1);

/* Clock control (HIG figure 2-22): a 23 px tall field (black frame,
 * white inside) with little arrows 2 px to its right. When focused, the
 * ring's colour fills behind both, as in the figure. The caller draws
 * the date or time text and the highlighted part inside the field. */
#define PL_CLOCK_H 23
void pl_clock_paint(struct pl_canvas *c, int x0, int y0, int field_w, bool focused,
		enum pl_arrows_part pressed, bool enabled, struct pl_accent accent, uint32_t bg);
/* The little arrows' x for a clock control at x0 with this field width. */
#define PL_CLOCK_ARROWS_X(x0, field_w) ((x0) + (field_w) + 2)

/* The keyboard focus ring (figures 2-22, 2-25, 3-11): 2 px of the accent's
 * dark shade around x0..x1, y0..y1, outer corners clipped to `bg`. */
void pl_focus_ring_paint(struct pl_canvas *c, int x0, int y0, int x1, int y1,
		struct pl_accent accent, uint32_t bg);

/* Determinate progress indicator (HIG figures 2-45, 3-31): 12 px tall
 * between its black lines, `w` wide including them, with a one-pixel
 * bevel outside. `fraction` is 0..1. */
#define PL_PROGRESS_H 12
void pl_progress_paint(struct pl_canvas *c, int x, int y, int w, double fraction,
		struct pl_accent accent);

/* Pop-up menu button (HIG figures 2-6, 3-25): a rounded black frame
 * holding the current choice and, in a box at its right end, a double
 * triangle. Standard height 20 (figure 3-25); figure 2-6 draws one 19
 * tall. Everything but the top rows is laid out from the bottom.
 * Buttons stack 6 px apart; a label sits to the left, right-aligned,
 * its ink ending PL_POPUP_LABEL_GAP px before the frame, on the same
 * baseline as the choice. */
#define PL_POPUP_H 20
#define PL_POPUP_SPACING 6
#define PL_POPUP_LABEL_GAP 7
#define PL_POPUP_TEXT_X 7     /* pen position of the choice */
#define PL_POPUP_ARROWS_W 21  /* the arrow box, with its left divider */
#define PL_POPUP_BASELINE(h) ((h) - 7)
/* `label` may be NULL. Disabled: dimmed text and arrows (TODO: measure). */
void pl_popup_button_paint(struct pl_canvas *c, int x, int y, int w, int h,
		const struct plat_text *label, bool enabled);

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
