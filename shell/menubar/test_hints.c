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
	it = (struct mb_item){ .label = "Something Unknown", .action = ACT_LAUNCH };
	assert(!hint_text(&it, buf, sizeof(buf)));

	const char *text = "Moves the selected items to the Trash. They are deleted when you empty it.";
	int w, h;
	uint32_t *px = hint_paint(text, false, &w, &h);
	assert(px && w > HINT_TAIL_W + 20 && w <= 180 + 2 * 5 + 2 + HINT_TAIL_W);
	assert(h >= 2 * 12); /* wrapped onto more than one line */
	assert(px[HINT_TIP_Y * w] == GRAY(0));             /* the tail's tip */
	assert(px[0] == 0);                                  /* outside: clear */
	assert(px[(h / 2) * w + w - 3] == RGB(0xFF, 0xFF, 0xCC)); /* inside */
	assert(px[(h / 2) * w + w - 1] == GRAY(0));        /* the frame */
	free(px);
	px = hint_paint(text, true, &w, &h);
	assert(px[HINT_TIP_Y * w + w - 1] == GRAY(0));     /* tail on the right */
	free(px);
	return 0;
}
