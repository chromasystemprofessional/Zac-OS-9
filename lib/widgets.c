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
