#ifndef ZACOS9_DECOR_H
#define ZACOS9_DECOR_H

#include <stdbool.h>
#include <stdint.h>

#include "draw.h"
#include "text.h"

/*
 * Platinum window frames, drawn pixel-for-pixel from
 * docs/reference/zacos9-window.md. All coordinates are frame-local:
 * (0,0) is the outer border's top-left, W x H excludes the drop shadow.
 *
 * This module is pure (no wlroots) so it can be unit-tested.
 */

enum decor_style {
	DECOR_STYLE_DOCUMENT,      /* HIG figure 5-1 */
	DECOR_STYLE_MOVABLE_MODAL, /* dialogs: title bar, no boxes (HIG fig 3-2) */
	/* A Sniffer pop-up window (Mac OS 8/9): a tab on top of a body that
	 * rises from the bottom of the screen; collapsed, only the tab. */
	DECOR_STYLE_POPUP,
};

/* Document-window margins around the client surface. */
#define DECOR_LEFT 6
#define DECOR_RIGHT 6
#define DECOR_TOP 22
#define DECOR_BOTTOM 6
#define DECOR_SHADOW 1

struct decor_margins {
	int left, right, top, bottom;
};
struct decor_margins decor_margins(enum decor_style style);
/* Height of a collapsed (WindowShaded) frame, excluding shadow. */
#define DECOR_COLLAPSED_H 22
/* A pop-up window's tab: the title row's height. TODO: measure against
 * a Mac OS 9 pop-up window; drawn to match the document title bar. */
#define DECOR_TAB_H 20
/* A pop-up's body corners (top-left, top-right) that resize it. */
#define DECOR_POPUP_CORNER 20
/* Resize-box cell: top-left corner relative to the frame's bottom-right. */
#define DECOR_GROW_INSET 21

enum decor_part {
	DECOR_PART_NONE,     /* outside the frame (or on the shadow) */
	DECOR_PART_CLIENT,
	DECOR_PART_DRAG,     /* title bar or frame: moves the window */
	DECOR_PART_CLOSE,
	DECOR_PART_ZOOM,
	DECOR_PART_COLLAPSE,
	DECOR_PART_GROW,
	DECOR_PART_BORDER,   /* a dialog's frame: clicks do nothing */
	DECOR_PART_TAB,      /* a pop-up window's tab */
	DECOR_PART_GROW_TOP_LEFT,  /* a pop-up body's top corners */
	DECOR_PART_GROW_TOP_RIGHT,
};

struct decor_state {
	enum decor_style style;
	int width, height; /* W x H */
	bool active;
	bool collapsed;
	bool has_close, has_zoom, has_collapse, has_grow;
	enum decor_part pressed; /* box being held down, if any */
	const struct plat_text *title;
	/* A proxy icon before the title (Mac OS 9's Finder windows): a
	 * pl_icon_kind + 1, or 0 for none. */
	int proxy;
	/* A pop-up window's tab: its left edge, frame-local. */
	int tab_x;
};

/* Height of the frame as drawn (collapsed, a title bar or a tab). */
int decor_frame_height(const struct decor_state *st);
/* A pop-up window's tab width for its title. */
int decor_tab_width(const struct decor_state *st);

/* Paints the part of the frame that falls inside the canvas, which is
 * positioned in frame-local coordinates. */
void decor_paint(struct pl_canvas *canvas, const struct decor_state *st);
enum decor_part decor_hit(const struct decor_state *st, int x, int y);

/* (x, y), frame-local, falls on the title text (or the blank between it
 * and the stripes). */
bool decor_title_hit(const struct decor_state *st, int x, int y);

/* (x, y), frame-local, falls on the proxy icon. */
bool decor_proxy_hit(const struct decor_state *st, int x, int y);

/* Widest title ink that fits between the boxes, for text_render(). */
int decor_title_max_width(const struct decor_state *st);

/* Box origin (top-left of the 12x12 box) for a title-bar part. */
void decor_box_origin(const struct decor_state *st, enum decor_part part,
		int *bx, int *by);

#endif
