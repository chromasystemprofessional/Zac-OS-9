/* Hint Bubbles over the menu bar's menus (hints.c). */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "draw.h"
#include "menubar.h"

int main(void) {
	char buf[256];
	struct mb_item it = { .label = "Move To Trash", .action = ACT_FINDER };
	assert(strstr(hint_text(&it, buf, sizeof(buf)), "Trash"));
	it.action = ACT_DBUSMENU; /* a program's own menu: no hint */
	assert(!hint_text(&it, buf, sizeof(buf)));
	it = (struct mb_item){ .label = "Scribble", .action = ACT_ACTIVATE_APP };
	assert(strstr(hint_text(&it, buf, sizeof(buf)), "Makes Scribble the active"));
	it = (struct mb_item){ .label = "Mouse Practice", .action = ACT_LAUNCH };
	assert(strstr(hint_text(&it, buf, sizeof(buf)), "double-clicking"));
	it = (struct mb_item){ .label = "Something Unknown", .action = ACT_LAUNCH };
	assert(!hint_text(&it, buf, sizeof(buf)));

	const char *text = "Moves the selected items to the Trash. They are deleted when you empty it.";
	int w, h;
	uint32_t *px = hint_paint(text, false, &w, &h, NULL);
	assert(px && w > HINT_TAIL_W + 20 && w <= 180 + 2 * 5 + 2 + HINT_TAIL_W);
	assert(h >= 2 * 12); /* wrapped onto more than one line */
	assert(px[HINT_TIP_Y * w] == GRAY(0));             /* the tail's tip */
	assert(px[0] == 0);                                  /* outside: clear */
	assert(px[(h / 2) * w + w - 3] == RGB(0xFF, 0xFF, 0xCC)); /* inside */
	assert(px[(h / 2) * w + w - 1] == GRAY(0));        /* the frame */
	free(px);
	int lw, lh, rw, rh;
	uint32_t *left = hint_paint(text, false, &lw, &lh, NULL);
	uint32_t *right = hint_paint(text, true, &rw, &rh, NULL);
	assert(left && right && lw == rw && lh == rh);
	assert(right[HINT_TIP_Y * rw + rw - 1] == GRAY(0)); /* tail on the right */
	/* Only the bubble turns around: the body is the same shifted by the
	 * tail's width, and the text is not mirrored with it. */
	for (int y = HINT_TIP_Y + HINT_TAIL_H + 1; y < lh; y++) {
		for (int x = 0; x < lw - HINT_TAIL_W; x++) {
			assert(right[y * rw + x] == left[y * lw + x + HINT_TAIL_W]);
		}
	}
	free(left);
	free(right);
	return 0;
}
