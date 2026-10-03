#include "draw.h"

void pl_put(struct pl_canvas *c, int x, int y, uint32_t color) {
	if (x < c->x || y < c->y || x >= c->x + c->width || y >= c->y + c->height) {
		return;
	}
	c->px[(y - c->y) * c->stride + (x - c->x)] = color;
}

void pl_hline(struct pl_canvas *c, int x0, int x1, int y, uint32_t color) {
	for (int x = x0; x <= x1; x++) {
		pl_put(c, x, y, color);
	}
}

void pl_vline(struct pl_canvas *c, int x, int y0, int y1, uint32_t color) {
	for (int y = y0; y <= y1; y++) {
		pl_put(c, x, y, color);
	}
}

void pl_fill(struct pl_canvas *c, int x0, int y0, int x1, int y1, uint32_t color) {
	for (int y = y0; y <= y1; y++) {
		pl_hline(c, x0, x1, y, color);
	}
}

void pl_outline(struct pl_canvas *c, int x0, int y0, int x1, int y1, uint32_t color) {
	pl_hline(c, x0, x1, y0, color);
	pl_hline(c, x0, x1, y1, color);
	pl_vline(c, x0, y0, y1, color);
	pl_vline(c, x1, y0, y1, color);
}

void pl_text(struct pl_canvas *c, const struct plat_text *t, int ink_x,
		int baseline_y, uint32_t color) {
	if (!t || t->ink_l < 0) {
		return;
	}
	int ox = ink_x - t->ink_l, oy = baseline_y - t->baseline;
	for (int y = 0; y < t->height; y++) {
		for (int x = 0; x < t->width; x++) {
			if (t->mask[y * t->stride + x]) {
				pl_put(c, ox + x, oy + y, color);
			}
		}
	}
}

void pl_grays(struct pl_canvas *c, int x, int y, const char *const *rows, int nrows) {
	for (int j = 0; j < nrows; j++) {
		for (int i = 0; rows[j][i]; i++) {
			char ch = rows[j][i];
			if (ch == '.') {
				continue;
			}
			int v = ch <= '9' ? ch - '0' : ch - 'a' + 10;
			pl_put(c, x + i, y + j, GRAY(v));
		}
	}
}

void pl_image(struct pl_canvas *c, int x, int y, const uint32_t *px, int w, int h) {
	for (int j = 0; j < h; j++) {
		for (int i = 0; i < w; i++) {
			uint32_t p = px[j * w + i];
			if (p >> 24) {
				pl_put(c, x + i, y + j, p);
			}
		}
	}
}

void pl_image_blend(struct pl_canvas *c, int x, int y, const uint32_t *px, int w, int h,
		bool selected) {
	for (int j = 0; j < h; j++) {
		int dy = y + j;
		if (dy < c->y || dy >= c->y + c->height) {
			continue;
		}
		for (int i = 0; i < w; i++) {
			int dx = x + i;
			if (dx < c->x || dx >= c->x + c->width) {
				continue;
			}
			uint32_t p = px[j * w + i];
			unsigned a = p >> 24;
			if (!a) {
				continue;
			}
			uint32_t *dst = &c->px[(dy - c->y) * c->stride + (dx - c->x)];
			uint32_t out;
			if (a == 255) {
				out = p;
			} else {
				uint32_t bg = *dst;
				unsigned br = (bg >> 16) & 0xFF, bgc = (bg >> 8) & 0xFF, bb = bg & 0xFF;
				unsigned sr = (p >> 16) & 0xFF, sg = (p >> 8) & 0xFF, sb = p & 0xFF;
				unsigned r = (sr * a + br * (255 - a)) / 255;
				unsigned g = (sg * a + bgc * (255 - a)) / 255;
				unsigned b = (sb * a + bb * (255 - a)) / 255;
				out = 0xFF000000u | (r << 16) | (g << 8) | b;
			}
			if (selected) {
				out = 0xFF000000u | ((out >> 1) & 0x7F7F7F);
			}
			*dst = out;
		}
	}
}
