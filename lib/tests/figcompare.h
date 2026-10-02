#ifndef PLATINUM_FIGCOMPARE_H
#define PLATINUM_FIGCOMPARE_H

/*
 * Compare our rendering against an Apple HIG figure, pixel by pixel, in
 * chosen regions. The figures are fetched by tools/measure/fetch.sh and
 * are never committed. Transparent pixels in our rendering stand for the
 * figure's white page background.
 */

#include <stdint.h>

struct fig_rect {
	int x, y, w, h; /* in our rendering's coordinates */
};

/* Returns the number of mismatched pixels, or -1 if the figure is missing.
 * px/w/h is our rendering, whose (0,0) lands on (fig_x, fig_y). Regions
 * end at the first one with w == 0. Prints a one-line result. */
int fig_compare(const char *name, const char *dir, const char *figure,
		int fig_x, int fig_y, const uint32_t *px, int w, int h,
		const struct fig_rect *regions, int max_regions);

/* Some figures were printed with slightly shifted colours (#EFEFEF for
 * #EEEEEE...). Map figure colours before comparing: pairs of {figure,
 * ours}; n = 0 turns the map off. */
void fig_set_color_map(const uint32_t (*pairs)[2], int n);

/* Compare only where the black (and near-black) pixels are, for figures
 * drawn in other shades but with the same shapes. */
void fig_set_black_only(int on);

/* Exit status for a test binary: 0 pass, 1 fail, 77 skip (meson). */
int fig_exit_status(int failures, int ran);

#endif
