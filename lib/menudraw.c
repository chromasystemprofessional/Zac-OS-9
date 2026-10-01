#include "menudraw.h"

#define C_222 GRAY(0x2)
#define C_888 GRAY(0x8)
#define C_999 GRAY(0x9)
#define C_DDD GRAY(0xD)

/* Rounded top-left screen corner, HIG figure 4-1. */
static const char *const corner_tl[9] = {
	"000005adf",
	"0005afffd",
	"005dfdddd",
	"05dfddddd",
	"0afdddddd",
	"5fddddddd",
	"afddddddd",
	"dfddddddd",
	"fdddddddd",
};

/* ⌘ (place of interest sign), 9x9, from the system font in figure 4-1. */
static const char *const cmd_glyph[9] = {
	".##...##.",
	"#..#.#..#",
	"#..#.#..#",
	".#######.",
	"...#.#...",
	".#######.",
	"#..#.#..#",
	"#..#.#..#",
	".##...##.",
};

static int ink_width(const struct plat_text *t) {
	return t && t->ink_l >= 0 ? t->ink_r - t->ink_l + 1 : 0;
}

/* ---- menu bar ---------------------------------------------------------- */

static void set_extent(struct mbar_title *t, int ink_l) {
	int w = t->icon ? MBAR_ICON_SIZE : ink_width(t->text);
	t->ink_l = ink_l;
	t->ink_r = ink_l + (w > 0 ? w : 1) - 1;
	t->hi_l = t->ink_l - MBAR_HILITE_PAD;
	t->hi_r = t->ink_r + MBAR_HILITE_PAD;
}

void mbar_layout_left(struct mbar_title *titles, int n) {
	int x = MBAR_FIRST_TEXT_X;
	for (int i = 0; i < n; i++) {
		if (i == 0 && titles[i].icon) {
			/* The icon title sits in its own slot; text titles start at
			 * x=43 regardless (HIG figure 4-1). */
			set_extent(&titles[i], MBAR_ICON_X);
			continue;
		}
		set_extent(&titles[i], x);
		x = titles[i].ink_r + 1 + MBAR_TITLE_GAP;
	}
}

void mbar_layout_right(struct mbar_title *titles, int n, int screen_w) {
	/* Mirror of the left icon slot (estimate; not in the HIG). */
	int right = screen_w - MBAR_ICON_X - 1;
	for (int i = n - 1; i >= 0; i--) {
		int w = titles[i].icon ? MBAR_ICON_SIZE : ink_width(titles[i].text);
		set_extent(&titles[i], right - (w > 0 ? w : 1) + 1);
		right = titles[i].ink_l - 1 - MBAR_TITLE_GAP;
	}
}

void mbar_paint_background(struct pl_canvas *c, int screen_w) {
	const int W = screen_w;
	pl_hline(c, 0, W - 1, 0, C_WHITE);
	pl_fill(c, 0, 1, W - 1, MBAR_HEIGHT - 3, C_DDD);
	pl_hline(c, 0, W - 1, MBAR_HEIGHT - 2, C_999);
	pl_hline(c, 0, W - 1, MBAR_HEIGHT - 1, C_BLACK);
	pl_vline(c, 0, 1, MBAR_HEIGHT - 3, C_WHITE);
	pl_put(c, 0, MBAR_HEIGHT - 2, C_DDD);

	pl_grays(c, 0, 0, corner_tl, 9);
	/* Top-right: mirror image without the left-hand highlight column.
	 * TODO: not shown in the HIG; measure from a real screen. */
	for (int j = 0; j < 9; j++) {
		for (int i = 0; i < 9; i++) {
			char ch = corner_tl[j][i];
			if (ch == 'f' && j > 0) {
				ch = 'd';
			}
			int v = ch <= '9' ? ch - '0' : ch - 'a' + 10;
			pl_put(c, W - 1 - i, j, GRAY(v));
		}
	}
}

void mbar_paint_titles(struct pl_canvas *c, const struct mbar_title *titles,
		int n, int highlighted, struct pl_accent accent) {
	for (int i = 0; i < n; i++) {
		const struct mbar_title *t = &titles[i];
		bool hi = i == highlighted;
		if (hi) {
			pl_hline(c, t->hi_l, t->hi_r, 0, accent.light);
			pl_fill(c, t->hi_l, 1, t->hi_r, MBAR_HEIGHT - 3, accent.base);
			pl_hline(c, t->hi_l, t->hi_r, MBAR_HEIGHT - 2, accent.dark);
		}
		if (t->icon) {
			pl_image(c, t->ink_l, MBAR_ICON_Y, t->icon, MBAR_ICON_SIZE, MBAR_ICON_SIZE);
		} else {
			uint32_t color = hi ? C_WHITE : t->dimmed ? C_888 : C_BLACK;
			pl_text(c, t->text, t->ink_l, MBAR_BASELINE, color);
		}
	}
}

int mbar_title_at(const struct mbar_title *titles, int n, int x) {
	for (int i = 0; i < n; i++) {
		if (x >= titles[i].hi_l && x <= titles[i].hi_r) {
			return i;
		}
	}
	return -1;
}

/* ---- pull-down menus --------------------------------------------------- */

static int row_height(const struct menu_item *item) {
	return item->label ? MENU_ITEM_H : MENU_SEP_H;
}

void menu_measure(const struct menu_item *items, int n, int *width, int *height) {
	int w = 0, h = 2;
	for (int i = 0; i < n; i++) {
		h += row_height(&items[i]);
		if (!items[i].label) {
			continue;
		}
		int need = MENU_TEXT_X + ink_width(items[i].label) + MENU_RIGHT_PAD;
		if (items[i].key) {
			need = MENU_TEXT_X + ink_width(items[i].label) + MENU_SHORTCUT_GAP +
				MENU_CMD_FROM_RIGHT;
		}
		if (need > w) {
			w = need;
		}
	}
	*width = w;
	*height = h;
}

static void paint_cmd(struct pl_canvas *c, int x, int y, uint32_t color) {
	for (int j = 0; j < 9; j++) {
		for (int i = 0; i < 9; i++) {
			if (cmd_glyph[j][i] == '#') {
				pl_put(c, x + i, y + j, color);
			}
		}
	}
}

void menu_paint(struct pl_canvas *c, const struct menu_item *items, int n,
		int width, int height, int selected, struct pl_accent accent) {
	const int W = width, H = height;

	pl_fill(c, 1, 1, W - 2, H - 2, C_DDD);
	pl_outline(c, 0, 0, W - 1, H - 1, C_BLACK);
	pl_hline(c, 1, W - 3, 1, C_WHITE);
	pl_vline(c, 1, 1, H - 3, C_WHITE);
	pl_vline(c, W - 2, 2, H - 2, C_999);
	pl_hline(c, 2, W - 2, H - 2, C_999);
	pl_vline(c, W, 2, H, C_222);
	pl_hline(c, 2, W, H, C_222);

	int y = 1;
	for (int i = 0; i < n; i++) {
		const struct menu_item *it = &items[i];
		if (!it->label) {
			pl_hline(c, 1, W - 2, y + 2, C_888);
			pl_hline(c, 1, W - 2, y + 3, C_WHITE);
			y += MENU_SEP_H;
			continue;
		}
		/* TODO: the selected-item look isn't in the HIG figures; we assume
		 * the title's accent fill with white text. */
		bool sel = i == selected && it->enabled;
		if (sel) {
			pl_fill(c, 1, y, W - 2, y + MENU_ITEM_H - 1, accent.base);
		}
		uint32_t color = sel ? C_WHITE : it->enabled ? C_BLACK : C_888;
		pl_text(c, it->label, MENU_TEXT_X, y + MENU_ITEM_BASELINE, color);
		if (it->key) {
			int cx = W - MENU_CMD_FROM_RIGHT;
			paint_cmd(c, cx, y + MENU_ITEM_BASELINE - 8, color);
			pl_text(c, it->key, cx + MENU_KEY_AFTER_CMD, y + MENU_ITEM_BASELINE, color);
		}
		y += MENU_ITEM_H;
	}
}

int menu_item_at(const struct menu_item *items, int n, int height, int y) {
	if (y < 1 || y > height - 2) {
		return -1;
	}
	int top = 1;
	for (int i = 0; i < n; i++) {
		int h = row_height(&items[i]);
		if (y >= top && y < top + h) {
			return items[i].label ? i : -1;
		}
		top += h;
	}
	return -1;
}
