#ifndef PLATINUM_ICONS_H
#define PLATINUM_ICONS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Finder icons: Platinum 2026's own 32x32 and 16x16 pixel art
 * (assets/icons/platinum-icons.picon).
 */

#include "draw.h"

enum pl_icon_kind {
	PL_ICON_FOLDER,
	PL_ICON_DOCUMENT,
	PL_ICON_APPLICATION,
	PL_ICON_DISK,
	PL_ICON_TRASH_EMPTY,
	PL_ICON_TRASH_FULL,
	PL_ICON_CAUTION,    /* alerts */
	PL_ICON_DISK_IMAGE, /* a Macintosh disk or CD image */
	PL_ICON_CLASSIC,    /* a classic Mac OS application (runs in Classic) */
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
/* The same, tinted with a Finder label color (0 = no label). */
void pl_icon_paint_label(struct pl_canvas *c, int x, int y, enum pl_icon_kind kind,
		int size, bool selected, uint32_t label_color);

/*
 * Finder labels: index 0 is "None", 1..7 the Mac OS 8 defaults (Label
 * control panel), colors from the standard 16-color system palette.
 */
#define PL_LABEL_COUNT 8
struct pl_label {
	const char *name;
	uint32_t color; /* ARGB; 0 for None */
};
extern const struct pl_label pl_labels[PL_LABEL_COUNT];

#ifdef __cplusplus
}
#endif

#endif
