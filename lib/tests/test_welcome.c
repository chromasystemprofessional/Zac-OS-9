/*
 * The Welcome screen's illustration: it draws something (not the blank it
 * would be if the cairo calls broke), stays inside its own box, and scales.
 * With WELCOME_PNG=file set, also writes it (on the Welcome box's grey, at
 * 3x) for a look.
 */
#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>

#include "draw.h"
#include "welcome.h"

static int fails;

static void check(bool ok, const char *what) {
	printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

static int differing(const uint32_t *px, int n, uint32_t bg) {
	int d = 0;
	for (int i = 0; i < n; i++) {
		d += px[i] != bg;
	}
	return d;
}

int main(void) {
	const int W = PL_WELCOME_ART_W + 20, H = PL_WELCOME_ART_H + 20;
	uint32_t *px = malloc(sizeof(uint32_t) * W * H);
	for (int i = 0; i < W * H; i++) {
		px[i] = GRAY(0xD);
	}
	struct pl_canvas c = { .px = px, .stride = W, .width = W, .height = H };
	pl_welcome_art(&c, 10, 10, 1.0);

	check(differing(px, W * H, GRAY(0xD)) > W * H / 3, "it paints a good part of its box");
	/* The margin is untouched: nothing is drawn outside the design size. */
	bool margin = true;
	for (int x = 0; x < W; x++) {
		margin &= px[x] == GRAY(0xD) && px[(H - 1) * W + x] == GRAY(0xD);
	}
	for (int y = 0; y < H; y++) {
		margin &= px[y * W] == GRAY(0xD) && px[y * W + W - 1] == GRAY(0xD);
	}
	check(margin, "it stays inside its design size");
	/* The screen's lavender is bluer than red: the desktop is in there. */
	const uint32_t screen = px[(10 + 80) * W + 10 + 30];
	check((screen & 0xFF) > ((screen >> 16) & 0xFF) + 40, "the display shows the lavender desktop");

	/* Twice the size: twice the box, and still nothing outside it. */
	const int W2 = 2 * PL_WELCOME_ART_W, H2 = 2 * PL_WELCOME_ART_H;
	uint32_t *big = malloc(sizeof(uint32_t) * W2 * H2);
	for (int i = 0; i < W2 * H2; i++) {
		big[i] = GRAY(0xD);
	}
	struct pl_canvas c2 = { .px = big, .stride = W2, .width = W2, .height = H2 };
	pl_welcome_art(&c2, 0, 0, 2.0);
	check(differing(big, W2 * H2, GRAY(0xD)) > 3 * differing(px, W * H, GRAY(0xD)),
		"scaled up it covers about four times the pixels");

	const char *png = getenv("WELCOME_PNG");
	if (png) {
		const int s = 3, PW = W * s, PH = H * s;
		uint32_t *look = malloc(sizeof(uint32_t) * PW * PH);
		for (int i = 0; i < PW * PH; i++) {
			look[i] = GRAY(0xD);
		}
		struct pl_canvas c3 = { .px = look, .stride = PW, .width = PW, .height = PH };
		pl_welcome_art(&c3, 10 * s, 10 * s, s);
		cairo_surface_t *surface = cairo_image_surface_create_for_data(
			(unsigned char *)look, CAIRO_FORMAT_ARGB32, PW, PH, PW * 4);
		cairo_surface_write_to_png(surface, png);
		cairo_surface_destroy(surface);
		free(look);
	}
	free(px);
	free(big);
	if (fails) {
		printf("%d failed\n", fails);
		return 1;
	}
	printf("all passed\n");
	return 0;
}
