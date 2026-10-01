/*
 * Pixel tests for Platinum controls against HIG figure 2-24 (Finder list
 * view windows, lavender accent).
 *
 * usage: test-widgets FIGS_DIR
 */
#include <stdio.h>
#include <stdlib.h>

#include "figcompare.h"
#include "widgets.h"

/* Horizontal scroll bar along the bottom of each Finder window: from the
 * content edge (x=5 / x=285) to where it meets the vertical bar, thumb at
 * the far left. */
static int run_hbar(const char *dir, const char *name, int fig_x, int *ran) {
	const struct pl_scrollbar sb = { .length = 254, .enabled = true, .thumb = 0 };
	int w = sb.length, h = SB_WIDTH;
	uint32_t *px = calloc((size_t)w * h, sizeof(*px));
	struct pl_canvas c = { .px = px, .stride = w, .width = w, .height = h };
	pl_scrollbar_paint(&c, 0, 0, &sb, PL_ACCENT_DEFAULT);
	/* The bottom border row is the window's well line; compare all of it. */
	const struct fig_rect regions[] = { { 0, 0, w, h } };
	int fails = fig_compare(name, dir, "img-038-058.png", fig_x, 153, px, w, h, regions, 1);
	free(px);
	if (fails >= 0) {
		*ran = 1;
	}
	return fails > 0 ? fails : 0;
}

static int run_hit_tests(void) {
	const struct pl_scrollbar sb = { .length = 254, .enabled = true, .thumb = 40 };
	int fails = 0;
	struct { int along; enum sb_part want; } cases[] = {
		{ 0, SB_NONE }, { 5, SB_DEC_ARROW }, { 30, SB_DEC_PAGE },
		{ 60, SB_THUMB_PART }, { 120, SB_INC_PAGE }, { 245, SB_INC_ARROW },
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		enum sb_part got = sb_hit(&sb, cases[i].along);
		if (got != cases[i].want) {
			fprintf(stderr, "FAIL sb_hit(%d) = %d, want %d\n",
				cases[i].along, got, cases[i].want);
			fails++;
		}
	}
	printf("%s scroll bar hit testing\n", fails ? "FAIL" : "ok  ");
	return fails;
}

int main(int argc, char *argv[]) {
	const char *dir = argc > 1 ? argv[1] : "tools/measure/figs";
	int ran = 0;
	int fails = run_hit_tests();
	fails += run_hbar(dir, "horizontal scroll bar, left window (fig 2-24)", 5, &ran);
	return fig_exit_status(fails, ran);
}
