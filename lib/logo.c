/*
 * The Platinum 2026 logo: an original 16x16 mark for the far left of the
 * menu bar (where Mac OS put the Apple logo, which we can't use). A
 * faceted four-pointed platinum sparkle, lit from the top left, with a
 * lavender heart (the default Platinum accent) and a small twinkle. Its
 * ink spans 12 columns, the same as the slot measured in HIG figure 4-1.
 */
#include <stddef.h>
#include <stdint.h>

#include "draw.h"
#include "logo.h"

static const char *const rows[PL_LOGO_SIZE] = {
	"................",
	"............K...",
	".......K...KWK..",
	"......KWK...K...",
	"......KWK.......",
	".....KWWcK......",
	"...KKWWMccKK....",
	"..KWWWMLM999K...",
	"...KKccM99KK....",
	".....Kc99K......",
	"......K9K.......",
	"......K9K.......",
	".......K........",
	"................",
	"................",
	"................",
};

static uint32_t color_of(char c) {
	switch (c) {
	case 'K': return C_BLACK;
	case 'W': return C_WHITE;
	case 'c': return GRAY(0xC);
	case '9': return GRAY(0x9);
	case 'L': return RGB(0xCC, 0xCC, 0xFF);
	case 'M': return RGB(0x99, 0x99, 0xFF);
	default: return 0; /* transparent */
	}
}

const uint32_t *logo_pixels(void) {
	static uint32_t px[PL_LOGO_SIZE * PL_LOGO_SIZE];
	static int built;
	if (!built) {
		for (int y = 0; y < PL_LOGO_SIZE; y++) {
			for (int x = 0; x < PL_LOGO_SIZE; x++) {
				px[y * PL_LOGO_SIZE + x] = color_of(rows[y][x]);
			}
		}
		built = 1;
	}
	return px;
}
