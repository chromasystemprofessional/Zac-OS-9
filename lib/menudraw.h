#ifndef PLATINUM_MENUDRAW_H
#define PLATINUM_MENUDRAW_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Platinum menu bar and pull-down menus, drawn from
 * docs/reference/platinum-menus.md. Pure drawing + layout; no I/O.
 */

#include "draw.h"

/* Menu bar (HIG figure 4-1). */
#define MBAR_HEIGHT 20
#define MBAR_BASELINE 13
#define MBAR_TITLE_GAP 15      /* clear pixels between adjacent titles' ink */
#define MBAR_HILITE_PAD 10     /* highlight extends this far past the ink */
#define MBAR_FIRST_TEXT_X 43   /* ink of the first text title */
#define MBAR_ICON_X 15         /* left-hand icon title cell (estimate) */
#define MBAR_ICON_Y 2
#define MBAR_ICON_SIZE 16

/* Pull-down menus (HIG figures 4-1, 4-2). */
#define MENU_ITEM_H 16
#define MENU_SEP_H 6
#define MENU_ITEM_BASELINE 11
#define MENU_TEXT_X 18
#define MENU_CHECK_X 4         /* checkmark ink, HIG figure 4-3 */
#define MENU_RIGHT_PAD 12      /* clear pixels between widest ink and border */
#define MENU_CMD_FROM_RIGHT 28 /* the ⌘ glyph's ink starts at W - 28 */
#define MENU_KEY_AFTER_CMD 11  /* key character ink starts 11 px after ⌘ */
#define MENU_SHORTCUT_GAP 12   /* text ink to ⌘ (not in the HIG; assumed) */
#define MENU_SHADOW 1          /* extra column and row painted for the shadow */
#define MENU_ARROW_FROM_RIGHT 19 /* submenu arrow ink starts at W - 19 (fig 4-3) */
/* Label-menu color swatch: not in the HIG figures; estimated. */
#define MENU_SWATCH 10         /* square, outlined in the text color */
#define MENU_SWATCH_ADVANCE 15 /* text moves right by this much */

struct mbar_title {
	const struct plat_text *text; /* text title, or */
	const uint32_t *icon;         /* MBAR_ICON_SIZE² ARGB icon title */
	bool dimmed;
	/* Filled in by mbar_layout_*: */
	int ink_l, ink_r; /* text ink, or the icon cell */
	int hi_l, hi_r;   /* highlight extent; also the hit-test area */
};

/* Lay titles out left to right from the left screen edge (an icon title
 * first, if any) or right to left ending at the right screen edge. */
void mbar_layout_left(struct mbar_title *titles, int n);
void mbar_layout_right(struct mbar_title *titles, int n, int screen_w);

/* Paint the bar (rows 0..19) across screen_w, with titles. highlighted is
 * an index into titles, or -1. Several arrays can be painted in turn. */
void mbar_paint_background(struct pl_canvas *c, int screen_w);
void mbar_paint_titles(struct pl_canvas *c, const struct mbar_title *titles,
		int n, int highlighted, struct pl_accent accent);
int mbar_title_at(const struct mbar_title *titles, int n, int x);

struct menu_item {
	const struct plat_text *label; /* NULL means a separator */
	const struct plat_text *key;   /* ⌘ shortcut character, or NULL */
	bool enabled;
	bool checked;
	bool submenu; /* draws the hierarchical-menu arrow */
	uint32_t swatch; /* ARGB color square before the text, or 0 */
	int indent;      /* text moves right this much (room for an icon the
	                    caller paints), or 0 */
};

void menu_measure(const struct menu_item *items, int n, int *width, int *height);
/* Canvas coordinates are menu-local: (0,0) is the top-left border pixel,
 * which sits on the menu bar's bottom line. Paints the shadow too
 * (one extra column and row). */
void menu_paint(struct pl_canvas *c, const struct menu_item *items, int n,
		int width, int height, int selected, struct pl_accent accent);
/* Item index at menu-local y, or -1 (separators and borders count as -1). */
int menu_item_at(const struct menu_item *items, int n, int height, int y);
/* Menu-local y of item i's top row. */
int menu_item_top(const struct menu_item *items, int n, int i);

#ifdef __cplusplus
}
#endif

#endif
