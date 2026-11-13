#include "menudraw.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "chrome.h"
#include "theme.h"

#define C_222 GRAY(0x2)
#define C_888 GRAY(0x8)
#define C_999 GRAY(0x9)
#define C_DDD GRAY(0xD)

/* A menu skin named by the theme, else the conventional bundle file.
 * "off" means the flat palette. */
static bool menu_skin(const char *key, const char *path, char *out, size_t size) {
	if (zac_theme_string(key, out, size)) {
		if (strcmp(out, "off") == 0) {
			return false;
		}
		return true;
	}
	snprintf(out, size, "%s", path);
	return true;
}

static void menu_fill_skin(struct pl_canvas *c, int x0, int y0, int x1, int y1,
		const char *key, const char *path, uint32_t fallback) {
	char rel[256];
	if (menu_skin(key, path, rel, sizeof(rel))) {
		chrome_fill(c, x0, y0, x1, y1, rel, fallback);
		return;
	}
	pl_fill(c, x0, y0, x1, y1, fallback);
}

static const uint32_t *menu_skin_pixels(const char *key, const char *path,
		int *w, int *h) {
	char rel[256];
	if (!menu_skin(key, path, rel, sizeof(rel))) {
		return NULL;
	}
	return chrome_file(rel, w, h);
}

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

/* ⇧ (shift), 9x9, outlined in the ⌘'s weight; not measured. */
static const char *const shift_glyph[9] = {
	"....#....",
	"...#.#...",
	"..#...#..",
	".#.....#.",
	"###...###",
	"..#...#..",
	"..#...#..",
	"..#...#..",
	"..#####..",
};

/* Checkmark, 9x8, HIG figure 4-3: x=4 in the menu, bottom row on the
 * item's baseline. */
static const char *const check_glyph[8] = {
	"........#",
	".......##",
	"......##.",
	".....##..",
	"#...##...",
	"##.##....",
	".###.....",
	"..#......",
};

static int ink_width(const struct plat_text *t) {
	return t && t->ink_l >= 0 ? t->ink_r - t->ink_l + 1 : 0;
}

/* ---- menu bar ---------------------------------------------------------- */

static void set_extent(struct mbar_title *t, int ink_l) {
	int w = t->icon ? MBAR_ICON_W : ink_width(t->text);
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
		int w = titles[i].icon ? MBAR_ICON_W : ink_width(titles[i].text);
		set_extent(&titles[i], right - (w > 0 ? w : 1) + 1);
		right = titles[i].ink_l - 1 - MBAR_TITLE_GAP;
		if (titles[i].handle) {
			titles[i].handle_l = titles[i].hi_l - 1 - MBAR_HANDLE_W;
			right = titles[i].handle_l - 1 - MBAR_TITLE_GAP;
		}
	}
}

void mbar_paint_background(struct pl_canvas *c, int screen_w) {
	mbar_paint_background_themed(c, screen_w, NULL);
}

void mbar_paint_background_themed(struct pl_canvas *c, int screen_w,
		const struct pl_menu_palette *palette) {
	const int W = screen_w;
	int mw, mh;
	const uint32_t *bar_art = menu_skin_pixels("menu-bar-image",
		"chrome/menu-background.png", &mw, &mh);
	if (bar_art) {
		pl_fill(c, 0, 0, W - 1, MBAR_HEIGHT - 1, palette ? palette->bar_face : C_DDD);
		chrome_tile(c, bar_art, mw, mh, 0, 0, W - 1, MBAR_HEIGHT - 1);
		pl_hline(c, 0, W - 1, MBAR_HEIGHT - 1,
			palette ? palette->bar_border : C_BLACK);
		return;
	}
	const uint32_t face = palette ? palette->bar_face : C_DDD;
	const uint32_t light = palette ? palette->bar_light : C_WHITE;
	const uint32_t shadow = palette ? palette->bar_shadow : C_999;
	const uint32_t border = palette ? palette->bar_border : C_BLACK;
	if (palette && palette->flat_bar) {
		/* A themed bar: flat colour and its two edge lines. The Platinum
		 * corner ornaments belong to the Platinum face only. */
		pl_fill(c, 0, 0, W - 1, MBAR_HEIGHT - 1, face);
		pl_hline(c, 0, W - 1, 0, light);
		pl_hline(c, 0, W - 1, MBAR_HEIGHT - 2, shadow);
		pl_hline(c, 0, W - 1, MBAR_HEIGHT - 1, border);
		pl_vline(c, 0, 1, MBAR_HEIGHT - 2, light);
		return;
	}
	pl_hline(c, 0, W - 1, 0, light);
	pl_fill(c, 0, 1, W - 1, MBAR_HEIGHT - 3, face);
	pl_hline(c, 0, W - 1, MBAR_HEIGHT - 2, shadow);
	pl_hline(c, 0, W - 1, MBAR_HEIGHT - 1, border);
	pl_vline(c, 0, 1, MBAR_HEIGHT - 3, light);
	pl_put(c, 0, MBAR_HEIGHT - 2, face);

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
	mbar_paint_titles_themed(c, titles, n, highlighted, accent, NULL);
}

void mbar_paint_titles_themed(struct pl_canvas *c, const struct mbar_title *titles,
		int n, int highlighted, struct pl_accent accent,
		const struct pl_menu_palette *palette) {
	const uint32_t text = palette ? palette->bar_text : C_BLACK;
	const uint32_t dimmed = palette ? palette->bar_text_dimmed : C_888;
	const uint32_t selected = palette ? palette->bar_text_selected : C_WHITE;
	const uint32_t ridge_light = palette ? palette->bar_light : C_WHITE;
	const uint32_t ridge_shadow = palette ? palette->bar_text_dimmed : C_888;
	for (int i = 0; i < n; i++) {
		const struct mbar_title *t = &titles[i];
		bool hi = i == highlighted;
		if (hi) {
			pl_hline(c, t->hi_l, t->hi_r, 0, accent.dark);
			pl_fill(c, t->hi_l, 1, t->hi_r, MBAR_HEIGHT - 3, accent.shadow);
			pl_hline(c, t->hi_l, t->hi_r, MBAR_HEIGHT - 2, accent.deep);
		}
		if (t->handle) {
			/* Ridges: a highlight and a shadow each. */
			for (int r = 0; r < 3; r++) {
				pl_vline(c, t->handle_l + 2 * r, 4, MBAR_HEIGHT - 6, ridge_light);
				pl_vline(c, t->handle_l + 2 * r + 1, 4, MBAR_HEIGHT - 6, ridge_shadow);
			}
		}
		if (t->icon) {
			/* Blended: the logo's edge is soft. */
			pl_image_blend(c, t->ink_l, MBAR_ICON_Y, t->icon, MBAR_ICON_W, MBAR_ICON_H, false);
		} else {
			pl_text(c, t->text, t->ink_l, MBAR_BASELINE,
				hi ? selected : t->dimmed ? dimmed : text);
		}
	}
}

int mbar_handle_at(const struct mbar_title *titles, int n, int x) {
	for (int i = 0; i < n; i++) {
		if (titles[i].handle && x >= titles[i].handle_l - 2 &&
				x <= titles[i].handle_l + MBAR_HANDLE_W + 1) {
			return i;
		}
	}
	return -1;
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
		int text_r = MENU_TEXT_X + ink_width(items[i].label) +
			(items[i].swatch ? MENU_SWATCH_ADVANCE : 0) + items[i].indent;
		int need = text_r + MENU_RIGHT_PAD;
		if (items[i].submenu) {
			need = text_r + MENU_SHORTCUT_GAP + MENU_ARROW_FROM_RIGHT;
		}
		if (items[i].key) {
			need = text_r + MENU_SHORTCUT_GAP + MENU_CMD_FROM_RIGHT +
				(items[i].shift ? MENU_SHIFT_BEFORE_CMD : 0);
		}
		if (need > w) {
			w = need;
		}
	}
	*width = w;
	*height = h;
}

static void paint_glyph(struct pl_canvas *c, const char *const *rows, int nrows,
		int x, int y, uint32_t color) {
	for (int j = 0; j < nrows; j++) {
		for (int i = 0; rows[j][i]; i++) {
			if (rows[j][i] == '#') {
				pl_put(c, x + i, y + j, color);
			}
		}
	}
}

void menu_paint(struct pl_canvas *c, const struct menu_item *items, int n,
		int width, int height, int selected, struct pl_accent accent) {
	menu_paint_themed(c, items, n, width, height, selected, accent, NULL);
}

void menu_paint_themed(struct pl_canvas *c, const struct menu_item *items, int n,
		int width, int height, int selected, struct pl_accent accent,
		const struct pl_menu_palette *palette) {
	const int W = width, H = height;
	const uint32_t face = palette ? palette->face : C_DDD;
	const uint32_t text = palette ? palette->text : C_BLACK;
	const uint32_t dimmed = palette ? palette->text_dimmed : C_888;
	const uint32_t selected_text = palette ? palette->text_selected : C_WHITE;
	const uint32_t border = palette ? palette->border : C_BLACK;
	const uint32_t light = palette ? palette->light : C_WHITE;
	const uint32_t shadow = palette ? palette->shadow : C_999;
	const uint32_t separator = palette ? palette->separator : C_888;
	const uint32_t separator_light = palette ? palette->separator_light : C_WHITE;
	const uint32_t outer_shadow = palette ? palette->shadow : C_222;

	menu_fill_skin(c, 1, 1, W - 2, H - 2, "menu-background-image",
		"chrome/menu-background.png", face);
	pl_outline(c, 0, 0, W - 1, H - 1, border);
	pl_hline(c, 1, W - 3, 1, light);
	pl_vline(c, 1, 1, H - 3, light);
	pl_vline(c, W - 2, 2, H - 2, shadow);
	pl_hline(c, 2, W - 2, H - 2, shadow);
	pl_vline(c, W, 2, H, outer_shadow);
	pl_hline(c, 2, W, H, outer_shadow);

	int y = 1;
	for (int i = 0; i < n; i++) {
		const struct menu_item *it = &items[i];
		if (!it->label) {
			pl_hline(c, 1, W - 2, y + 2, separator);
			pl_hline(c, 1, W - 2, y + 3, separator_light);
			y += MENU_SEP_H;
			continue;
		}
		/* The selected item (HIG figure 2-7): an accent bar lit along its
		 * top and left, shaded down its right side; white text. */
		bool sel = i == selected && it->enabled;
		if (sel) {
			int hw, hh;
			const uint32_t *hp = menu_skin_pixels("menu-highlight-image",
				"chrome/menu-highlight.png", &hw, &hh);
			if (hp) {
				chrome_tile(c, hp, hw, hh, 1, y, W - 2, y + MENU_ITEM_H - 1);
			} else {
				pl_fill(c, 1, y, W - 2, y + MENU_ITEM_H - 1, accent.shadow);
				pl_hline(c, 1, W - 3, y, accent.dark);
				pl_vline(c, 1, y, y + MENU_ITEM_H - 1, accent.dark);
				pl_vline(c, W - 2, y + 1, y + MENU_ITEM_H - 1, accent.deep);
			}
		}
		uint32_t color = sel ? selected_text : it->enabled ? text : dimmed;
		int tx = MENU_TEXT_X + it->indent;
		if (it->swatch) {
			int sy = y + (MENU_ITEM_H - MENU_SWATCH) / 2;
			pl_fill(c, tx, sy, tx + MENU_SWATCH - 1, sy + MENU_SWATCH - 1, it->swatch);
			pl_outline(c, tx, sy, tx + MENU_SWATCH - 1, sy + MENU_SWATCH - 1, color);
			tx += MENU_SWATCH_ADVANCE;
		}
		pl_text(c, it->label, tx, y + MENU_ITEM_BASELINE, color);
		if (it->checked) {
			paint_glyph(c, check_glyph, 8, MENU_CHECK_X, y + MENU_ITEM_BASELINE - 7, color);
		}
		if (it->submenu) {
			/* The hierarchical-menu arrow: widths 1..6..1 (figure 4-3). */
			for (int j = 0; j < 11; j++) {
				int w = j < 6 ? j + 1 : 11 - j;
				pl_hline(c, W - MENU_ARROW_FROM_RIGHT, W - MENU_ARROW_FROM_RIGHT + w - 1,
					y + 2 + j, color);
			}
		}
		if (it->key) {
			int cx = W - MENU_CMD_FROM_RIGHT;
			paint_glyph(c, cmd_glyph, 9, cx, y + MENU_ITEM_BASELINE - 8, color);
			if (it->shift) {
				paint_glyph(c, shift_glyph, 9, cx - MENU_SHIFT_BEFORE_CMD, y + MENU_ITEM_BASELINE - 8, color);
			}
			pl_text(c, it->key, cx + MENU_KEY_AFTER_CMD, y + MENU_ITEM_BASELINE, color);
		}
		y += MENU_ITEM_H;
	}
}

int menu_item_top(const struct menu_item *items, int n, int i) {
	int top = 1;
	for (int k = 0; k < i && k < n; k++) {
		top += row_height(&items[k]);
	}
	return top;
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
