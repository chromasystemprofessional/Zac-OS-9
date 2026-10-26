#ifndef ZACOS9_TEXT_H
#define ZACOS9_TEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/*
 * A rendered single line of 1-bit text, stored as an 8-bit coverage mask
 * (every value is 0 or 255; no antialiasing, matching the 1990s screen).
 * Drawn with ZacOS 9's own bitmap fonts (assets/fonts), which have
 * the Mac OS 8 metrics; characters they lack fall back to DejaVu Sans.
 */
struct plat_text {
	int width, height;
	int baseline;     /* row of the baseline within the mask */
	int ink_l, ink_r; /* first/last columns containing ink; -1 if blank */
	int advance;      /* pen advance in pixels */
	int stride;
	uint8_t *mask;
	/* What it shows (after any ellipsis) and in which enum pl_font, to
	 * draw it again at another scale (text_render_scaled). */
	char *utf8;
	int font;
};

enum pl_font {
	PL_FONT_SYSTEM,       /* menus, window titles, buttons (Charcoal 12 metrics) */
	PL_FONT_VIEWS,        /* Finder icon labels and lists (Geneva 9 metrics) */
	PL_FONT_VIEWS_ITALIC, /* the same, slanted: alias names */
};

/* Renders utf8, truncating with an ellipsis so the ink is at most
 * max_ink_width pixels wide. Returns NULL on failure. */
struct plat_text *text_render_font(const char *utf8, int max_ink_width, enum pl_font font);
/* The system font. */
struct plat_text *text_render(const char *utf8, int max_ink_width);
/* The same text in the font for a 1.25x, 1.5x or 1.75x screen (quarters
 * 5, 6 or 7: assets/fonts/platinum-*-125|150|175.pfont), at the screen's
 * pixels: its pen and baseline where the 1x text's fall on that screen
 * (pl_snap), its letters even. NULL at other scales. */
struct plat_text *text_render_scaled(const struct plat_text *text, int quarters);
void text_destroy(struct plat_text *text);

#ifdef __cplusplus
}
#endif

#endif
