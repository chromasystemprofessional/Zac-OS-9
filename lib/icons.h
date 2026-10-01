#ifndef PLATINUM_ICONS_H
#define PLATINUM_ICONS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Finder icons: original artwork drawn from simple shapes.
 * TODO(phase 6): replace with hand-drawn 32x32 and 16x16 pixel art.
 */

#include "draw.h"

enum pl_icon_kind {
	PL_ICON_FOLDER,
	PL_ICON_DOCUMENT,
	PL_ICON_APPLICATION,
	PL_ICON_DISK,
	PL_ICON_TRASH_EMPTY,
	PL_ICON_TRASH_FULL,
	PL_ICON_COUNT,
};

#define PL_ICON_LARGE 32
#define PL_ICON_SMALL 16

/* size x size ARGB pixels, alpha 0 or 255. size is 32 or 16. */
const uint32_t *pl_icon(enum pl_icon_kind kind, int size);

/* Draw an icon; selected icons are darkened, as the Finder does.
 * TODO: measure the exact selected transform. */
void pl_icon_paint(struct pl_canvas *c, int x, int y, enum pl_icon_kind kind,
		int size, bool selected);

#ifdef __cplusplus
}
#endif

#endif
