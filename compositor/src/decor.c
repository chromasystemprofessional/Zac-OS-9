#include "decor.h"
#include "icons.h"

#define C_222 GRAY(0x2)
#define C_555 GRAY(0x5)
#define C_666 GRAY(0x6)
#define C_777 GRAY(0x7)
#define C_888 GRAY(0x8)
#define C_999 GRAY(0x9)
#define C_AAA GRAY(0xA)
#define C_CCC GRAY(0xC)
#define C_DDD GRAY(0xD)

/* Title-bar boxes: 12x12 at y=4, 1 px emboss right and below. */
#define BOX_Y 4
#define BOX_SIZE 12
#define CLOSE_X 4
#define ZOOM_X_FROM_RIGHT 33
#define COLLAPSE_X_FROM_RIGHT 17
#define BOX_STRIPE_GAP 4 /* clear pixels between a box's emboss and stripes */
#define STRIPE_TEXT_GAP 4 /* clear pixels between stripes and title ink */
#define TITLE_BASELINE 14
/* The proxy icon: a small icon, then a gap, then the title; the two
 * centred together. TODO: measure against a Mac OS 9 Finder window. */
#define PROXY_SIZE 16
#define PROXY_GAP 4
#define PROXY_Y 3
#define NO_BOX_STRIPE_X 5
#define TITLE_BAR_LAST_ROW 19

/* Active resize box, 20x20 from (W-21, H-21) to (W-2, H-2), measured from
 * HIG figure 5-1. Hex digit = gray level. */
static const char *const grow_active[20] = {
	"0000000000000000fcc9",
	"0ffffffffffffffffcc9",
	"0fccccccccccccccccc9",
	"0fccccccccccccccccc9",
	"0fcccccccffcccccccc9",
	"0fccccccfc7cccccccc9",
	"0fcccccfc7cffcccccc9",
	"0fccccfc7cfc7cccccc9",
	"0fcccfc7cfc7cffcccc9",
	"0fccfc7cfc7cfc7cccc9",
	"0fcca7cfc7cfc7ccccc9",
	"0fccccfc7cfc7cccccc9",
	"0fcccca7cfc7ccccccc9",
	"0fccccccfc7cccccccc9",
	"0fcccccca7ccccccccc9",
	"0fccccccccccccccccc9",
	"ffccccccccccccccccc9",
	"ccccccccccccccccccc9",
	"ccccccccccccccccccc9",
	"99999999999999999999",
};

/* A pop-up's tab: room either side of the title, and its narrowest. */
#define TAB_PAD 12
#define TAB_MIN_W 64

static int frame_height(const struct decor_state *st) {
	if (st->collapsed) {
		return st->style == DECOR_STYLE_POPUP ? DECOR_TAB_H : DECOR_COLLAPSED_H;
	}
	return st->height;
}

int decor_frame_height(const struct decor_state *st) {
	return frame_height(st);
}

void decor_box_origin(const struct decor_state *st, enum decor_part part,
		int *bx, int *by) {
	*by = BOX_Y;
	switch (part) {
	case DECOR_PART_CLOSE:
		*bx = CLOSE_X;
		break;
	case DECOR_PART_ZOOM:
		*bx = st->width - ZOOM_X_FROM_RIGHT;
		break;
	case DECOR_PART_COLLAPSE:
		*bx = st->width - COLLAPSE_X_FROM_RIGHT;
		break;
	default:
		*bx = *by = -1;
		break;
	}
}

static void paint_box(struct pl_canvas *c, int bx, int by,
		enum decor_part part, bool pressed) {
	pl_hline(c, bx, bx + 11, by, C_888);
	pl_vline(c, bx, by, by + 11, C_888);
	pl_outline(c, bx + 1, by + 1, bx + 11, by + 11, C_222);

	/* 9x9 interior: bevel ring around a 7x7 diagonal gradient.
	 * TODO: pressed state is not in the HIG figures; this inverted, darker
	 * rendering is a placeholder until measured. */
	int ix = bx + 2, iy = by + 2;
	uint32_t hi = pressed ? C_555 : C_CCC;
	uint32_t lo = pressed ? C_CCC : C_888;
	int base = pressed ? 0x5 : 0x9;
	pl_put(c, ix, iy, pressed ? C_555 : C_WHITE);
	pl_hline(c, ix + 1, ix + 8, iy, hi);
	pl_vline(c, ix, iy + 1, iy + 8, hi);
	pl_hline(c, ix + 1, ix + 8, iy + 8, lo);
	pl_vline(c, ix + 8, iy + 1, iy + 7, lo);
	for (int j = 0; j < 7; j++) {
		for (int i = 0; i < 7; i++) {
			pl_put(c, ix + 1 + i, iy + 1 + j, GRAY(base + (i + j) / 2));
		}
	}

	pl_vline(c, bx + 12, by + 1, by + 12, C_WHITE);
	pl_hline(c, bx + 1, bx + 12, by + 12, C_WHITE);

	if (part == DECOR_PART_ZOOM) {
		pl_vline(c, bx + 7, by + 2, by + 7, C_222);
		pl_hline(c, bx + 2, bx + 7, by + 7, C_222);
	} else if (part == DECOR_PART_COLLAPSE) {
		pl_hline(c, bx + 1, bx + 11, by + 5, C_222);
		pl_hline(c, bx + 1, bx + 11, by + 7, C_222);
	}
}

static void paint_stripes(struct pl_canvas *c, int x0, int x1) {
	if (x1 - x0 < 1) {
		return;
	}
	for (int k = 0; k < 6; k++) {
		pl_hline(c, x0, x1 - 1, BOX_Y + 2 * k, C_WHITE);
		pl_hline(c, x0 + 1, x1, BOX_Y + 2 * k + 1, C_777);
	}
}

/* Horizontal extent available to stripes and title, inclusive. */
static void title_span(const struct decor_state *st, int *x0, int *x1) {
	/* Without boxes the stripes run from x=5 to W-6 (HIG figure 3-2). */
	*x0 = st->has_close ? CLOSE_X + BOX_SIZE + 1 + BOX_STRIPE_GAP : NO_BOX_STRIPE_X;
	int right_box = st->width;
	if (st->has_zoom) {
		right_box = st->width - ZOOM_X_FROM_RIGHT;
	} else if (st->has_collapse) {
		right_box = st->width - COLLAPSE_X_FROM_RIGHT;
	}
	*x1 = right_box < st->width ? right_box - BOX_STRIPE_GAP - 1
		: st->width - 1 - NO_BOX_STRIPE_X;
}

struct decor_margins decor_margins(enum decor_style style) {
	if (style == DECOR_STYLE_POPUP) {
		/* The body's edge is a document window's, under the tab. */
		return (struct decor_margins){ .left = DECOR_LEFT, .right = DECOR_RIGHT,
			.top = DECOR_TAB_H + 6, .bottom = DECOR_BOTTOM };
	}
	if (style == DECOR_STYLE_MOVABLE_MODAL) {
		return (struct decor_margins){ .left = 3, .right = 3, .top = 24, .bottom = 3 };
	}
	return (struct decor_margins){
		.left = DECOR_LEFT, .right = DECOR_RIGHT, .top = DECOR_TOP, .bottom = DECOR_BOTTOM,
	};
}

int decor_title_max_width(const struct decor_state *st) {
	if (st->style == DECOR_STYLE_POPUP) {
		const int width = st->width - 2 * TAB_PAD;
		return width > 0 ? width : 0;
	}
	int x0, x1;
	title_span(st, &x0, &x1);
	int center = (st->width - 1) / 2;
	int half = center - x0 < x1 - center ? center - x0 : x1 - center;
	int width = half > 0 ? 2 * half + 1 : 0;
	if (st->proxy) {
		width -= PROXY_SIZE + PROXY_GAP;
	}
	return width > 0 ? width : 0;
}

/* Where the title's ink starts, and the proxy icon's left (if any): the
 * icon and the title are centred as one. */
static void title_layout(const struct decor_state *st, int ink_w, int *ink_l, int *proxy_x) {
	const int group = ink_w + (st->proxy ? PROXY_SIZE + PROXY_GAP : 0);
	const int left = (st->width - 1) / 2 - (group - 1) / 2;
	*proxy_x = left;
	*ink_l = st->proxy ? left + PROXY_SIZE + PROXY_GAP : left;
}

static int title_ink_width(const struct decor_state *st) {
	const struct plat_text *t = st->title;
	return t && t->ink_l >= 0 ? t->ink_r - t->ink_l + 1 : 0;
}

bool decor_title_hit(const struct decor_state *st, int x, int y) {
	const struct plat_text *t = st->title;
	if (st->style == DECOR_STYLE_POPUP || !t || t->ink_l < 0 || y < 0 || y >= DECOR_TOP - 2) {
		return false;
	}
	const int ink_w = title_ink_width(st);
	int ink_l, proxy_x;
	title_layout(st, ink_w, &ink_l, &proxy_x);
	return x >= ink_l - STRIPE_TEXT_GAP && x <= ink_l + ink_w - 1 + STRIPE_TEXT_GAP;
}

bool decor_proxy_hit(const struct decor_state *st, int x, int y) {
	if (!st->proxy || st->style != DECOR_STYLE_DOCUMENT) {
		return false;
	}
	int ink_l, proxy_x;
	title_layout(st, title_ink_width(st), &ink_l, &proxy_x);
	return x >= proxy_x - 1 && x <= proxy_x + PROXY_SIZE && y >= PROXY_Y - 1 &&
		y <= PROXY_Y + PROXY_SIZE;
}

static void paint_title(struct pl_canvas *c, const struct decor_state *st) {
	int x0, x1;
	title_span(st, &x0, &x1);

	const struct plat_text *t = st->title;
	if ((!t || t->ink_l < 0) && !st->proxy) {
		if (st->active) {
			paint_stripes(c, x0, x1);
		}
		return;
	}

	const int ink_w = title_ink_width(st);
	int ink_l, proxy_x;
	title_layout(st, ink_w, &ink_l, &proxy_x);
	int ink_r = ink_l + ink_w - 1;
	if (ink_w > 0) {
		pl_text(c, t, ink_l, TITLE_BASELINE, st->active ? C_BLACK : C_666);
	}
	int left = ink_l;
	if (st->proxy) {
		pl_icon_paint(c, proxy_x, PROXY_Y, (enum pl_icon_kind)(st->proxy - 1), PROXY_SIZE, false);
		left = proxy_x;
		if (ink_w == 0) {
			ink_r = proxy_x + PROXY_SIZE - 1;
		}
	}

	if (st->active) {
		paint_stripes(c, x0, left - STRIPE_TEXT_GAP - 1);
		paint_stripes(c, ink_r + STRIPE_TEXT_GAP + 1, x1);
	}
}

static void paint_grow(struct pl_canvas *c, const struct decor_state *st) {
	int gx = st->width - DECOR_GROW_INSET, gy = st->height - DECOR_GROW_INSET;
	if (st->active) {
		pl_grays(c, gx, gy, grow_active, 20);
		return;
	}
	/* Inactive: blank cell; the well lines stop at it. */
	int x1 = st->width - DECOR_RIGHT, y1 = st->height - DECOR_BOTTOM;
	pl_fill(c, gx + 1, gy + 1, x1, y1, C_DDD);
	pl_hline(c, gx, x1, gy, C_555);
	pl_vline(c, gx, gy, y1, C_555);
}

/* Movable modal dialog (HIG figure 3-2): a document-style title bar
 * without boxes over a body edged by a 2 px bevel. Margins 3/3/24/3. */
static void paint_modal(struct pl_canvas *c, const struct decor_state *st) {
	const int W = st->width, H = st->height;
	const bool act = st->active;
	const uint32_t border = act ? C_BLACK : C_555;
	const uint32_t face = act ? C_CCC : C_DDD;

	/* Title bar: rows 0..21, as a collapsed document window's. */
	pl_fill(c, 0, 0, W - 1, 21, face);
	/* Body edges (the client paints the interior). */
	pl_fill(c, 0, 22, 2, H - 1, C_DDD);
	pl_fill(c, W - 3, 22, W - 1, H - 1, C_DDD);
	pl_fill(c, 0, 22, W - 1, 23, C_DDD);
	pl_fill(c, 0, H - 3, W - 1, H - 1, C_DDD);

	pl_outline(c, 0, 0, W - 1, H - 1, border);
	if (act) {
		pl_hline(c, 1, W - 3, 1, C_WHITE);
		pl_vline(c, 1, 1, 19, C_WHITE);
		pl_vline(c, W - 2, 2, 20, C_999);
		pl_hline(c, 2, W - 2, 20, C_999);
	}
	pl_hline(c, 0, W - 1, 21, border);

	if (act) {
		pl_hline(c, 2, W - 4, 23, C_WHITE);
		pl_vline(c, 2, 23, H - 4, C_WHITE);
		pl_vline(c, W - 3, 24, H - 3, C_999);
		pl_hline(c, 3, W - 3, H - 3, C_999);
		pl_vline(c, W - 2, 23, H - 2, C_555);
		pl_hline(c, 2, W - 2, H - 2, C_555);
	}

	pl_vline(c, W, 2, H, border);
	pl_hline(c, 2, W, H, border);
	paint_title(c, st);
}

int decor_tab_width(const struct decor_state *st) {
	int w = title_ink_width(st) + 2 * TAB_PAD;
	w = w < TAB_MIN_W ? TAB_MIN_W : w;
	return w > st->width ? st->width : w;
}

/* A pop-up window: the tab (rows 0..DECOR_TAB_H-1) with its top corners
 * cut, standing on a body edged like a document window, which opens into
 * the tab. Collapsed, the tab alone, at the bottom of the screen. */
static void paint_popup(struct pl_canvas *c, const struct decor_state *st) {
	const int W = st->width, H = frame_height(st);
	const bool act = st->active;
	const uint32_t border = act ? C_BLACK : C_555;
	const uint32_t face = act ? C_CCC : C_DDD;
	const int x0 = st->tab_x, x1 = st->tab_x + decor_tab_width(st) - 1;
	const int y0 = DECOR_TAB_H; /* the body's top row */

	if (!st->collapsed) {
		pl_fill(c, 0, y0, W - 1, y0 + 5, face);
		pl_fill(c, 0, H - DECOR_BOTTOM, W - 1, H - 1, face);
		pl_fill(c, 0, y0, DECOR_LEFT - 1, H - 1, face);
		pl_fill(c, W - DECOR_RIGHT, y0, W - 1, H - 1, face);
		pl_outline(c, 0, y0, W - 1, H - 1, border);
		if (act) {
			pl_hline(c, 1, W - 3, y0 + 1, C_WHITE);
			pl_vline(c, 1, y0 + 1, H - 3, C_WHITE);
			pl_vline(c, W - 2, y0 + 2, H - 2, C_999);
			pl_hline(c, 2, W - 2, H - 2, C_999);
			pl_hline(c, 4, W - 6, y0 + 4, C_999);
			pl_vline(c, 4, y0 + 4, H - 6, C_999);
		}
		pl_outline(c, 5, y0 + 5, W - 6, H - 6, border);
		if (act) {
			pl_vline(c, W - 5, y0 + 5, H - 5, C_WHITE);
			pl_hline(c, 4, W - 5, H - 5, C_WHITE);
		}
		/* The resizing corners, marked off on the top edge. */
		pl_vline(c, DECOR_POPUP_CORNER, y0 + 1, y0 + 4, C_888);
		pl_vline(c, W - 1 - DECOR_POPUP_CORNER, y0 + 1, y0 + 4, C_888);
	}

	/* The tab: face, a black edge with its two top corners cut, the
	 * emboss of a title bar. A closed one along the bottom of the screen
	 * always reads as active. */
	const bool tab_act = act || st->collapsed;
	const uint32_t tab_border = tab_act ? C_BLACK : C_555;
	const uint32_t tab_face = tab_act ? C_CCC : C_DDD;
	pl_fill(c, x0 + 1, 1, x1 - 1, H > y0 ? y0 : H - 1, tab_face);
	pl_hline(c, x0 + 2, x1 - 2, 0, tab_border);
	pl_put(c, x0 + 1, 1, tab_border);
	pl_put(c, x1 - 1, 1, tab_border);
	pl_vline(c, x0, 2, y0 - 1, tab_border);
	pl_vline(c, x1, 2, y0 - 1, tab_border);
	if (tab_act) {
		pl_hline(c, x0 + 2, x1 - 2, 1, C_WHITE);
		pl_vline(c, x0 + 1, 2, st->collapsed ? y0 - 1 : y0 + 1, C_WHITE);
		pl_vline(c, x1 - 1, 2, st->collapsed ? y0 - 1 : y0 + 1, C_999);
	}
	if (!st->collapsed) {
		/* Open: the tab's sides run on into the body's edge. */
		pl_put(c, x0, y0, tab_border);
		pl_put(c, x1, y0, tab_border);
	}

	const struct plat_text *t = st->title;
	const int ink_w = title_ink_width(st);
	if (ink_w > 0) {
		pl_text(c, t, x0 + (x1 - x0 + 1 - ink_w) / 2, TITLE_BASELINE,
			tab_act ? C_BLACK : C_666);
	}
}

void decor_paint(struct pl_canvas *c, const struct decor_state *st) {
	if (st->style == DECOR_STYLE_MOVABLE_MODAL) {
		paint_modal(c, st);
		return;
	}
	if (st->style == DECOR_STYLE_POPUP) {
		paint_popup(c, st);
		return;
	}
	const int W = st->width, H = frame_height(st);
	const bool act = st->active;
	const uint32_t border = act ? C_BLACK : C_555;
	const uint32_t face = act ? C_CCC : C_DDD;

	/* Frame fill, never touching the client area. */
	if (st->collapsed) {
		pl_fill(c, 0, 0, W - 1, H - 1, face);
	} else {
		pl_fill(c, 0, 0, W - 1, DECOR_TOP - 1, face);
		pl_fill(c, 0, H - DECOR_BOTTOM, W - 1, H - 1, face);
		pl_fill(c, 0, DECOR_TOP, DECOR_LEFT - 1, H - DECOR_BOTTOM - 1, face);
		pl_fill(c, W - DECOR_RIGHT, DECOR_TOP, W - 1, H - DECOR_BOTTOM - 1, face);
	}

	pl_outline(c, 0, 0, W - 1, H - 1, border);
	if (act) {
		pl_hline(c, 1, W - 3, 1, C_WHITE);
		pl_vline(c, 1, 1, H - 3, C_WHITE);
		pl_vline(c, W - 2, 2, H - 2, C_999);
		pl_hline(c, 2, W - 2, H - 2, C_999);
	}

	/* Drop shadow: 1 px, offset 2 px in from the corners. */
	pl_vline(c, W, 2, H, border);
	pl_hline(c, 2, W, H, border);

	if (!st->collapsed) {
		if (act) {
			pl_hline(c, 4, W - 6, 20, C_999);
			pl_vline(c, 4, 20, H - 6, C_999);
		}
		pl_outline(c, 5, 21, W - 6, H - 6, border);
		if (act) {
			pl_vline(c, W - 5, 21, H - 5, C_WHITE);
			pl_hline(c, 4, W - 5, H - 5, C_WHITE);
		}
		if (st->has_grow) {
			paint_grow(c, st);
		}
	}

	if (act) {
		int bx, by;
		const enum decor_part boxes[] = {
			DECOR_PART_CLOSE, DECOR_PART_ZOOM, DECOR_PART_COLLAPSE,
		};
		const bool present[] = { st->has_close, st->has_zoom, st->has_collapse };
		for (int i = 0; i < 3; i++) {
			if (present[i]) {
				decor_box_origin(st, boxes[i], &bx, &by);
				paint_box(c, bx, by, boxes[i], st->pressed == boxes[i]);
			}
		}
	}
	paint_title(c, st);
}

static bool in_box(const struct decor_state *st, enum decor_part part, int x, int y) {
	int bx, by;
	decor_box_origin(st, part, &bx, &by);
	return x >= bx && x <= bx + BOX_SIZE && y >= by && y <= by + BOX_SIZE;
}

enum decor_part decor_hit(const struct decor_state *st, int x, int y) {
	const int W = st->width, H = frame_height(st);
	if (x < 0 || y < 0 || x >= W || y >= H) {
		return DECOR_PART_NONE;
	}
	if (st->style == DECOR_STYLE_POPUP) {
		/* Above the body only the tab is there; the rest lets clicks
		 * through, as does all but the tab when it is closed. */
		if (y < DECOR_TAB_H) {
			return x >= st->tab_x && x < st->tab_x + decor_tab_width(st)
				? DECOR_PART_TAB : DECOR_PART_NONE;
		}
		if (st->collapsed) {
			return DECOR_PART_NONE;
		}
		const struct decor_margins m = decor_margins(st->style);
		if (y < DECOR_TAB_H + DECOR_POPUP_CORNER && (x < DECOR_POPUP_CORNER ||
				x >= W - DECOR_POPUP_CORNER) && !(x >= m.left && x < W - m.right &&
				y >= m.top)) {
			return x < DECOR_POPUP_CORNER ? DECOR_PART_GROW_TOP_LEFT
				: DECOR_PART_GROW_TOP_RIGHT;
		}
		if (x >= m.left && x < W - m.right && y >= m.top && y < H - m.bottom) {
			return DECOR_PART_CLIENT;
		}
		return DECOR_PART_BORDER;
	}
	if (st->style == DECOR_STYLE_MOVABLE_MODAL) {
		/* Dialogs move by their title bar only. */
		const struct decor_margins m = decor_margins(st->style);
		if (y < 22) {
			return DECOR_PART_DRAG;
		}
		if (x >= m.left && x < W - m.right && y >= m.top && y < H - m.bottom) {
			return DECOR_PART_CLIENT;
		}
		return DECOR_PART_BORDER;
	}
	if (y <= TITLE_BAR_LAST_ROW && st->active) {
		if (st->has_close && in_box(st, DECOR_PART_CLOSE, x, y)) {
			return DECOR_PART_CLOSE;
		}
		if (st->has_zoom && in_box(st, DECOR_PART_ZOOM, x, y)) {
			return DECOR_PART_ZOOM;
		}
		if (st->has_collapse && in_box(st, DECOR_PART_COLLAPSE, x, y)) {
			return DECOR_PART_COLLAPSE;
		}
	}
	if (st->collapsed) {
		return DECOR_PART_DRAG;
	}
	if (st->has_grow && x >= W - DECOR_GROW_INSET && y >= H - DECOR_GROW_INSET) {
		return DECOR_PART_GROW;
	}
	if (x >= DECOR_LEFT && x < W - DECOR_RIGHT &&
			y >= DECOR_TOP && y < H - DECOR_BOTTOM) {
		return DECOR_PART_CLIENT;
	}
	return DECOR_PART_DRAG;
}
