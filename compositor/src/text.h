#ifndef PLATINUM_TEXT_H
#define PLATINUM_TEXT_H

#include <stdint.h>

/*
 * A rendered single line of 1-bit text, stored as an 8-bit coverage mask
 * (every value is 0 or 255; no antialiasing, matching the 1990s screen).
 *
 * TODO(phase 6): the backend is a stand-in (cairo + a system TrueType font).
 * It will be replaced by our own Charcoal-metric bitmap font; this interface
 * stays the same.
 */
struct plat_text {
	int width, height;
	int baseline;     /* row of the baseline within the mask */
	int ink_l, ink_r; /* first/last columns containing ink; -1 if blank */
	int stride;
	uint8_t *mask;
};

/* Renders utf8, truncating with an ellipsis so the ink is at most
 * max_ink_width pixels wide. Returns NULL on failure. */
struct plat_text *text_render(const char *utf8, int max_ink_width);
void text_destroy(struct plat_text *text);

#endif
