#ifndef PLATINUM_DECOR_H
#define PLATINUM_DECOR_H

#include <stdbool.h>
#include <stdint.h>

#include "draw.h"
#include "text.h"

/*
 * Platinum document-window frame, drawn pixel-for-pixel from
 * docs/reference/platinum-window.md. All coordinates are frame-local:
 * (0,0) is the outer border's top-left, W x H excludes the drop shadow.
 *
 * This module is pure (no wlroots) so it can be unit-tested.
 */

/* Decoration margins around the client surface. */
#define DECOR_LEFT 6
#define DECOR_RIGHT 6
#define DECOR_TOP 22
#define DECOR_BOTTOM 6
#define DECOR_SHADOW 1
/* Height of a collapsed (WindowShaded) frame, excluding shadow. */
#define DECOR_COLLAPSED_H 22
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
};

struct decor_state {
	int width, height; /* W x H */
	bool active;
	bool collapsed;
	bool has_close, has_zoom, has_collapse, has_grow;
	enum decor_part pressed; /* box being held down, if any */
	const struct plat_text *title;
};

/* Paints the part of the frame that falls inside the canvas, which is
 * positioned in frame-local coordinates. */
void decor_paint(struct pl_canvas *canvas, const struct decor_state *st);
enum decor_part decor_hit(const struct decor_state *st, int x, int y);

/* Widest title ink that fits between the boxes, for text_render(). */
int decor_title_max_width(const struct decor_state *st);

/* Box origin (top-left of the 12x12 box) for a title-bar part. */
void decor_box_origin(const struct decor_state *st, enum decor_part part,
		int *bx, int *by);

#endif
