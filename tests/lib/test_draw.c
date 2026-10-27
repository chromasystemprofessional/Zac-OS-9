/*
 * pl_image_blend: alpha compositing for a resolved application icon
 * (real alpha, not the 1990s pixel art's on/off alpha).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "draw.h"
#include "text.h"

static int fails;

static void check(bool ok, const char *what) {
	printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

static struct pl_canvas make(uint32_t *px, int w, int h) {
	return (struct pl_canvas){ .px = px, .stride = w, .width = w, .height = h };
}

int main(void) {
	uint32_t bg[4] = { GRAY(0x0), GRAY(0x0), GRAY(0x0), GRAY(0x0) }; /* black, 2x2 */
	struct pl_canvas c = make(bg, 2, 2);

	uint32_t img[4] = {
		0x00FFFFFF, /* alpha 0: no-op */
		0xFFFFFFFF, /* alpha 255: overwrites exactly */
		0x80FFFFFF, /* alpha 128: roughly half white over black */
		0x80FFFFFF,
	};
	pl_image_blend(&c, 0, 0, img, 2, 2, false);
	check(bg[0] == GRAY(0x0), "alpha 0 leaves the background alone");
	check(bg[1] == 0xFFFFFFFFu, "alpha 255 writes the pixel exactly");
	const unsigned r = (bg[2] >> 16) & 0xFF;
	check(r > 90 && r < 160, "alpha 128 blends roughly halfway");

	/* Selected: the same darkening pl_icon_paint_label applies. */
	uint32_t bg2[1] = { GRAY(0x0) };
	struct pl_canvas c2 = make(bg2, 1, 1);
	uint32_t white[1] = { 0xFFFFFFFFu };
	pl_image_blend(&c2, 0, 0, white, 1, 1, true);
	check(bg2[0] == (0xFF000000u | ((0xFFFFFFu >> 1) & 0x7F7F7F)),
		"a selected icon is darkened the same way the icon set is");

	/* Clipping: a pixel outside the canvas is left alone, not written
	 * past the buffer. */
	uint32_t bg3[1] = { GRAY(0x0) };
	struct pl_canvas c3 = make(bg3, 1, 1);
	uint32_t offscreen[1] = { 0xFFFFFFFFu };
	pl_image_blend(&c3, 5, 5, offscreen, 1, 1, false);
	check(bg3[0] == GRAY(0x0), "a pixel outside the canvas is clipped, not written");

	/* Fractional scales: every 1x pixel one or two whole screen pixels,
	 * the same pattern everywhere, by wlroots' own rounding. */
	for (int q = 5; q <= 8; q++) {
		bool whole = true, inverse = true, rounding = true, periodic = true;
		const int period = q == 6 ? 2 : 4;
		for (int u = -40; u < 40; u++) {
			const int n = pl_snap(u + 1, q) - pl_snap(u, q);
			whole = whole && n >= 1 && n <= 2;
			for (int d = pl_snap(u, q); d < pl_snap(u + 1, q); d++) {
				inverse = inverse && pl_unsnap(d, q) == u;
			}
			if (u >= 0) {
				const double exact = u * q / 4.0;
				rounding = rounding && pl_snap(u, q) == (int)(exact + 0.5);
			}
			periodic = periodic && pl_snap(u + period, q) == pl_snap(u, q) + period * q / 4;
		}
		char what[96];
		snprintf(what, sizeof(what), "at %d/4: each 1x pixel 1-2 screen pixels, exactly inverted, "
			"periodic, rounded as wlroots does", q);
		check(whole && inverse && rounding && periodic, what);
	}
	/* 1.5: a 2x2 checkerboard at an odd place comes out in whole pixels. */
	const uint32_t board[4] = { C_BLACK, C_WHITE, C_WHITE, C_BLACK };
	const int dw = pl_snap(3 + 2, 6) - pl_snap(3, 6), dh = pl_snap(1 + 2, 6) - pl_snap(1, 6);
	uint32_t big[16] = { 0 };
	check(dw == 3 && dh == 3, "a 2x2 image at (3, 1), at 1.5: 3x3 screen pixels");
	pl_snap_scale(board, 2, 2, 3, 1, 6, big);
	/* pl_snap(3)=5 (4.5 up), pl_snap(4)=6: column 3 is one pixel, 4 two;
	 * likewise row 1 one, row 2 two. */
	check(big[0] == C_BLACK && big[1] == C_WHITE && big[2] == C_WHITE &&
		big[3] == C_WHITE && big[4] == C_BLACK && big[5] == C_BLACK &&
		big[6] == C_WHITE && big[7] == C_BLACK && big[8] == C_BLACK,
		"each screen pixel shows its own 1x pixel");

	/* Text on a canvas with a log, at 1.5x: the picture without its 1x
	 * letters, enlarged, and the text in the 1.5x font where its pen and
	 * baseline fall; nothing else. */
	{
		enum { W = 60, H = 20, Q = 6 };
		uint32_t buf[W * H];
		struct pl_canvas c = make(buf, W, H);
		c.log = pl_text_log_new(buf, W);
		pl_fill(&c, 0, 0, W - 1, H - 1, GRAY(0xD));
		struct plat_text *t = text_render("Hi", 100);
		pl_text(&c, t, 5, 14, C_BLACK);
		const int dw = pl_snap(W, Q), dh = pl_snap(H, Q);
		uint32_t *got = calloc((size_t)dw * dh, 4), *want = calloc((size_t)dw * dh, 4);
		pl_snap_scale_canvas(&c, 0, 0, Q, got);
		for (int i = 0; i < dw * dh; i++) {
			want[i] = GRAY(0xD);
		}
		struct plat_text *big = text_render_scaled(t, Q);
		const int pen = 5 - t->ink_l + 1;
		const int ox = pl_snap(pen, Q) - 1, oy = pl_snap(14, Q) - big->baseline;
		for (int y = 0; y < big->height; y++) {
			for (int x = 0; x < big->width; x++) {
				if (big->mask[y * big->stride + x] && ox + x < dw && oy + y < dh) {
					want[(oy + y) * dw + ox + x] = C_BLACK;
				}
			}
		}
		check(memcmp(got, want, (size_t)dw * dh * 4) == 0,
			"logged text at 1.5x: the background enlarged, the text in the 1.5x font");
		/* Painted over since: the cover stays. */
		pl_fill(&c, 0, 0, 7, H - 1, RGB(0xFF, 0, 0));
		pl_snap_scale_canvas(&c, 0, 0, Q, got);
		check(got[(pl_snap(10, Q)) * dw + 1] == RGB(0xFF, 0, 0),
			"what was painted over a letter since is kept");
		text_destroy(big);
		text_destroy(t);
		free(got);
		free(want);
		pl_text_log_free(c.log);
	}

	if (fails) {
		printf("%d failed\n", fails);
	} else {
		printf("all passed\n");
	}
	return fails ? 1 : 0;
}
