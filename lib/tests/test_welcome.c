/*
 * The startup screen (lib/welcome.c): the scale for each display, where the
 * box goes, and what it paints - the 1984 box's line, shadow, logo and
 * title - at every scale. With WELCOME_PNG=file set, also writes the box at
 * scale 2 for a look.
 */
#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>

#include "draw.h"
#include "text.h"
#include "welcome.h"

static int fails;

static void check(bool ok, const char *what) {
	printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

#define BG 0x00123456u /* nothing yet: where the pattern would show */

int main(void) {
	check(pl_welcome_scale(512, 342) == 1 && pl_welcome_scale(800, 600) == 1, "1984's size is scale 1");
	check(pl_welcome_scale(1280, 800) == 2 && pl_welcome_scale(1920, 1080) == 3 &&
		pl_welcome_scale(2560, 1440) == 4, "common displays get the largest whole scale");
	check(pl_welcome_scale(320, 200) == 1 && pl_welcome_scale(7680, 4320) == PL_WELCOME_MAX_SCALE,
		"tiny and huge displays stay within 1..PL_WELCOME_MAX_SCALE");

	int x, y;
	pl_welcome_box_origin(512, 342, &x, &y);
	check(x == 32 && y == 64, "on the 1984 screen the box is where it was");
	pl_welcome_box_origin(1920, 1080, &x, &y);
	check(x == 192 + 96 && y == 27 + 192, "on 1920x1080 it is the 1984 screen tripled, centred");
	pl_happy_zac_origin(1920, 1080, &x, &y);
	check(x == (1920 - 96) / 2 && y == (1080 - 96) / 2, "Happy Zac is centred at its scale");

	bool art = true;
	for (int s = 1; s <= PL_WELCOME_MAX_SCALE; s++) {
		art &= pl_boot_art(PL_ART_HAPPY_ZAC, s) && pl_boot_art(PL_ART_LOGO, s);
	}
	check(art, "both pictures load at every scale");
	check(!pl_boot_art(PL_ART_LOGO, 0) && !pl_boot_art(PL_ART_LOGO, PL_WELCOME_MAX_SCALE + 1),
		"there are no pictures outside those scales");

	struct plat_text *title = text_render_font(PL_WELCOME_TITLE, 1000, PL_FONT_SYSTEM);
	for (int s = 1; s <= 3; s++) {
		const int W = (PL_WELCOME_BOX_W + PL_WELCOME_SHADOW + 4) * s;
		const int H = (PL_WELCOME_BOX_H + PL_WELCOME_SHADOW + 4) * s;
		uint32_t *px = malloc(sizeof(uint32_t) * W * H);
		for (int i = 0; i < W * H; i++) {
			px[i] = BG;
		}
		struct pl_canvas c = { .px = px, .stride = W, .width = W, .height = H };
		const int bx = 2 * s, by = 2 * s;
		pl_welcome_box_paint(&c, bx, by, s, title);
		const int x1 = bx + PL_WELCOME_BOX_W * s - 1, y1 = by + PL_WELCOME_BOX_H * s - 1;
		const int d = PL_WELCOME_SHADOW * s;
		char what[96];

		bool line = true;
		for (int k = 0; k < s; k++) {
			line &= px[(by + k) * W + bx + 40 * s] == C_BLACK && px[(y1 - k) * W + bx + 40 * s] == C_BLACK;
			line &= px[(by + 40 * s) * W + bx + k] == C_BLACK && px[(by + 40 * s) * W + x1 - k] == C_BLACK;
		}
		line &= px[(by + s) * W + bx + s] == C_WHITE && px[(y1 - s) * W + x1 - s] == C_WHITE;
		snprintf(what, sizeof(what), "scale %d: a %d px black line round white", s, s);
		check(line, what);

		bool shadow = px[(y1 + d) * W + x1 + d] == C_BLACK && px[(y1 + 1) * W + bx + d] == C_BLACK;
		shadow &= px[(by + d - 1) * W + x1 + 1] == BG && px[(y1 + 1) * W + bx + d - 1] == BG;
		shadow &= px[(y1 + d + 1) * W + x1] == BG && px[(by + 40 * s) * W + x1 + d + 1] == BG;
		snprintf(what, sizeof(what), "scale %d: a solid shadow %d px right and below, nothing else", s, d);
		check(shadow, what);

		int colour = 0;
		for (int py = by + PL_WELCOME_ICON_Y * s; py < by + (PL_WELCOME_ICON_Y + PL_WELCOME_ICON) * s; py++) {
			for (int qx = bx + PL_WELCOME_ICON_X * s; qx < bx + (PL_WELCOME_ICON_X + PL_WELCOME_ICON) * s; qx++) {
				const uint32_t v = px[py * W + qx];
				const int r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
				colour += abs(r - g) > 40 || abs(g - b) > 40;
			}
		}
		snprintf(what, sizeof(what), "scale %d: the logo, in colour, at its corner", s);
		check(colour > 20 * s * s, what);

		int l = W, r = -1, t = H, b = -1;
		for (int py = by + s; py < by + 70 * s; py++) {
			for (int qx = bx + 80 * s; qx < x1 - s; qx++) {
				if (px[py * W + qx] == C_BLACK) {
					l = qx < l ? qx : l;
					r = qx > r ? qx : r;
					t = py < t ? py : t;
					b = py > b ? py : b;
				}
			}
		}
		const int mid = (l + r + 1) / 2 - bx;
		snprintf(what, sizeof(what), "scale %d: the title, centred on its mark, on its baseline", s);
		check(r > l && abs(mid - PL_WELCOME_TITLE_X * s) <= s && b == by + (PL_WELCOME_BASELINE + 1) * s - 1 &&
			t < b - 6 * s, what);

		const char *png = getenv("WELCOME_PNG");
		if (png && s == 2) {
			cairo_surface_t *surface = cairo_image_surface_create_for_data(
				(unsigned char *)px, CAIRO_FORMAT_RGB24, W, H, W * 4);
			cairo_surface_write_to_png(surface, png);
			cairo_surface_destroy(surface);
		}
		free(px);
	}
	text_destroy(title);
	if (fails) {
		printf("%d failed\n", fails);
		return 1;
	}
	printf("all passed\n");
	return 0;
}
