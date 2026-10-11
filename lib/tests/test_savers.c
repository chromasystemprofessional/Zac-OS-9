/*
 * The screen savers (lib/savers.c): which there are, that each draws a
 * whole frame that changes as time passes, the same way from the same
 * seed, and stays inside its canvas.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "savers.h"

static int fails;

static void check(bool ok, const char *what) {
	printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

enum { W = 320, H = 200 };

/* A frame of saver `id` at `t` seconds (after drawing every 40 ms up to it). */
static void frame(const char *id, unsigned seed, double t, uint32_t *px) {
	struct pl_saver *s = pl_saver_new(pl_saver_find(id), W, H, seed);
	struct pl_canvas c = { px, W, 0, 0, W, H };
	for (double at = 0; at < t; at += 0.04) {
		pl_saver_draw(s, &c, at);
	}
	pl_saver_draw(s, &c, t);
	pl_saver_free(s);
}

static int count_not(const uint32_t *px, uint32_t color) {
	int n = 0;
	for (int i = 0; i < W * H; i++) {
		n += px[i] != color;
	}
	return n;
}

int main(void) {
	static uint32_t a[W * H], b[W * H];

	check(pl_saver_count() == 4 && pl_saver_find("patterns") == 0 && pl_saver_find("stars") == 1 &&
		pl_saver_find("logo") == 2 && pl_saver_find("blank") == 3, "four savers, found by id");
	check(pl_saver_find("nonsense") == 0 && pl_saver_find(NULL) == 0 && !pl_saver_id(9) &&
		strcmp(pl_saver_name(0), "Desktop Patterns") == 0, "an unknown one is the default");

	memset(a, 0x7f, sizeof(a));
	frame("blank", 1, 2, a);
	check(count_not(a, C_BLACK) == 0, "Blank Screen is black, every pixel of it");

	const char *moving[] = { "patterns", "stars", "logo" };
	for (int i = 0; i < 3; i++) {
		char what[96];
		memset(a, 0, sizeof(a));
		frame(moving[i], 7, 1.0, a);
		frame(moving[i], 7, 1.0, b);
		snprintf(what, sizeof(what), "%s: the same seed draws the same frame", moving[i]);
		check(memcmp(a, b, sizeof(a)) == 0, what);
		bool opaque = true;
		for (int p = 0; p < W * H; p++) {
			opaque &= (a[p] >> 24) == 0xFF;
		}
		snprintf(what, sizeof(what), "%s: a whole opaque frame, not all one colour", moving[i]);
		check(opaque && count_not(a, a[0]) > 0, what);
		frame(moving[i], 7, 3.0, b);
		snprintf(what, sizeof(what), "%s: it moves", moving[i]);
		check(memcmp(a, b, sizeof(a)) != 0, what);
	}

	frame("patterns", 7, 1.0, a);
	frame("patterns", 7, 13.5, b);
	int same_rows = 0;
	for (int y = 0; y < H; y++) {
		for (int y2 = 0; y2 < H; y2++) {
			if (memcmp(a + y * W, b + y2 * W, W * sizeof(uint32_t)) == 0) {
				same_rows++;
				break;
			}
		}
	}
	check(same_rows < H, "Desktop Patterns goes on to another pattern");

	/* The logo bounces: after 30 s it is still wholly on screen. */
	struct pl_saver *s = pl_saver_new(pl_saver_find("logo"), W, H, 3);
	struct pl_canvas c = { a, W, 0, 0, W, H };
	for (double t = 0; t < 30; t += 0.04) {
		pl_saver_draw(s, &c, t);
	}
	int left = W, right = -1, top = H, bottom = -1;
	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			if (a[y * W + x] != C_BLACK) {
				left = x < left ? x : left;
				right = x > right ? x : right;
				top = y < top ? y : top;
				bottom = y > bottom ? y : bottom;
			}
		}
	}
	pl_saver_free(s);
	check(right >= 0 && right - left < 64 && bottom - top < 64 && left >= 0 && right < W,
		"Bouncing Logo shows the logo, after 30 s of bouncing");

	/* A saver for a bigger screen than its canvas only draws inside it. */
	static uint32_t big[(W + 16) * (H + 16)];
	for (int i = 0; i < pl_saver_count(); i++) {
		memset(big, 0x55, sizeof(big));
		struct pl_saver *wide = pl_saver_new(i, W * 2, H * 2, 5);
		struct pl_canvas inner = { big + 8 * (W + 16) + 8, W + 16, 0, 0, W, H };
		for (double t = 0; t < 2; t += 0.1) {
			pl_saver_draw(wide, &inner, t);
		}
		pl_saver_free(wide);
		bool kept = true;
		for (int y = 0; y < H + 16; y++) {
			for (int x = 0; x < W + 16; x++) {
				const bool in = x >= 8 && x < W + 8 && y >= 8 && y < H + 8;
				kept &= in || big[y * (W + 16) + x] == 0x55555555u;
			}
		}
		char what[96];
		snprintf(what, sizeof(what), "%s stays inside a smaller canvas", pl_saver_id(i));
		check(kept, what);
	}
	return fails ? 1 : 0;
}
