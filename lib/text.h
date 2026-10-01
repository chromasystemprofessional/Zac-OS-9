#ifndef PLATINUM_TEXT_H
#define PLATINUM_TEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/*
 * A rendered single line of 1-bit text, stored as an 8-bit coverage mask
 * (every value is 0 or 255; no antialiasing, matching the 1990s screen).
 *
 * TODO(phase 6): the backend is a stand-in (cairo + a system TrueType font).
 * It will be replaced by our own bitmap fonts with the Mac OS 8 metrics;
 * this interface stays the same.
 */
struct plat_text {
	int width, height;
	int baseline;     /* row of the baseline within the mask */
	int ink_l, ink_r; /* first/last columns containing ink; -1 if blank */
	int advance;      /* pen advance in pixels */
	int stride;
	uint8_t *mask;
};

enum pl_font {
	PL_FONT_SYSTEM, /* menus, window titles, buttons (Charcoal 12) */
	PL_FONT_VIEWS,  /* Finder icon labels and lists (Geneva) */
};

/* Renders utf8, truncating with an ellipsis so the ink is at most
 * max_ink_width pixels wide. Returns NULL on failure. */
struct plat_text *text_render_font(const char *utf8, int max_ink_width, enum pl_font font);
/* The system font. */
struct plat_text *text_render(const char *utf8, int max_ink_width);
void text_destroy(struct plat_text *text);

#ifdef __cplusplus
}
#endif

#endif
