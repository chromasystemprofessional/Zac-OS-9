/*
 * Theme image skins (the bundle's chrome PNG files): tiled over
 * window frames (compositor/src/decor.c), the menu bar and menus
 * (lib/menudraw.c). Missing files keep the Platinum drawing.
 */
#include <cairo.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "chrome.h"
#include "draw.h"
#include "theme.h"

#define CHROME_SLOTS 16

static char *chrome_bundle; /* theme.conf path the skins below were read from */
static struct timespec chrome_mtime;
static off_t chrome_size;
static struct chrome_skin {
	char *path;          /* bundle-relative file, owned */
	uint32_t *px;
	int w, h;
	bool tried;
} chrome_cache[CHROME_SLOTS];
static int chrome_used;

static void chrome_clear(void) {
	free(chrome_bundle);
	chrome_bundle = NULL;
	chrome_mtime = (struct timespec){ 0 };
	chrome_size = -1;
	for (int i = 0; i < chrome_used; i++) {
		free(chrome_cache[i].path);
		free(chrome_cache[i].px);
		chrome_cache[i] = (struct chrome_skin){ 0 };
	}
	chrome_used = 0;
}

/* The active theme changed when its theme.conf path, time or size did;
 * drop the skins. The time matters for the Theme Editor: turning a linked
 * copy's artwork off rewrites the same theme.conf while it is active. */
static void chrome_maybe_reload(void) {
	char conf[4096];
	if (!zac_theme_asset("theme.conf", conf, sizeof(conf))) {
		if (chrome_bundle) {
			chrome_clear();
		}
		return;
	}
	struct stat st;
	if (stat(conf, &st) != 0) {
		chrome_clear();
		return;
	}
	if (chrome_bundle && strcmp(chrome_bundle, conf) == 0 &&
			chrome_mtime.tv_sec == st.st_mtim.tv_sec &&
			chrome_mtime.tv_nsec == st.st_mtim.tv_nsec &&
			chrome_size == st.st_size) {
		return;
	}
	chrome_clear();
	chrome_bundle = strdup(conf);
	chrome_mtime = st.st_mtim;
	chrome_size = st.st_size;
}

static struct chrome_skin *chrome_find(const char *path) {
	for (int i = 0; i < chrome_used; i++) {
		if (strcmp(chrome_cache[i].path, path) == 0) {
			return &chrome_cache[i];
		}
	}
	if (chrome_used == CHROME_SLOTS) {
		return NULL;
	}
	chrome_cache[chrome_used].path = strdup(path);
	return &chrome_cache[chrome_used++];
}

const uint32_t *chrome_file(const char *path, int *w, int *h) {
	chrome_maybe_reload();
	if (!chrome_bundle) {
		return NULL;
	}
	struct chrome_skin *s = chrome_find(path);
	if (!s || s->tried) {
		goto out;
	}
	s->tried = true;
	{
		char full[4096];
		if (!zac_theme_asset(path, full, sizeof(full))) {
			goto out;
		}
		cairo_surface_t *img = cairo_image_surface_create_from_png(full);
		if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
			cairo_surface_destroy(img);
			goto out;
		}
		int sw = cairo_image_surface_get_width(img);
		int sh = cairo_image_surface_get_height(img);
		if (sw > 0 && sh > 0) {
			cairo_surface_flush(img);
			const uint32_t *in = (const uint32_t *)cairo_image_surface_get_data(img);
			int stride = cairo_image_surface_get_stride(img) / 4;
			uint32_t *px = malloc(sizeof(uint32_t) * sw * sh);
			for (int y = 0; y < sh; y++) {
				for (int x = 0; x < sw; x++) {
					uint32_t p = in[y * stride + x];
					unsigned a = p >> 24;
					if (a != 0 && a != 255) {
						/* Cairo stores premultiplied ARGB; the
						 * compositor's blitters expect straight. */
						unsigned r = ((p >> 16) & 0xFF) * 255 / a;
						unsigned g = ((p >> 8) & 0xFF) * 255 / a;
						unsigned b = (p & 0xFF) * 255 / a;
						p = a << 24 | (r > 255 ? 255 : r) << 16 |
							(g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b);
					}
					px[y * sw + x] = p;
				}
			}
			s->px = px;
			s->w = sw;
			s->h = sh;
		}
		cairo_surface_destroy(img);
	}
out:
	if (!s || !s->px) {
		return NULL;
	}
	*w = s->w;
	*h = s->h;
	return s->px;
}

/* Write one straight-ARGB pixel over the canvas, blending its alpha. */
static void tile_put(struct pl_canvas *c, int x, int y, uint32_t p) {
	const unsigned a = p >> 24;
	if (a == 0) {
		return;
	}
	if (a == 255) {
		pl_put(c, x, y, p);
		return;
	}
	if (x < c->x || x >= c->x + c->width || y < c->y || y >= c->y + c->height) {
		return;
	}
	uint32_t *dst = &c->px[(size_t)(y - c->y) * c->stride + (x - c->x)];
	const uint32_t bg = *dst;
	const unsigned r = (((p >> 16) & 0xFF) * a + ((bg >> 16) & 0xFF) * (255 - a)) / 255;
	const unsigned g = (((p >> 8) & 0xFF) * a + ((bg >> 8) & 0xFF) * (255 - a)) / 255;
	const unsigned b = ((p & 0xFF) * a + (bg & 0xFF) * (255 - a)) / 255;
	*dst = 0xFF000000u | r << 16 | g << 8 | b;
}

/* Tile a skin over a rectangle, wrapping in both directions. */
void chrome_tile(struct pl_canvas *c, const uint32_t *px, int sw, int sh,
		int x0, int y0, int x1, int y1) {
	for (int y = y0; y <= y1; y++) {
		for (int x = x0; x <= x1; x++) {
			tile_put(c, x, y, px[((y - y0) % sh) * sw + ((x - x0) % sw)]);
		}
	}
}

/* Fill a rectangle with a skin when the theme has one, else a flat colour. */
void chrome_fill(struct pl_canvas *c, int x0, int y0, int x1, int y1,
		const char *path, uint32_t fallback) {
	pl_fill(c, x0, y0, x1, y1, fallback);
	int w, h;
	const uint32_t *px = chrome_file(path, &w, &h);
	if (px) {
		chrome_tile(c, px, w, h, x0, y0, x1, y1);
	}
}
