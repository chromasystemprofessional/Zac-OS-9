/*
 * The Platinum 2026 logo: an original 16x16 color mark for the far left of
 * the menu bar (where Mac OS put the Apple logo, which we can't use). A
 * disc in six rainbow bands, as a nod to the era's 6-color stripes. Its ink
 * spans 12 columns, the same as the slot measured in HIG figure 4-1.
 */
#include <stddef.h>
#include <stdint.h>

#include "menubar.h"

static const char *const rows[MBAR_ICON_SIZE] = {
	"................",
	"......gggg......",
	"....gggggggg....",
	"...yyyyyyyyyy...",
	"..yyyyyyyyyyyy..",
	"..oooooooooooo..",
	"..oooooooooooo..",
	"..rrrrrrrrrrrr..",
	"..rrrrrrrrrrrr..",
	"..pppppppppppp..",
	"...pppppppppp...",
	"....bbbbbbbb....",
	"......bbbb......",
	"................",
	"................",
	"................",
};

static uint32_t color_of(char c) {
	switch (c) {
	case 'g': return RGB(0x00, 0xBB, 0x00);
	case 'y': return RGB(0xFF, 0xCC, 0x33);
	case 'o': return RGB(0xFF, 0x66, 0x00);
	case 'r': return RGB(0xDD, 0x00, 0x00);
	case 'p': return RGB(0x99, 0x33, 0x99);
	case 'b': return RGB(0x00, 0x66, 0xCC);
	default: return 0; /* transparent */
	}
}

const uint32_t *logo_pixels(void) {
	static uint32_t px[MBAR_ICON_SIZE * MBAR_ICON_SIZE];
	static int built;
	if (!built) {
		for (int y = 0; y < MBAR_ICON_SIZE; y++) {
			for (int x = 0; x < MBAR_ICON_SIZE; x++) {
				px[y * MBAR_ICON_SIZE + x] = color_of(rows[y][x]);
			}
		}
		built = 1;
	}
	return px;
}
