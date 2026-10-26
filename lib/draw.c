#include "draw.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* A text drawn on a canvas with a log: what, where, and what it covered. */
struct pl_text_entry {
	char *utf8;
	int font;
	int pen_x, baseline_y; /* in the buffer's pixels */
	int clip_x, clip_y, clip_w, clip_h; /* the canvas it was drawn on, there */
	uint32_t color;
	size_t *where; /* covered pixels, as indexes into the buffer */
	uint32_t *was; /* and what they were */
	int n, cap;
};

struct pl_text_log {
	const uint32_t *base;
	int stride;
	struct pl_text_entry *entries;
	int n, cap;
};

struct pl_text_log *pl_text_log_new(const uint32_t *base, int stride) {
	struct pl_text_log *log = calloc(1, sizeof(*log));
	if (log) {
		log->base = base;
		log->stride = stride;
	}
	return log;
}

void pl_text_log_free(struct pl_text_log *log) {
	if (!log) {
		return;
	}
	for (int i = 0; i < log->n; i++) {
		free(log->entries[i].utf8);
		free(log->entries[i].where);
		free(log->entries[i].was);
	}
	free(log->entries);
	free(log);
}

/* A new entry for text drawn on c with its mask's top left at (ox, oy). */
static struct pl_text_entry *log_text(struct pl_canvas *c, const struct plat_text *t, int ox, int oy,
		uint32_t color) {
	struct pl_text_log *log = c->log;
	if (!t->utf8 || c->stride != log->stride || c->px < log->base) {
		return NULL;
	}
	if (log->n == log->cap) {
		const int cap = log->cap ? log->cap * 2 : 8;
		struct pl_text_entry *grown = realloc(log->entries, sizeof(*grown) * cap);
		if (!grown) {
			return NULL;
		}
		log->entries = grown;
		log->cap = cap;
	}
	/* The canvas's own place in the buffer. */
	const ptrdiff_t at = c->px - log->base;
	const int bx = (int)(at % log->stride) - c->x, by = (int)(at / log->stride) - c->y;
	struct pl_text_entry *e = &log->entries[log->n];
	*e = (struct pl_text_entry){
		.utf8 = strdup(t->utf8), .font = t->font,
		/* render_exact's pen starts at the mask's column 1. */
		.pen_x = bx + ox + 1, .baseline_y = by + oy + t->baseline, .color = color,
		.clip_x = bx + c->x, .clip_y = by + c->y, .clip_w = c->width, .clip_h = c->height,
	};
	if (!e->utf8) {
		return NULL;
	}
	log->n++;
	return e;
}

static void log_pixel(struct pl_text_log *log, struct pl_text_entry *e, const uint32_t *p) {
	if (e->n == e->cap) {
		const int cap = e->cap ? e->cap * 2 : 64;
		size_t *where = realloc(e->where, sizeof(*where) * cap);
		if (where) {
			e->where = where;
		}
		uint32_t *was = realloc(e->was, sizeof(*was) * cap);
		if (was) {
			e->was = was;
		}
		if (!where || !was) {
			return;
		}
		e->cap = cap;
	}
	e->where[e->n] = (size_t)(p - log->base);
	e->was[e->n] = *p;
	e->n++;
}

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
	struct pl_text_entry *e = c->log ? log_text(c, t, ox, oy, color) : NULL;
	for (int y = 0; y < t->height; y++) {
		for (int x = 0; x < t->width; x++) {
			if (t->mask[y * t->stride + x]) {
				const int px = ox + x, py = oy + y;
				if (e && px >= c->x && py >= c->y && px < c->x + c->width && py < c->y + c->height) {
					log_pixel(c->log, e, &c->px[(py - c->y) * c->stride + (px - c->x)]);
				}
				pl_put(c, px, py, color);
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

/* floor(a / b) for b > 0. */
static int floor_div(long long a, int b) {
	return (int)(a >= 0 ? a / b : -((-a + b - 1) / b));
}

int pl_snap(int u, int quarters) {
	/* floor(u * s + 0.5) */
	return floor_div((long long)u * quarters + 2, 4);
}

int pl_unsnap(int d, int quarters) {
	/* The last u with pl_snap(u) <= d: ceil((d + 0.5) / s) - 1. */
	return -floor_div(-(4LL * d + 2), quarters) - 1;
}

void pl_snap_scale(const uint32_t *src, int w, int h, int ux, int uy, int quarters,
		uint32_t *dst) {
	const int dx0 = pl_snap(ux, quarters), dw = pl_snap(ux + w, quarters) - dx0;
	const int dy0 = pl_snap(uy, quarters), dh = pl_snap(uy + h, quarters) - dy0;
	for (int j = 0; j < dh; j++) {
		const int y = pl_unsnap(dy0 + j, quarters) - uy;
		const uint32_t *row = src + (size_t)(y < 0 ? 0 : y >= h ? h - 1 : y) * w;
		uint32_t *out = dst + (size_t)j * dw;
		for (int i = 0, x = 0; i < dw; i++) {
			/* Columns advance in step; no division per pixel. */
			while (x + 1 < w && pl_snap(ux + x + 1, quarters) <= dx0 + i) {
				x++;
			}
			out[i] = row[x];
		}
	}
}

void pl_snap_scale_canvas(const struct pl_canvas *c, int ux, int uy, int quarters, uint32_t *dst) {
	const struct pl_text_log *log = c->log;
	const int w = c->width, h = c->height;
	/* ZACOS9_SCALED_FONTS=0: the 1x letters enlarged, as before (for
	 * comparing pictures with the 1x ones: tests). */
	const char *fonts = getenv("ZACOS9_SCALED_FONTS");
	const bool scaled_fonts = !fonts || strcmp(fonts, "0") != 0;
	uint32_t *src = scaled_fonts && log && log->n && quarters % 4 && log->base == c->px && c->stride == w ?
		malloc((size_t)w * h * sizeof(*src)) : NULL;
	if (!src) {
		pl_snap_scale(c->px, w, h, ux, uy, quarters, dst);
		return;
	}
	/* The picture without its 1x letters, newest first. */
	memcpy(src, c->px, (size_t)w * h * sizeof(*src));
	for (int i = log->n - 1; i >= 0; i--) {
		const struct pl_text_entry *e = &log->entries[i];
		for (int k = e->n - 1; k >= 0; k--) {
			if (e->where[k] < (size_t)w * h && src[e->where[k]] == e->color) {
				src[e->where[k]] = e->was[k];
			}
		}
	}
	pl_snap_scale(src, w, h, ux, uy, quarters, dst);
	free(src);
	/* Then each text in the screen's own font, oldest first. */
	const int dx0 = pl_snap(ux, quarters), dy0 = pl_snap(uy, quarters);
	const int dw = pl_snap(ux + w, quarters) - dx0, dh = pl_snap(uy + h, quarters) - dy0;
	for (int i = 0; i < log->n; i++) {
		const struct pl_text_entry *e = &log->entries[i];
		const struct plat_text one = { .utf8 = e->utf8, .font = e->font };
		struct plat_text *t = text_render_scaled(&one, quarters);
		if (!t) {
			continue;
		}
		const int ox = pl_snap(ux + e->pen_x, quarters) - dx0 - 1;
		const int oy = pl_snap(uy + e->baseline_y, quarters) - dy0 - t->baseline;
		/* Clipped as on its canvas (a list's rows under its header). */
		const int cx0 = pl_snap(ux + e->clip_x, quarters) - dx0;
		const int cy0 = pl_snap(uy + e->clip_y, quarters) - dy0;
		const int cx1 = pl_snap(ux + e->clip_x + e->clip_w, quarters) - dx0;
		const int cy1 = pl_snap(uy + e->clip_y + e->clip_h, quarters) - dy0;
		for (int y = 0; y < t->height; y++) {
			for (int x = 0; x < t->width; x++) {
				const int px = ox + x, py = oy + y;
				if (t->mask[y * t->stride + x] && px >= 0 && py >= 0 && px < dw && py < dh &&
						px >= cx0 && py >= cy0 && px < cx1 && py < cy1) {
					dst[(size_t)py * dw + px] = e->color;
				}
			}
		}
		text_destroy(t);
	}
}
