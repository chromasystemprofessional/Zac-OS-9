/*
 * Pixel tests for the menu bar and pull-down menus against HIG figures
 * 4-1 and 4-2. Text and icons are excluded (the font is still a stand-in).
 *
 * usage: test-menudraw FIGS_DIR
 */
#include <stdio.h>
#include <stdlib.h>

#include "figcompare.h"
#include "menudraw.h"

#define MAX_REGIONS 12
#define ITEM(l, k, e) { .label = (l), .key = (k), .enabled = (e) }
#define SEP { .label = NULL }

static uint32_t *render_menu(const struct menu_item *items, int n, int w, int h) {
	/* One extra column and row for the shadow. */
	uint32_t *px = calloc((size_t)(w + 1) * (h + 1), sizeof(*px));
	struct pl_canvas c = { .px = px, .stride = w + 1, .width = w + 1, .height = h + 1 };
	menu_paint(&c, items, n, w, h, -1, PL_ACCENT_DEFAULT);
	return px;
}

/* Frame, bevels, shadow and separators: everything except the text area
 * (x = 17 .. W-12) of item rows. */
static int compare_menu(const char *name, const char *dir, const char *figure,
		int fig_x, int fig_y, const struct menu_item *items, int n, int w, int h) {
	uint32_t *px = render_menu(items, n, w, h);
	struct fig_rect regions[MAX_REGIONS] = {
		{ 0, 0, 30, 1 },         /* top border, left of the figure's pointer */
		{ 0, 1, 17, h },         /* left side + bottom-left shadow corner */
		{ w - 6, 1, 7, h },      /* right bevel + shadow (key glyphs vary) */
		{ 0, h - 3, w + 1, 4 },  /* bottom rows + shadow */
	};
	int r = 4, top = 1;
	for (int i = 0; i < n && r < MAX_REGIONS; i++) {
		int rh = items[i].label ? MENU_ITEM_H : MENU_SEP_H;
		if (!items[i].label) {
			regions[r++] = (struct fig_rect){ 0, top, w + 1, rh };
		}
		top += rh;
	}
	int fails = fig_compare(name, dir, figure, fig_x, fig_y, px, w + 1, h + 1,
		regions, MAX_REGIONS);
	free(px);
	return fails;
}

static int run_edit_menu(const char *dir, int *ran) {
	struct plat_text *t = text_render("x", 1000), *k = text_render("Z", 1000);
	const struct menu_item items[] = {
		ITEM(t, k, false), SEP,
		ITEM(t, k, false), ITEM(t, k, true), ITEM(t, k, false), ITEM(t, NULL, false),
		ITEM(t, k, true), ITEM(t, NULL, true), SEP, ITEM(t, NULL, true),
	};
	int n = sizeof(items) / sizeof(items[0]);
	int fails = compare_menu("Edit menu (fig 4-1)", dir, "img-091-125.png",
		69, 19, items, n, 129, 142);
	text_destroy(t);
	text_destroy(k);
	if (fails >= 0) {
		*ran = 1;
	}
	return fails > 0 ? fails : 0;
}

static int run_help_menu(const char *dir, int *ran) {
	struct plat_text *t = text_render("x", 1000), *k = text_render("?", 1000);
	const struct menu_item items[] = {
		ITEM(t, NULL, true), SEP, ITEM(t, NULL, true), SEP, ITEM(t, k, true),
	};
	int fails = compare_menu("Help menu (fig 4-2)", dir, "img-092-126.png",
		4, 19, items, 5, 122, 62);
	text_destroy(t);
	text_destroy(k);
	if (fails >= 0) {
		*ran = 1;
	}
	return fails > 0 ? fails : 0;
}

static int run_menu_bar(const char *dir, int *ran) {
	const int W = 256;
	uint32_t *px = calloc((size_t)W * MBAR_HEIGHT, sizeof(*px));
	struct pl_canvas c = { .px = px, .stride = W, .width = W, .height = MBAR_HEIGHT };
	mbar_paint_background(&c, W);
	/* Edit's highlight as measured; no text. */
	struct mbar_title edit = { .hi_l = 69, .hi_r = 111, .ink_l = 79, .ink_r = 101 };
	mbar_paint_titles(&c, &edit, 1, 0, PL_ACCENT_DEFAULT);

	/* The figure is cropped at x=256 (no right-hand corner), the pointer
	 * covers x=100..116 at the bottom, and text/icon rows are excluded. */
	const struct fig_rect regions[] = {
		{ 0, 0, 69, 2 }, { 0, 17, 69, 3 },      /* bar left of Edit */
		{ 112, 0, 135, 2 }, { 117, 17, 130, 3 }, /* bar right of Edit */
		{ 0, 0, 9, 9 },                          /* rounded corner */
		{ 69, 0, 10, 19 },                       /* highlight left pad */
		{ 69, 19, 31, 1 },                       /* line under highlight */
	};
	int fails = fig_compare("menu bar (fig 4-1)", dir, "img-091-125.png", 0, 0,
		px, W, MBAR_HEIGHT, regions, sizeof(regions) / sizeof(regions[0]));
	free(px);
	if (fails >= 0) {
		*ran = 1;
	}
	return fails > 0 ? fails : 0;
}

static int run_layout_tests(void) {
	int fails = 0;
	/* Width rule: W = 18 + ink + 12, checked against both figures. */
	struct plat_text fake = { .ink_l = 0, .ink_r = 98 }; /* "Show Clipboard" */
	struct menu_item one = ITEM(&fake, NULL, true);
	int w, h;
	menu_measure(&one, 1, &w, &h);
	if (w != 129 || h != 18) {
		fprintf(stderr, "FAIL menu_measure: %dx%d, want 129x18\n", w, h);
		fails++;
	}
	struct menu_item items[] = { ITEM(&fake, NULL, true), SEP, ITEM(&fake, NULL, true) };
	if (menu_item_at(items, 3, 2 + 16 + 6 + 16, 5) != 0 ||
			menu_item_at(items, 3, 40, 19) != -1 ||
			menu_item_at(items, 3, 40, 25) != 2) {
		fprintf(stderr, "FAIL menu_item_at\n");
		fails++;
	}
	/* Title spacing as in figure 4-1: File ink 43..63, Edit ink from 79,
	 * Edit highlight from 69. */
	struct plat_text file = { .ink_l = 0, .ink_r = 20 }, edit = { .ink_l = 0, .ink_r = 22 };
	struct mbar_title titles[] = { { .text = &file }, { .text = &edit } };
	mbar_layout_left(titles, 2);
	if (titles[0].ink_l != 43 || titles[0].ink_r != 63 || titles[1].ink_l != 79 ||
			titles[1].hi_l != 69) {
		fprintf(stderr, "FAIL mbar_layout_left: %d..%d, %d (hi %d)\n",
			titles[0].ink_l, titles[0].ink_r, titles[1].ink_l, titles[1].hi_l);
		fails++;
	}
	printf("%s layout\n", fails ? "FAIL" : "ok  ");
	return fails;
}

int main(int argc, char *argv[]) {
	const char *dir = argc > 1 ? argv[1] : "tools/measure/figs";
	int ran = 0;
	int fails = run_layout_tests();
	fails += run_menu_bar(dir, &ran);
	fails += run_edit_menu(dir, &ran);
	fails += run_help_menu(dir, &ran);
	return fig_exit_status(fails, ran);
}
