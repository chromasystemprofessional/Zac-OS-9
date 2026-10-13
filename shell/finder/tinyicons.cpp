#include "tinyicons.h"

#include <cstdlib>

/* 0 outline, 1 fill, . clear. */
static const char *const FOLDER[TINY_ICON] = {
	"............",
	"............",
	".0000.......",
	"011110......",
	"000000000000",
	"011111111110",
	"011111111110",
	"011111111110",
	"011111111110",
	"011111111110",
	"000000000000",
	"............",
};
static const char *const DOCUMENT[TINY_ICON] = {
	"..000000....",
	"..0111100...",
	"..01111010..",
	"..011110000.",
	"..011111110.",
	"..011111110.",
	"..011111110.",
	"..011111110.",
	"..011111110.",
	"..011111110.",
	"..011111110.",
	"..000000000.",
};
static const char *const APPLICATION[TINY_ICON] = {
	".....00.....",
	"....0110....",
	"...011110...",
	"..01111110..",
	".0111111110.",
	"011111111110",
	"011111111110",
	".0111111110.",
	"..01111110..",
	"...011110...",
	"....0110....",
	".....00.....",
};
static const char *const DISK[TINY_ICON] = {
	"............",
	"............",
	"000000000000",
	"011111111110",
	"011111111110",
	"011111111110",
	"011111111110",
	"011111110010",
	"011111111110",
	"000000000000",
	"............",
	"............",
};
static const char *const TRASH[TINY_ICON] = {
	"....0000....",
	".0000000000.",
	".0111111110.",
	".0000000000.",
	"..01111110..",
	"..01010110..",
	"..01010110..",
	"..01010110..",
	"..01010110..",
	"..01010110..",
	"..01111110..",
	"..00000000..",
};

void paintTinyIcon(pl_canvas *c, const Item &item, int x, int y, bool highlight) {
	const char *const *art = DOCUMENT;
	uint32_t fill = C_WHITE;
	switch (item.iconKind()) {
	case PL_ICON_FOLDER:
	case PL_ICON_SHARED_FOLDER:
		art = FOLDER;
		fill = 0xFFCCCCFF; /* the Platinum folders' lavender */
		break;
	case PL_ICON_APPLICATION:
		art = APPLICATION;
		fill = GRAY(0xC);
		break;
	case PL_ICON_DISK:
		art = DISK;
		fill = GRAY(0xD);
		break;
	case PL_ICON_TRASH_EMPTY:
	case PL_ICON_TRASH_FULL:
		art = TRASH;
		fill = GRAY(0xD);
		break;
	default:
		break;
	}
	if (const uint32_t label = item.labelColor()) {
		fill = label;
	}
	if (highlight) {
		/* As the Finder darkens a selected icon: each channel halved. */
		fill = 0xFF000000u | ((fill >> 1) & 0x7F7F7Fu);
	}
	for (int j = 0; j < TINY_ICON; j++) {
		for (int i = 0; i < TINY_ICON; i++) {
			const char p = art[j][i];
			if (p == '0') {
				pl_put(c, x + i, y + j, C_BLACK);
			} else if (p == '1') {
				pl_put(c, x + i, y + j, fill);
			}
		}
	}
}
