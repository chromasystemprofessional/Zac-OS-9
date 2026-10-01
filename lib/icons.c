#include <stdlib.h>

#include "icons.h"

#define C_444 GRAY(0x4)
#define C_555 GRAY(0x5)
#define C_777 GRAY(0x7)
#define C_888 GRAY(0x8)
#define C_999 GRAY(0x9)
#define C_AAA GRAY(0xA)
#define C_BBB GRAY(0xB)
#define C_CCC GRAY(0xC)
#define C_DDD GRAY(0xD)
#define C_EEE GRAY(0xE)
#define LAV_LIGHT RGB(0xCC, 0xCC, 0xFF)
#define LAV_MID RGB(0x99, 0x99, 0xFF)
#define LAV_DARK RGB(0x66, 0x66, 0xCC)

/* Shapes are designed on a 32-pixel grid; `S(v)` maps a 32-grid coordinate
 * to the target size so the 16-pixel versions keep the same silhouette. */
struct pen {
	struct pl_canvas c;
	int size;
};
#define S(v) ((v) * p->size / 32)

static void box(struct pen *p, int x0, int y0, int x1, int y1, uint32_t fill,
		uint32_t hi, uint32_t lo) {
	int a = S(x0), b = S(y0), c = S(x1), d = S(y1);
	pl_fill(&p->c, a, b, c, d, fill);
	pl_hline(&p->c, a + 1, c - 1, b + 1, hi);
	pl_vline(&p->c, a + 1, b + 1, d - 1, hi);
	pl_hline(&p->c, a + 1, c - 1, d - 1, lo);
	pl_vline(&p->c, c - 1, b + 1, d - 1, lo);
	pl_outline(&p->c, a, b, c, d, C_BLACK);
}

static void folder(struct pen *p) {
	/* Tab, then the body overlapping it. */
	box(p, 2, 6, 13, 11, LAV_LIGHT, C_WHITE, LAV_MID);
	box(p, 1, 9, 30, 27, LAV_LIGHT, C_WHITE, LAV_MID);
	pl_hline(&p->c, S(2), S(30) - 2, S(12), LAV_DARK);
}

static void document(struct pen *p) {
	int x0 = S(6), y0 = S(2), x1 = S(25), y1 = S(29), fold = S(7);
	pl_fill(&p->c, x0, y0, x1, y1, C_WHITE);
	pl_hline(&p->c, x0, x1 - fold, y0, C_BLACK);
	pl_vline(&p->c, x0, y0, y1, C_BLACK);
	pl_vline(&p->c, x1, y0 + fold, y1, C_BLACK);
	pl_hline(&p->c, x0, x1, y1, C_BLACK);
	/* Folded corner: diagonal edge, then the turned-down flap. */
	for (int i = 0; i <= fold; i++) {
		pl_put(&p->c, x1 - fold + i, y0 + i, C_BLACK);
		for (int k = x1 - fold; k < x1 - fold + i; k++) {
			pl_put(&p->c, k, y0 + i, C_DDD);
		}
		for (int k = x1 - fold + i + 1; k <= x1; k++) {
			pl_put(&p->c, k, y0 + i, 0);
		}
	}
	pl_vline(&p->c, x1 - fold, y0, y0 + fold, C_BLACK);
	pl_hline(&p->c, x1 - fold, x1, y0 + fold, C_BLACK);
	if (p->size >= 32) {
		for (int y = y0 + fold + 4; y < y1 - 2; y += 3) {
			pl_hline(&p->c, x0 + 3, x1 - 4, y, C_AAA);
		}
	}
}

static void disk(struct pen *p) {
	box(p, 1, 11, 30, 24, C_CCC, C_EEE, C_888);
	/* Front panel line and the activity light. */
	pl_hline(&p->c, S(3), S(28), S(20), C_888);
	pl_hline(&p->c, S(3), S(28), S(21), C_WHITE);
	pl_fill(&p->c, S(5), S(16), S(5) + (p->size >= 32 ? 2 : 1), S(17), RGB(0x00, 0xBB, 0x00));
	pl_hline(&p->c, S(16), S(26), S(16), C_555);
}

static void trash(struct pen *p, bool full) {
	int top = S(9), bottom = S(29);
	/* Can: slightly narrower at the bottom. */
	for (int y = top; y <= bottom; y++) {
		int inset = (y - top) * S(3) / (bottom - top + 1);
		int l = S(7) + inset, r = S(24) - inset;
		pl_hline(&p->c, l, r, y, C_CCC);
		pl_put(&p->c, l, y, C_BLACK);
		pl_put(&p->c, r, y, C_BLACK);
		pl_put(&p->c, l + 1, y, C_EEE);
		pl_put(&p->c, r - 1, y, C_888);
		if (p->size >= 32 && y > top + 2 && y < bottom - 1) {
			for (int x = l + 4; x < r - 2; x += 4) {
				pl_put(&p->c, x, y, C_888);
			}
		}
	}
	pl_hline(&p->c, S(10), S(21), bottom, C_BLACK);
	/* Lid and handle. */
	box(p, 5, 6, 26, 9, C_DDD, C_WHITE, C_888);
	box(p, 12, 3, 19, 6, C_DDD, C_WHITE, C_888);
	if (full) {
		/* Paper poking out from under the lid. */
		pl_fill(&p->c, S(8), S(3), S(11), S(6), C_WHITE);
		pl_outline(&p->c, S(8), S(3), S(11), S(6), C_BLACK);
		pl_fill(&p->c, S(21), S(2), S(25), S(6), C_WHITE);
		pl_outline(&p->c, S(21), S(2), S(25), S(6), C_BLACK);
	}
}

static void application(struct pen *p) {
	/* A diamond. */
	int cx = S(16), cy = S(16), r = S(13);
	for (int dy = -r; dy <= r; dy++) {
		int w = r - abs(dy);
		pl_hline(&p->c, cx - w, cx + w, cy + dy, dy < 0 ? LAV_LIGHT : LAV_MID);
		pl_put(&p->c, cx - w, cy + dy, C_BLACK);
		pl_put(&p->c, cx + w, cy + dy, C_BLACK);
	}
	pl_hline(&p->c, cx - r, cx + r, cy, LAV_DARK);
	pl_put(&p->c, cx - r, cy, C_BLACK);
	pl_put(&p->c, cx + r, cy, C_BLACK);
	pl_put(&p->c, cx, cy - r, C_BLACK);
	pl_put(&p->c, cx, cy + r, C_BLACK);
}

static void caution(struct pen *p) {
	/* A warning triangle with an exclamation mark. */
	int top = S(2), bottom = S(29);
	for (int y = top; y <= bottom; y++) {
		int half = (y - top) * S(14) / (bottom - top);
		int l = S(16) - half - 1, r = S(16) + half;
		pl_hline(&p->c, l, r, y, RGB(0xFF, 0xCC, 0x00));
		pl_put(&p->c, l, y, C_BLACK);
		pl_put(&p->c, r, y, C_BLACK);
	}
	pl_hline(&p->c, S(2) - 1, S(30), bottom, C_BLACK);
	pl_fill(&p->c, S(15), S(10), S(16), S(21), C_BLACK);
	pl_fill(&p->c, S(15), S(24), S(16), S(25), C_BLACK);
}

static uint32_t *build(enum pl_icon_kind kind, int size) {
	uint32_t *px = calloc((size_t)size * size, sizeof(*px));
	struct pen pen = {
		.c = { .px = px, .stride = size, .width = size, .height = size },
		.size = size,
	};
	struct pen *p = &pen;
	switch (kind) {
	case PL_ICON_FOLDER: folder(p); break;
	case PL_ICON_DOCUMENT: document(p); break;
	case PL_ICON_APPLICATION: application(p); break;
	case PL_ICON_DISK: disk(p); break;
	case PL_ICON_TRASH_EMPTY: trash(p, false); break;
	case PL_ICON_TRASH_FULL: trash(p, true); break;
	case PL_ICON_CAUTION: caution(p); break;
	default: break;
	}
	return px;
}

const uint32_t *pl_icon(enum pl_icon_kind kind, int size) {
	static uint32_t *cache[PL_ICON_COUNT][2];
	int s = size >= 32 ? 0 : 1;
	if (kind < 0 || kind >= PL_ICON_COUNT) {
		kind = PL_ICON_DOCUMENT;
	}
	if (!cache[kind][s]) {
		cache[kind][s] = build(kind, s == 0 ? 32 : 16);
	}
	return cache[kind][s];
}

const struct pl_label pl_labels[PL_LABEL_COUNT] = {
	{ "None", 0 },
	{ "Essential", 0xFFFF6403 },
	{ "Hot", 0xFFDD0806 },
	{ "In Progress", 0xFFF20884 },
	{ "Cool", 0xFF02ABEA },
	{ "Personal", 0xFF0000D4 },
	{ "Project 1", 0xFF1FB714 },
	{ "Project 2", 0xFF562C05 },
};

/* Each channel scaled by the label's: white becomes the label color and
 * black stays black. TODO: compare with a real Mac's labelled icons. */
static uint32_t tint(uint32_t v, uint32_t label) {
	uint32_t out = 0xFF000000u;
	for (int shift = 0; shift < 24; shift += 8) {
		uint32_t a = (v >> shift) & 0xFF, b = (label >> shift) & 0xFF;
		out |= (a * b / 255) << shift;
	}
	return out;
}

void pl_icon_paint(struct pl_canvas *c, int x, int y, enum pl_icon_kind kind,
		int size, bool selected) {
	pl_icon_paint_label(c, x, y, kind, size, selected, 0);
}

void pl_icon_paint_label(struct pl_canvas *c, int x, int y, enum pl_icon_kind kind,
		int size, bool selected, uint32_t label_color) {
	const uint32_t *px = pl_icon(kind, size);
	for (int j = 0; j < size; j++) {
		for (int i = 0; i < size; i++) {
			uint32_t v = px[j * size + i];
			if (!(v >> 24)) {
				continue;
			}
			if (label_color) {
				v = tint(v, label_color);
			}
			if (selected) {
				v = 0xFF000000u | ((v >> 1) & 0x7F7F7F);
			}
			pl_put(c, x + i, y + j, v);
		}
	}
}
