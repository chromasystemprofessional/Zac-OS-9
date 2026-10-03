/*
 * pl_image_blend: alpha compositing for a resolved application icon
 * (real alpha, not the 1990s pixel art's on/off alpha).
 */
#include <stdio.h>
#include <stdlib.h>

#include "draw.h"

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

	if (fails) {
		printf("%d failed\n", fails);
	} else {
		printf("all passed\n");
	}
	return fails ? 1 : 0;
}
