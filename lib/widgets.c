#include <string.h>

#include "widgets.h"

/* Push-button art: left columns, one stretchable column, right columns. */
struct nine_slice {
	int left, right, height;
	const char *rows[26];
};
#include "buttons_data.h"

static void paint_nine(struct pl_canvas *c, int x, int y, int w,
		const struct nine_slice *s) {
	for (int j = 0; j < s->height; j++) {
		const char *row = s->rows[j];
		for (int i = 0; i < w; i++) {
			int k = i < s->left ? i : i >= w - s->right ? s->left + 1 + (i - (w - s->right)) : s->left;
			char ch = row[k];
			if (ch != '.') {
				pl_put(c, x + i, y + j, GRAY(ch <= '9' ? ch - '0' : ch - 'a' + 10));
			}
		}
	}
}

void pl_bevel_button_paint(struct pl_canvas *c, int x, int y, int w, int h, bool pressed) {
	const int x1 = x + w - 1, y1 = y + h - 1;
	if (!pressed) {
		/* Raised: #888 / #444 outside, white / #999 inside, #CCC face. */
		pl_fill(c, x + 2, y + 2, x1 - 2, y1 - 2, GRAY(0xC));
		pl_hline(c, x, x1 - 1, y, GRAY(0x8));
		pl_vline(c, x, y, y1 - 1, GRAY(0x8));
		pl_hline(c, x + 1, x1, y1, GRAY(0x4));
		pl_vline(c, x1, y + 1, y1, GRAY(0x4));
		pl_put(c, x1, y, GRAY(0x7));
		pl_put(c, x, y1, GRAY(0x7));
		pl_hline(c, x + 1, x1 - 2, y + 1, C_WHITE);
		pl_vline(c, x + 1, y + 1, y1 - 2, C_WHITE);
		pl_hline(c, x + 2, x1 - 1, y1 - 1, GRAY(0x9));
		pl_vline(c, x1 - 1, y + 2, y1 - 1, GRAY(0x9));
		pl_put(c, x1 - 1, y + 1, GRAY(0xC));
		pl_put(c, x + 1, y1 - 1, GRAY(0xC));
		return;
	}
	/* Pressed: #222 / #666 outside, #555 / #AAA inside, #888 face. */
	pl_fill(c, x + 2, y + 2, x1 - 2, y1 - 2, GRAY(0x8));
	pl_hline(c, x, x1 - 1, y, GRAY(0x2));
	pl_vline(c, x, y, y1 - 1, GRAY(0x2));
	pl_hline(c, x + 1, x1, y1, GRAY(0x6));
	pl_vline(c, x1, y + 1, y1, GRAY(0x6));
	pl_put(c, x1, y, GRAY(0x5));
	pl_put(c, x, y1, GRAY(0x5));
	pl_hline(c, x + 1, x1 - 2, y + 1, GRAY(0x5));
	pl_vline(c, x + 1, y + 1, y1 - 2, GRAY(0x5));
	pl_hline(c, x + 2, x1 - 1, y1 - 1, GRAY(0xA));
	pl_vline(c, x1 - 1, y + 2, y1 - 1, GRAY(0xA));
	pl_put(c, x1 - 1, y + 1, GRAY(0x8));
	pl_put(c, x + 1, y1 - 1, GRAY(0x8));
}

void pl_button_paint(struct pl_canvas *c, int x, int y, int w,
		const struct plat_text *label, unsigned flags) {
	const bool pressed = flags & PL_BUTTON_PRESSED;
	if (flags & PL_BUTTON_DEFAULT) {
		paint_nine(c, x - PL_BUTTON_RING, y - PL_BUTTON_RING, w + 2 * PL_BUTTON_RING,
			pressed ? &button_default_pressed : &button_default);
	} else {
		paint_nine(c, x, y, w, pressed ? &button_standard_pressed : &button_standard);
	}
	if (label && label->ink_l >= 0) {
		int ink_w = label->ink_r - label->ink_l + 1;
		uint32_t color = pressed ? C_WHITE : (flags & PL_BUTTON_DISABLED) ? GRAY(0x8) : C_BLACK;
		/* Baseline 13 px below the top ("OK", HIG figure 3-2). */
		pl_text(c, label, x + (w - ink_w) / 2, y + 13, color);
	}
}

/* ---- group box (figure 2-38) ------------------------------------------- */

void pl_group_box_paint(struct pl_canvas *c, int x0, int y0, int x1, int y1,
		const struct plat_text *title, uint32_t bg) {
	/* White first, one pixel down and right; the #888 line over it. */
	pl_outline(c, x0 + 1, y0 + 1, x1 + 1, y1 + 1, C_WHITE);
	pl_outline(c, x0, y0, x1, y1, GRAY(0x8));
	if (!title || title->ink_l < 0) {
		return;
	}
	const int ink_x = x0 + PL_GROUP_TITLE_X;
	const int ink_w = title->ink_r - title->ink_l + 1;
	/* Clear both lines behind the title, 4 px either side of its ink. */
	for (int x = ink_x - 4; x <= ink_x + ink_w + 3; x++) {
		pl_put(c, x, y0, bg);
		pl_put(c, x, y0 + 1, bg);
	}
	pl_text(c, title, ink_x, y0, C_BLACK);
}

/* ---- list box (figure 2-25) --------------------------------------------- */

int pl_list_visible_rows(int y0, int y1) {
	return (y1 - y0 - 1) / PL_LIST_ROW_H;
}

struct pl_scrollbar pl_list_scrollbar(int y0, int y1, const struct pl_list *list) {
	const int visible = pl_list_visible_rows(y0, y1);
	struct pl_scrollbar sb = { .vertical = true, .length = y1 - y0 + 1,
		.enabled = list->n > visible };
	if (sb.enabled) {
		sb.thumb = list->top * sb_thumb_range(sb.length) / (list->n - visible);
	}
	return sb;
}

void pl_list_paint(struct pl_canvas *c, int x0, int y0, int x1, int y1,
		const struct pl_list *list, struct pl_accent accent, uint32_t highlight) {
	const int sx = x1 - SB_WIDTH + 1; /* the scroll bar's left line */
	if (list->focused) {
		/* A 2 px ring outside the frame, its outer corners clipped. */
		for (int k = 1; k <= 2; k++) {
			pl_outline(c, x0 - k, y0 - k, x1 + k, y1 + k, accent.dark);
		}
		pl_put(c, x0 - 2, y0 - 2, GRAY(0xD));
		pl_put(c, x1 + 2, y0 - 2, GRAY(0xD));
		pl_put(c, x0 - 2, y1 + 2, GRAY(0xD));
		pl_put(c, x1 + 2, y1 + 2, GRAY(0xD));
	}
	pl_outline(c, x0, y0, x1, y1, C_BLACK);
	pl_fill(c, x0 + 1, y0 + 1, sx - 1, y1 - 1, C_WHITE);
	const int visible = pl_list_visible_rows(y0, y1);
	for (int r = 0; r < visible && list->top + r < list->n; r++) {
		const int i = list->top + r, top = y0 + 1 + r * PL_LIST_ROW_H;
		if (i == list->selected) {
			pl_fill(c, x0 + 1, top, sx - 1, top + PL_LIST_ROW_H - 1, highlight);
		}
		pl_text(c, list->rows[i], x0 + 3, top + 11, C_BLACK);
	}
	struct pl_scrollbar sb = pl_list_scrollbar(y0, y1, list);
	pl_scrollbar_paint(c, sx, y0, &sb, accent);
}

int pl_list_row_at(int x0, int y0, int x1, int y1, const struct pl_list *list, int x, int y) {
	if (x <= x0 || x >= x1 - SB_WIDTH + 1 || y <= y0 || y >= y1) {
		return -1;
	}
	const int i = list->top + (y - y0 - 1) / PL_LIST_ROW_H;
	return i < list->n ? i : -1;
}

/* ---- tab control (figures 2-30, 2-33) ----------------------------------- */

/*
 * Each tab's slanted sides, row by row from its top line (row 0) to the
 * row above the pane (row 20), as measured in figure 2-30. Left edges are
 * offsets from the tab's bottom-left corner, right edges from its
 * bottom-right; the strings run left to right in greys: '1' #222, 'K'
 * black, 'A'..'E' #AAA..#EEE, '3' '4' '8' #333 #444 #888, 'W' white. The
 * edges step out a pixel every three rows; step rows are softened. Tabs
 * behind the front one are shaded a little differently.
 */
struct tab_edge {
	int dx;
	const char *px;
};
static const struct tab_edge front_left[PL_TAB_H] = {
	{ 10, "1" }, { 8, "1KB" }, { 7, "1BC" }, { 6, "1BCW" }, { 6, "KCW" },
	{ 5, "1BW" }, { 5, "KCW" }, { 5, "KCW" }, { 4, "1BW" }, { 4, "KCW" },
	{ 4, "KCW" }, { 3, "1BW" }, { 3, "KCW" }, { 3, "KCW" }, { 2, "1BW" },
	{ 2, "KCW" }, { 2, "KCW" }, { 1, "1BW" }, { 1, "KCW" }, { 1, "KCW" },
	{ 0, "1BW" },
};
static const struct tab_edge back_left[PL_TAB_H] = {
	{ 10, "1" }, { 8, "1KA" }, { 7, "1AC" }, { 6, "1ADW" }, { 6, "KCW" },
	{ 5, "1AW" }, { 5, "KCW" }, { 5, "KCW" }, { 4, "1AW" }, { 4, "KCW" },
	{ 4, "KCW" }, { 3, "1AW" }, { 3, "KCW" }, { 3, "KCW" }, { 2, "1AW" },
	{ 2, "KCW" }, { 2, "KCW" }, { 1, "1AW" }, { 1, "KCW" }, { 1, "KCW" },
	{ 0, "1AW" },
};
static const struct tab_edge front_right[PL_TAB_H] = {
	{ -10, "" }, { -10, "BKK" }, { -9, "D8K" }, { -8, "B43" }, { -8, "D8K" },
	{ -7, "B43" }, { -7, "D8K" }, { -7, "D8K" }, { -6, "B43" }, { -6, "D8K" },
	{ -6, "D8K" }, { -5, "B43" }, { -5, "C8K" }, { -5, "D8K" }, { -4, "B43" },
	{ -4, "C8K" }, { -4, "D8K" }, { -3, "B43" }, { -3, "D8K" }, { -3, "E8K" },
	{ -2, "B4K" },
};
static const struct tab_edge back_right[PL_TAB_H] = {
	{ -10, "" }, { -10, "AKK" }, { -9, "D8K" }, { -8, "B43" }, { -8, "C8K" },
	{ -7, "B43" }, { -7, "C8K" }, { -7, "CAK" }, { -6, "B43" }, { -6, "C8K" },
	{ -6, "CAK" }, { -5, "B43" }, { -5, "C8K" }, { -5, "CAK" }, { -4, "B43" },
	{ -4, "C8K" }, { -4, "CAK" }, { -3, "B43" }, { -3, "C8K" }, { -3, "CAK" },
	{ -2, "B4K" },
};
#define TAB_INSET 4     /* first tab's bottom-left from the pane's left */
#define TAB_LABEL_PAD 7 /* top line's start to the label ink */
#define TAB_LABEL_PAD_R 8 /* label ink to the top line's end */
#define TAB_SPREAD 11   /* bottom-left to the top line's start */
#define TAB_PITCH 21    /* top line end to the next tab's top line start */
#define TAB_BASELINE 15

/* The top line of tab i spans tx0..tx1; returns the bottom-left x. */
static int tab_geometry(int x0, const struct plat_text *const *labels, int i,
		int *tx0, int *tx1) {
	int left = x0 + TAB_INSET + TAB_SPREAD;
	for (int k = 0; k <= i; k++) {
		const struct plat_text *t = labels[k];
		const int ink = t && t->ink_l >= 0 ? t->ink_r - t->ink_l + 1 : 0;
		*tx0 = left;
		*tx1 = left + TAB_LABEL_PAD + ink + TAB_LABEL_PAD_R - 1;
		left = *tx1 + TAB_PITCH;
	}
	return *tx0 - TAB_SPREAD;
}

static uint32_t tab_color(char ch) {
	switch (ch) {
	case '1': return GRAY(0x2);
	case 'K': return C_BLACK;
	case 'A': return GRAY(0xA);
	case 'B': return GRAY(0xB);
	case 'C': return GRAY(0xC);
	case 'D': return GRAY(0xD);
	case 'E': return GRAY(0xE);
	case '8': return GRAY(0x8);
	case '4': return GRAY(0x4);
	case '3': return GRAY(0x3);
	default: return C_WHITE;
	}
}

static void paint_tab(struct pl_canvas *c, int x0, int y0,
		const struct plat_text *const *labels, int i, bool front, bool last) {
	int tx0, tx1;
	const int bl = tab_geometry(x0, labels, i, &tx0, &tx1);
	const int br = tx1 + 10;
	const uint32_t fill = front ? GRAY(0xE) : GRAY(0xC);
	const struct tab_edge *left = front ? front_left : back_left;
	const struct tab_edge *right = front ? front_right : back_right;
	for (int r = 0; r < PL_TAB_H; r++) {
		const int y = y0 + r;
		const int lx = bl + left[r].dx, rx = br + right[r].dx;
		const int l_end = lx + (int)strlen(left[r].px);
		/* Interior: the #CCC row under the top line, then a white row. */
		const uint32_t inside = r == 1 ? GRAY(0xC) : r == 2 ? C_WHITE : fill;
		if (r == 0) {
			pl_hline(c, tx0, tx1, y, C_BLACK);
		} else {
			pl_hline(c, l_end, rx - 1, y, inside);
		}
		for (int k = 0; left[r].px[k]; k++) {
			pl_put(c, lx + k, y, tab_color(left[r].px[k]));
		}
		for (int k = 0; right[r].px[k]; k++) {
			pl_put(c, rx + k, y, tab_color(right[r].px[k]));
		}
		/* The rightmost tab behind catches a little more light at its
		 * top-right corner (figure 2-30). */
		if (!front && last && r >= 7 && r % 3 == 1) {
			pl_put(c, lx + 2, y, GRAY(0xE)); /* before each step */
		}
		if (!front && last && r == 1) {
			pl_put(c, rx - 1, y, GRAY(0xB));
		} else if (!front && last && r == 2) {
			pl_put(c, lx + 3, y, GRAY(0xE));
			pl_put(c, rx - 1, y, GRAY(0xE));
		}
	}
	pl_text(c, labels[i], tx0 + TAB_LABEL_PAD, y0 + TAB_BASELINE, C_BLACK);
}

void pl_tabs_paint(struct pl_canvas *c, int x0, int y0, int x1, int y1,
		const struct plat_text *const *labels, int n, int selected) {
	const int py = y0 + PL_TAB_H; /* the pane's top line */
	/* The raised pane: black, an inner #CCC line, a white highlight, and
	 * a #999 shade along the bottom and right. */
	pl_fill(c, x0 + 1, py + 1, x1 - 1, y1 - 1, GRAY(0xE));
	pl_outline(c, x0, py, x1, y1, C_BLACK);
	pl_hline(c, x0 + 1, x1 - 1, py + 1, GRAY(0xC));
	pl_vline(c, x0 + 1, py + 1, y1 - 1, GRAY(0xC));
	pl_hline(c, x0 + 2, x1 - 3, py + 2, C_WHITE);
	pl_vline(c, x0 + 2, py + 2, y1 - 3, C_WHITE);
	pl_hline(c, x0 + 2, x1 - 1, y1 - 1, GRAY(0x9));
	pl_vline(c, x1 - 1, py + 2, y1 - 1, GRAY(0x9));
	pl_put(c, x1 - 1, py + 1, GRAY(0xD));
	pl_put(c, x0 + 1, y1 - 1, GRAY(0xE));

	for (int i = n - 1; i >= 0; i--) {
		if (i != selected) {
			paint_tab(c, x0, y0, labels, i, false, i == n - 1);
		}
	}
	if (selected < 0 || selected >= n) {
		return;
	}
	paint_tab(c, x0, y0, labels, selected, true, selected == n - 1);
	/* The front tab opens into the pane: its sides continue through the
	 * pane's top line and the pane's inner lines stop beneath it. */
	int tx0, tx1;
	const int bl = tab_geometry(x0, labels, selected, &tx0, &tx1), br = tx1 + 10;
	pl_put(c, bl, py, C_BLACK);
	pl_put(c, bl + 1, py, GRAY(0xC));
	pl_put(c, bl + 2, py, C_WHITE);
	pl_hline(c, bl + 3, br - 2, py, GRAY(0xE));
	pl_put(c, br - 1, py, GRAY(0x8));
	pl_put(c, br, py, C_BLACK);
	pl_hline(c, bl + 2, br - 2, py + 1, GRAY(0xE));
	pl_hline(c, bl + 1, br - 1, py + 2, GRAY(0xE));
}

int pl_tabs_hit(int x0, int y0, const struct plat_text *const *labels, int n, int x, int y) {
	if (y < y0 || y >= y0 + PL_TAB_H) {
		return -1;
	}
	for (int i = 0; i < n; i++) {
		int tx0, tx1;
		const int bl = tab_geometry(x0, labels, i, &tx0, &tx1);
		const int r = y - y0;
		if (x >= bl + front_left[r].dx && x <= tx1 + 10 + front_right[r].dx + 2) {
			return i;
		}
	}
	return -1;
}

/*
 * Progress bar, measured in HIG figure 2-45 (whose colours are slightly
 * shifted; each maps to the nearest Platinum value). Rows are relative to
 * the top black line (row 0); the inside is rows 1..10.
 */
void pl_progress_paint(struct pl_canvas *c, int x, int y, int w, double fraction,
		struct pl_accent ac) {
	const int x1 = x + w - 1, y1 = y + PL_PROGRESS_H - 1;
	const int inner = w - 2;
	int fill = (int)(fraction * inner + 0.5);
	if (fill < 0) {
		fill = 0;
	}
	if (fill > inner) {
		fill = inner;
	}

	/* Bevel outside the black line: shade above and left, white below and right. */
	pl_hline(c, x - 1, x1, y - 1, GRAY(0xB));
	pl_vline(c, x - 1, y - 1, y1, GRAY(0xB));
	pl_hline(c, x, x1 + 1, y1 + 1, C_WHITE);
	pl_vline(c, x1 + 1, y, y1 + 1, C_WHITE);
	pl_outline(c, x, y, x1, y1, C_BLACK);

	/* The filled part: a lit cylinder, dark at the edges, a near-white
	 * line along the middle; its first columns catch the light and its
	 * last fall into shadow. */
	const uint32_t profile[10] = {
		ac.shadow, ac.dark, ac.body, ac.light, ac.grip_hi,
		ac.light, ac.body, ac.dark, ac.shadow, ac.deep,
	};
	const uint32_t first[10] = {
		ac.dark, ac.dark, ac.dark, ac.dark, ac.dark,
		ac.dark, ac.dark, ac.dark, ac.dark, ac.shadow,
	};
	const uint32_t second[10] = {
		ac.dark, ac.body, ac.light, ac.grip_hi, ac.grip_hi,
		ac.grip_hi, ac.light, ac.body, ac.dark, ac.shadow,
	};
	for (int i = 0; i < fill; i++) {
		const int cx = x + 1 + i;
		for (int r = 0; r < 10; r++) {
			uint32_t v = profile[r];
			if (i == 0) {
				v = first[r];
			} else if (i == 1) {
				v = second[r];
			} else if (fill < inner && i == fill - 1) {
				v = r == 0 ? ac.shadow : ac.deep;
			} else if (fill < inner && i == fill - 2) {
				v = r == 1 ? ac.dark : r == 9 ? ac.deep : ac.shadow;
			}
			pl_put(c, cx, y + 1 + r, v);
		}
	}
	if (fill >= inner) {
		return;
	}
	/* A black line ends the fill (unless empty), then the recessed well:
	 * #666 and #999 along the left, #999 on top, #CCC inside, #DDD along
	 * the bottom and right. */
	int wx = x + 1 + fill;
	if (fill > 0) {
		pl_vline(c, wx, y + 1, y1 - 1, C_BLACK);
		wx++;
	}
	if (wx > x1 - 1) {
		return;
	}
	pl_fill(c, wx, y + 1, x1 - 1, y1 - 1, GRAY(0xC));
	pl_hline(c, wx, x1 - 1, y + 1, GRAY(0x9));
	pl_hline(c, wx, x1 - 1, y1 - 1, GRAY(0xD));
	pl_vline(c, x1 - 1, y + 2, y1 - 1, GRAY(0xD));
	pl_vline(c, wx, y + 1, y1 - 1, GRAY(0x6));
	if (wx + 1 <= x1 - 1) {
		pl_vline(c, wx + 1, y + 1, y1 - 1, GRAY(0x9));
	}
}

/*
 * Scroll bars are described horizontally, as measured in HIG figure 2-24:
 * `a` runs along the bar (0 = leading border), `b` across it (0 = top
 * border, 1..14 interior, 15 = bottom border). Vertical bars transpose.
 */

#define C_555 GRAY(0x5)
#define C_777 GRAY(0x7)
#define C_888 GRAY(0x8)
#define C_AAA GRAY(0xA)
#define C_BBB GRAY(0xB)
#define C_CCC GRAY(0xC)
#define C_DDD GRAY(0xD)
#define C_EEE GRAY(0xE)

struct sb_ctx {
	struct pl_canvas *c;
	int x, y;
	bool vertical;
};

static void sbp(struct sb_ctx *s, int a, int b, uint32_t color) {
	if (s->vertical) {
		pl_put(s->c, s->x + b, s->y + a, color);
	} else {
		pl_put(s->c, s->x + a, s->y + b, color);
	}
}

static void sb_run(struct sb_ctx *s, int a0, int a1, int b, uint32_t color) {
	for (int a = a0; a <= a1; a++) {
		sbp(s, a, b, color);
	}
}

static void sb_col(struct sb_ctx *s, int a, int b0, int b1, uint32_t color) {
	for (int b = b0; b <= b1; b++) {
		sbp(s, a, b, color);
	}
}

int sb_thumb_range(int length) {
	/* Thumb lines may sit on either arrow separator. */
	int r = (length - 1 - SB_ARROW - 1) - (SB_ARROW + 1) - (SB_THUMB + 1);
	return r > 0 ? r : 0;
}

/* Arrow glyph: a 4x8 triangle pointing to `dir` (-1 = towards a=0). */
static void sb_arrow_glyph(struct sb_ctx *s, int box_a, int dir, uint32_t color) {
	/* Rows across 4..11, columns 5..8 of the box: widths 1,2,3,4,4,3,2,1
	 * growing from the flat side (column 8 for left, 5 for right). */
	static const int widths[8] = { 1, 2, 3, 4, 4, 3, 2, 1 };
	for (int i = 0; i < 8; i++) {
		for (int k = 0; k < widths[i]; k++) {
			int a = dir < 0 ? box_a + 8 - k : box_a + 5 + k;
			sbp(s, a, 4 + i, color);
		}
	}
}

/* An arrow box occupying a = box_a .. box_a+13 (interior). */
static void sb_arrow_box(struct sb_ctx *s, int box_a, int dir, bool enabled) {
	const int last = SB_ARROW; /* interior b runs 1..14 */
	if (!enabled) {
		for (int b = 1; b <= last; b++) {
			sb_run(s, box_a, box_a + SB_ARROW - 1, b, C_EEE);
		}
		sb_arrow_glyph(s, box_a, dir, C_888);
		return;
	}
	for (int b = 1; b <= last; b++) {
		sb_run(s, box_a, box_a + SB_ARROW - 1, b, C_DDD);
	}
	sb_run(s, box_a, box_a + SB_ARROW - 2, 1, C_WHITE);
	sb_col(s, box_a, 1, last - 1, C_WHITE);
	sb_col(s, box_a + SB_ARROW - 1, 2, last, C_BBB);
	sb_run(s, box_a + 1, box_a + SB_ARROW - 1, last, C_BBB);
	sb_arrow_glyph(s, box_a, dir, C_BLACK);
}

/* Recessed track, with the shadow cast by whatever precedes `shade_a`. */
static void sb_track(struct sb_ctx *s, int a0, int a1) {
	for (int a = a0; a <= a1; a++) {
		sbp(s, a, 1, C_777);
		sbp(s, a, 2, C_888);
		sb_col(s, a, 3, 12, C_AAA);
		sbp(s, a, 13, C_BBB);
		sbp(s, a, 14, C_CCC);
	}
}

static void sb_track_shadow(struct sb_ctx *s, int a, int a_max) {
	if (a <= a_max) {
		sb_col(s, a, 1, 13, C_777);
	}
	if (a + 1 <= a_max) {
		sb_col(s, a + 1, 2, 12, C_888);
	}
}

static void sb_thumb(struct sb_ctx *s, int line_a, struct pl_accent ac) {
	int t0 = line_a + 1, t1 = line_a + SB_THUMB; /* interior along */
	sb_col(s, line_a, 1, SB_ARROW, C_BLACK);
	sb_col(s, line_a + SB_THUMB + 1, 1, SB_ARROW, C_BLACK);

	sbp(s, t0, 1, ac.corner);
	sb_run(s, t0 + 1, t1 - 1, 1, ac.light);
	sbp(s, t1, 1, ac.body);
	for (int b = 2; b <= SB_ARROW - 1; b++) {
		sbp(s, t0, b, ac.light);
		sb_run(s, t0 + 1, t1 - 1, b, ac.body);
		sbp(s, t1, b, ac.dark);
	}
	sbp(s, t0, SB_ARROW, ac.body);
	sb_run(s, t0 + 1, t1, SB_ARROW, ac.dark);

	/* Grip: four ridges at interior offsets 3..10, rows 4..11. */
	for (int r = 0; r < 4; r++) {
		int la = t0 + 3 + 2 * r;
		sbp(s, la, 4, ac.grip_hi);
		sb_col(s, la, 5, 10, ac.light);
		sb_col(s, la + 1, 5, 11, ac.shadow);
	}
}

void pl_scrollbar_paint(struct pl_canvas *c, int x, int y,
		const struct pl_scrollbar *sb, struct pl_accent accent) {
	struct sb_ctx s = { .c = c, .x = x, .y = y, .vertical = sb->vertical };
	const int L = sb->length;
	const int dec_sep = SB_ARROW + 1, inc_sep = L - 2 - SB_ARROW;
	const uint32_t line = sb->enabled ? C_BLACK : C_555;

	/* Border lines along both sides and at both ends. */
	sb_run(&s, 0, L - 1, 0, C_BLACK);
	sb_run(&s, 0, L - 1, SB_WIDTH - 1, C_BLACK);
	sb_col(&s, 0, 0, SB_WIDTH - 1, C_BLACK);
	sb_col(&s, L - 1, 0, SB_WIDTH - 1, C_BLACK);
	sb_col(&s, dec_sep, 1, SB_ARROW, line);
	sb_col(&s, inc_sep, 1, SB_ARROW, line);

	sb_arrow_box(&s, 1, -1, sb->enabled);
	sb_arrow_box(&s, inc_sep + 1, +1, sb->enabled);

	int t0 = dec_sep + 1, t1 = inc_sep - 1;
	if (!sb->enabled) {
		for (int b = 1; b <= SB_ARROW; b++) {
			sb_run(&s, t0, t1, b, C_EEE);
		}
		return;
	}
	sb_track(&s, t0, t1);
	int thumb = sb->thumb < 0 ? 0 : sb->thumb;
	int range = sb_thumb_range(L);
	if (thumb > range) {
		thumb = range;
	}
	int line_a = dec_sep + thumb;
	if (line_a > dec_sep) {
		sb_track_shadow(&s, t0, line_a - 1);
	}
	sb_thumb(&s, line_a, accent);
	sb_track_shadow(&s, line_a + SB_THUMB + 2, t1);
}

enum sb_part sb_hit(const struct pl_scrollbar *sb, int along) {
	const int L = sb->length;
	if (along <= 0 || along >= L - 1) {
		return SB_NONE;
	}
	if (along <= SB_ARROW) {
		return SB_DEC_ARROW;
	}
	if (along >= L - 1 - SB_ARROW) {
		return SB_INC_ARROW;
	}
	if (!sb->enabled) {
		return SB_NONE;
	}
	int line_a = SB_ARROW + 1 + sb->thumb;
	if (along < line_a) {
		return SB_DEC_PAGE;
	}
	if (along > line_a + SB_THUMB + 1) {
		return SB_INC_PAGE;
	}
	return SB_THUMB_PART;
}
