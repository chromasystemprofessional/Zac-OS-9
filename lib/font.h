#ifndef ZACOS9_FONT_H
#define ZACOS9_FONT_H

/*
 * Bitmap fonts compiled from assets/fonts/ (*.pfont) by
 * tools/fonts/build_fonts.py (into fonts_data.h). Private to text.c.
 */

#include <stdint.h>

struct pl_glyph {
	uint32_t cp;
	int8_t top;      /* first row, relative to the baseline (negative = above) */
	uint8_t advance; /* pen advance; also the row width */
	uint8_t rows;
	uint16_t first;  /* index of the first row in the font's row table */
};

struct pl_bitmap_font {
	const char *name;
	int height, baseline; /* line box, and the baseline row within it */
	const struct pl_glyph *glyphs; /* sorted by code point */
	int n;
	const uint16_t *rows; /* bit x set = ink in column x */
};

#endif
