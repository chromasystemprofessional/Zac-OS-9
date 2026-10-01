#include "decor.h"

/* Mac 8-bit system palette grays are multiples of 0x111111. */
#define GRAY(n) (0xFF000000u | (uint32_t)(n) * 0x111111u)

#define C_BLACK GRAY(0x0)
#define C_222 GRAY(0x2)
#define C_555 GRAY(0x5)
#define C_666 GRAY(0x6)
#define C_777 GRAY(0x7)
#define C_888 GRAY(0x8)
#define C_999 GRAY(0x9)
#define C_AAA GRAY(0xA)
#define C_CCC GRAY(0xC)
#define C_DDD GRAY(0xD)
#define C_WHITE GRAY(0xF)

/* Title-bar boxes: 12x12 at y=4, 1 px emboss right and below. */
#define BOX_Y 4
#define BOX_SIZE 12
#define CLOSE_X 4
#define ZOOM_X_FROM_RIGHT 33
#define COLLAPSE_X_FROM_RIGHT 17
#define BOX_STRIPE_GAP 4 /* clear pixels between a box's emboss and stripes */
#define STRIPE_TEXT_GAP 4 /* clear pixels between stripes and title ink */
#define TITLE_BASELINE 14
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

static void put(struct decor_canvas *c, int x, int y, uint32_t color) {
	if (x < c->x || y < c->y || x >= c->x + c->width || y >= c->y + c->height) {
		return;
	}
	c->px[(y - c->y) * c->stride + (x - c->x)] = color;
}

static void hline(struct decor_canvas *c, int x0, int x1, int y, uint32_t color) {
	for (int x = x0; x <= x1; x++) {
		put(c, x, y, color);
	}
}

static void vline(struct decor_canvas *c, int x, int y0, int y1, uint32_t color) {
	for (int y = y0; y <= y1; y++) {
		put(c, x, y, color);
	}
}

static void fill(struct decor_canvas *c, int x0, int y0, int x1, int y1,
		uint32_t color) {
	for (int y = y0; y <= y1; y++) {
		hline(c, x0, x1, y, color);
	}
}

static void outline(struct decor_canvas *c, int x0, int y0, int x1, int y1,
		uint32_t color) {
	hline(c, x0, x1, y0, color);
	hline(c, x0, x1, y1, color);
	vline(c, x0, y0, y1, color);
	vline(c, x1, y0, y1, color);
}

static int frame_height(const struct decor_state *st) {
	return st->collapsed ? DECOR_COLLAPSED_H : st->height;
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

static void paint_box(struct decor_canvas *c, int bx, int by,
		enum decor_part part, bool pressed) {
	hline(c, bx, bx + 11, by, C_888);
	vline(c, bx, by, by + 11, C_888);
	outline(c, bx + 1, by + 1, bx + 11, by + 11, C_222);

	/* 9x9 interior: bevel ring around a 7x7 diagonal gradient.
	 * TODO: pressed state is not in the HIG figures; this inverted, darker
	 * rendering is a placeholder until measured. */
	int ix = bx + 2, iy = by + 2;
	uint32_t hi = pressed ? C_555 : C_CCC;
	uint32_t lo = pressed ? C_CCC : C_888;
	int base = pressed ? 0x5 : 0x9;
	put(c, ix, iy, pressed ? C_555 : C_WHITE);
	hline(c, ix + 1, ix + 8, iy, hi);
	vline(c, ix, iy + 1, iy + 8, hi);
	hline(c, ix + 1, ix + 8, iy + 8, lo);
	vline(c, ix + 8, iy + 1, iy + 7, lo);
	for (int j = 0; j < 7; j++) {
		for (int i = 0; i < 7; i++) {
			put(c, ix + 1 + i, iy + 1 + j, GRAY(base + (i + j) / 2));
		}
	}

	vline(c, bx + 12, by + 1, by + 12, C_WHITE);
	hline(c, bx + 1, bx + 12, by + 12, C_WHITE);

	if (part == DECOR_PART_ZOOM) {
		vline(c, bx + 7, by + 2, by + 7, C_222);
		hline(c, bx + 2, bx + 7, by + 7, C_222);
	} else if (part == DECOR_PART_COLLAPSE) {
		hline(c, bx + 1, bx + 11, by + 5, C_222);
		hline(c, bx + 1, bx + 11, by + 7, C_222);
	}
}

static void paint_stripes(struct decor_canvas *c, int x0, int x1) {
	if (x1 - x0 < 1) {
		return;
	}
	for (int k = 0; k < 6; k++) {
		hline(c, x0, x1 - 1, BOX_Y + 2 * k, C_WHITE);
		hline(c, x0 + 1, x1, BOX_Y + 2 * k + 1, C_777);
	}
}

/* Horizontal extent available to stripes and title, inclusive. */
static void title_span(const struct decor_state *st, int *x0, int *x1) {
	*x0 = st->has_close ? CLOSE_X + BOX_SIZE + 1 + BOX_STRIPE_GAP : CLOSE_X;
	int right_box = st->width;
	if (st->has_zoom) {
		right_box = st->width - ZOOM_X_FROM_RIGHT;
	} else if (st->has_collapse) {
		right_box = st->width - COLLAPSE_X_FROM_RIGHT;
	}
	*x1 = right_box < st->width ? right_box - BOX_STRIPE_GAP - 1 : st->width - 1 - CLOSE_X;
}

int decor_title_max_width(const struct decor_state *st) {
	int x0, x1;
	title_span(st, &x0, &x1);
	int center = (st->width - 1) / 2;
	int half = center - x0 < x1 - center ? center - x0 : x1 - center;
	return half > 0 ? 2 * half + 1 : 0;
}

static void paint_title(struct decor_canvas *c, const struct decor_state *st) {
	int x0, x1;
	title_span(st, &x0, &x1);

	const struct plat_text *t = st->title;
	if (!t || t->ink_l < 0) {
		if (st->active) {
			paint_stripes(c, x0, x1);
		}
		return;
	}

	int ink_w = t->ink_r - t->ink_l + 1;
	int ink_l = (st->width - 1) / 2 - (ink_w - 1) / 2;
	int ink_r = ink_l + ink_w - 1;
	int ox = ink_l - t->ink_l;
	int oy = TITLE_BASELINE - t->baseline;
	uint32_t color = st->active ? C_BLACK : C_666;
	for (int y = 0; y < t->height; y++) {
		for (int x = 0; x < t->width; x++) {
			if (t->mask[y * t->stride + x]) {
				put(c, ox + x, oy + y, color);
			}
		}
	}

	if (st->active) {
		paint_stripes(c, x0, ink_l - STRIPE_TEXT_GAP - 1);
		paint_stripes(c, ink_r + STRIPE_TEXT_GAP + 1, x1);
	}
}

static void paint_grow(struct decor_canvas *c, const struct decor_state *st) {
	int gx = st->width - DECOR_GROW_INSET, gy = st->height - DECOR_GROW_INSET;
	if (st->active) {
		for (int j = 0; j < 20; j++) {
			for (int i = 0; i < 20; i++) {
				char ch = grow_active[j][i];
				int v = ch <= '9' ? ch - '0' : ch - 'a' + 10;
				put(c, gx + i, gy + j, GRAY(v));
			}
		}
		return;
	}
	/* Inactive: blank cell; the well lines stop at it. */
	int x1 = st->width - DECOR_RIGHT, y1 = st->height - DECOR_BOTTOM;
	fill(c, gx + 1, gy + 1, x1, y1, C_DDD);
	hline(c, gx, x1, gy, C_555);
	vline(c, gx, gy, y1, C_555);
}

void decor_paint(struct decor_canvas *c, const struct decor_state *st) {
	const int W = st->width, H = frame_height(st);
	const bool act = st->active;
	const uint32_t border = act ? C_BLACK : C_555;
	const uint32_t face = act ? C_CCC : C_DDD;

	/* Frame fill, never touching the client area. */
	if (st->collapsed) {
		fill(c, 0, 0, W - 1, H - 1, face);
	} else {
		fill(c, 0, 0, W - 1, DECOR_TOP - 1, face);
		fill(c, 0, H - DECOR_BOTTOM, W - 1, H - 1, face);
		fill(c, 0, DECOR_TOP, DECOR_LEFT - 1, H - DECOR_BOTTOM - 1, face);
		fill(c, W - DECOR_RIGHT, DECOR_TOP, W - 1, H - DECOR_BOTTOM - 1, face);
	}

	outline(c, 0, 0, W - 1, H - 1, border);
	if (act) {
		hline(c, 1, W - 3, 1, C_WHITE);
		vline(c, 1, 1, H - 3, C_WHITE);
		vline(c, W - 2, 2, H - 2, C_999);
		hline(c, 2, W - 2, H - 2, C_999);
	}

	/* Drop shadow: 1 px, offset 2 px in from the corners. */
	vline(c, W, 2, H, border);
	hline(c, 2, W, H, border);

	if (!st->collapsed) {
		if (act) {
			hline(c, 4, W - 6, 20, C_999);
			vline(c, 4, 20, H - 6, C_999);
		}
		outline(c, 5, 21, W - 6, H - 6, border);
		if (act) {
			vline(c, W - 5, 21, H - 5, C_WHITE);
			hline(c, 4, W - 5, H - 5, C_WHITE);
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
