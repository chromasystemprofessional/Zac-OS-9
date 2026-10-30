/*
 * Geometry guard: a theme may recolour the window chrome (Step 6a) but
 * must never move the close/zoom/collapse boxes, the title bar, the resize
 * box or the margins -- those positions are hit-tested by zacos9-wm.  These
 * assertions pin them down so a future change (or a theme that tries to
 * override them) fails the build.
 */
#include <assert.h>
#include <stdio.h>

#include "decor.h"
#include "icons.h"

int main(void) {
	/* Margins are fixed by the HIG. */
	assert(DECOR_LEFT == 6);
	assert(DECOR_RIGHT == 6);
	assert(DECOR_TOP == 22);
	assert(DECOR_BOTTOM == 6);
	assert(DECOR_SHADOW == 1);
	assert(DECOR_COLLAPSED_H == 22);
	assert(DECOR_TAB_H == 20);
	assert(DECOR_GROW_INSET == 21);

	struct decor_state st = { .style = DECOR_STYLE_DOCUMENT,
		.width = 214, .height = 227, .active = true,
		.has_close = true, .has_zoom = true, .has_collapse = true,
		.has_grow = true };

	/* Frame height is the full height for a document window. */
	assert(decor_frame_height(&st) == 227);

	/* Box origins are fixed (HIG figure 5-1). */
	int bx, by;
	decor_box_origin(&st, DECOR_PART_CLOSE, &bx, &by);
	assert(bx == 4 && by == 4);
	decor_box_origin(&st, DECOR_PART_ZOOM, &bx, &by);
	assert(bx == 214 - 33 && by == 4);
	decor_box_origin(&st, DECOR_PART_COLLAPSE, &bx, &by);
	assert(bx == 214 - 17 && by == 4);

	/* Margins agree with decor_margins(). */
	struct decor_margins m = decor_margins(DECOR_STYLE_DOCUMENT);
	assert(m.left == DECOR_LEFT && m.right == DECOR_RIGHT);
	assert(m.top == DECOR_TOP && m.bottom == DECOR_BOTTOM);

	/* A pop-up tab keeps its geometry. */
	struct decor_state pop = { .style = DECOR_STYLE_POPUP, .width = 200,
		.height = 120, .active = true, .tab_x = 10 };
	assert(decor_tab_width(&pop) >= 64);
	assert(decor_tab_width(&pop) <= 200);

	printf("decor-geometry: ok\n");
	return 0;
}