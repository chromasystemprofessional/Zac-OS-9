#include "decor.h"

#include <cairo.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "chrome.h"
#include "icons.h"
#include "theme.h"

#define C_222 GRAY(0x2)
#define C_555 GRAY(0x5)
#define C_666 GRAY(0x6)
#define C_777 GRAY(0x7)
#define C_888 GRAY(0x8)
#define C_999 GRAY(0x9)
#define C_AAA GRAY(0xA)
#define C_CCC GRAY(0xC)
#define C_DDD GRAY(0xD)

/* A window-chrome colour, overridable by the active theme's theme.conf
 * ("key=RRGGBB"); otherwise the Platinum default. */
static uint32_t chrome(const char *key, uint32_t fallback) {
	uint32_t v;
	return zac_theme_color(key, &v) ? v : fallback;
}

/* Inactive frames follow the theme's active colours instead of the fixed
 * Platinum greys, so a dark theme stays dark when its window loses focus.
 * A theme may name the inactive colour outright; without any theme colour
 * the Platinum fallback is used. */
static uint32_t inactive_face(uint32_t fallback) {
	uint32_t v;
	if (zac_theme_color("frame-face-inactive", &v)) {
		return v;
	}
	if (zac_theme_color("frame-face", &v)) {
		return zac_blend(v, C_WHITE, 8);
	}
	return fallback;
}

static uint32_t inactive_border(uint32_t fallback, uint32_t face) {
	uint32_t v;
	if (zac_theme_color("frame-border-inactive", &v)) {
		return v;
	}
	if (zac_theme_color("frame-border", &v)) {
		return zac_blend(v, face, 35);
	}
	return fallback;
}

/* The body-edge fallback is C_DDD in Platinum; a themed frame uses the
 * window's face colour (active or inactive) so a dark theme does not show
 * light edges. */
static uint32_t themed_or(uint32_t themed, uint32_t fallback) {
	uint32_t v;
	return zac_theme_color("frame-face", &v) ? themed : fallback;
}

/* Title ink: the theme's title-text, else readable ink for the bar. An
 * inactive title without its own colour is the active ink dimmed toward
 * the bar, so a dark theme keeps light text. */
static uint32_t title_ink(bool active, uint32_t base) {
	uint32_t v;
	if (active) {
		zac_theme_ink("title-text", base, &v);
		return v;
	}
	if (zac_theme_color("title-text-inactive", &v)) {
		return v;
	}
	if (zac_theme_color("title-text", &v)) {
		return zac_blend(v, base, 35);
	}
	/* Auto contrast, dimmed toward the bar for an inactive window. */
	const uint32_t ink = zac_ink_for(base);
	return ink == C_BLACK ? C_666 : zac_blend(ink, base, 35);
}

/* Window-frame skins: the bundle files per frame part, active and
 * inactive. An inactive window uses the -inactive skin when the theme ships
 * one, else its active skin; missing files keep the Platinum drawing. */
enum chrome_slot {
	CHROME_DOC_TOP, CHROME_DOC_BOTTOM, CHROME_DOC_LEFT, CHROME_DOC_RIGHT,
	CHROME_MODAL_TOP, CHROME_MODAL_BOTTOM, CHROME_MODAL_LEFT, CHROME_MODAL_RIGHT,
	CHROME_POPUP_TAB,
	CHROME_SLOTS
};

static const char *const chrome_files[CHROME_SLOTS][2] = {
	[CHROME_DOC_TOP] = { "chrome/document-top.png", "chrome/document-top-inactive.png" },
	[CHROME_DOC_BOTTOM] = { "chrome/document-bottom.png", "chrome/document-bottom-inactive.png" },
	[CHROME_DOC_LEFT] = { "chrome/document-left.png", "chrome/document-left-inactive.png" },
	[CHROME_DOC_RIGHT] = { "chrome/document-right.png", "chrome/document-right-inactive.png" },
	[CHROME_MODAL_TOP] = { "chrome/modal-top.png", NULL },
	[CHROME_MODAL_BOTTOM] = { "chrome/modal-bottom.png", NULL },
	[CHROME_MODAL_LEFT] = { "chrome/modal-left.png", NULL },
	[CHROME_MODAL_RIGHT] = { "chrome/modal-right.png", NULL },
	[CHROME_POPUP_TAB] = { "chrome/popup-tab.png", "chrome/popup-tab-inactive.png" },
};

/* Theme keys for those skins. Absent keeps the conventional file (and an
 * inherited bundle's), "off" paints the flat face, anything else is a
 * bundle-relative image. */
static const char *const chrome_skin_keys[CHROME_SLOTS][2] = {
	[CHROME_DOC_TOP] = { "document-top", "document-top-inactive" },
	[CHROME_DOC_BOTTOM] = { "document-bottom", "document-bottom-inactive" },
	[CHROME_DOC_LEFT] = { "document-left", "document-left-inactive" },
	[CHROME_DOC_RIGHT] = { "document-right", "document-right-inactive" },
	[CHROME_MODAL_TOP] = { "modal-top", NULL },
	[CHROME_MODAL_BOTTOM] = { "modal-bottom", NULL },
	[CHROME_MODAL_LEFT] = { "modal-left", NULL },
	[CHROME_MODAL_RIGHT] = { "modal-right", NULL },
	[CHROME_POPUP_TAB] = { "popup-tab", "popup-tab-inactive" },
};

static void chrome_fill_slot(struct pl_canvas *c, int x0, int y0, int x1, int y1,
		enum chrome_slot slot, bool active, uint32_t fallback) {
	const int variant = !active && chrome_files[slot][1] ? 1 : 0;
	char rel[256];
	if (chrome_skin_keys[slot][variant] &&
			zac_theme_string(chrome_skin_keys[slot][variant], rel, sizeof(rel))) {
		if (strcmp(rel, "off") == 0) {
			pl_fill(c, x0, y0, x1, y1, fallback);
			return;
		}
		chrome_fill(c, x0, y0, x1, y1, rel, fallback);
		return;
	}
	chrome_fill(c, x0, y0, x1, y1, chrome_files[slot][variant], fallback);
}

/* Title-bar boxes: 12x12 at y=4, 1 px emboss right and below. */
#define BOX_Y 4
#define BOX_SIZE 12
#define CLOSE_X 4
#define ZOOM_X_FROM_RIGHT 33
#define COLLAPSE_X_FROM_RIGHT 17
#define BOX_STRIPE_GAP 4 /* clear pixels between a box's emboss and stripes */
#define STRIPE_TEXT_GAP 4 /* clear pixels between stripes and title ink */
#define TITLE_BASELINE 14
/* The proxy icon: a small icon, then a gap, then the title; the two
 * centred together. TODO: measure against a Mac OS 9 Finder window. */
/* Title-bar art the extractor decodes from the theme's own resources
 * (theme.conf keys naming bundle-relative .argb tiles: u32le width and
 * height, then straight RGBA). Cached per resolved path; reloading when
 * the active theme changes. Missing art falls back to Platinum drawing. */
enum {
	CHROME_BAR_LEFT, CHROME_BAR_MIDDLE, CHROME_BAR_RIGHT,
	CHROME_BAR_LEFT_INACTIVE, CHROME_BAR_MIDDLE_INACTIVE, CHROME_BAR_RIGHT_INACTIVE,
	CHROME_BUTTON_CLOSE, CHROME_BUTTON_CLOSE_PRESSED,
	CHROME_BUTTON_ZOOM, CHROME_BUTTON_ZOOM_PRESSED,
	CHROME_BUTTON_GROW, CHROME_BUTTON_GROW_PRESSED,
	CHROME_BAR_SPAN, CHROME_BAR_SPAN_INACTIVE,
	CHROME_BAR_DIALOG_LEFT, CHROME_BAR_DIALOG_MIDDLE, CHROME_BAR_DIALOG_RIGHT,
	CHROME_BAR_DIALOG_LEFT_INACTIVE, CHROME_BAR_DIALOG_MIDDLE_INACTIVE, CHROME_BAR_DIALOG_RIGHT_INACTIVE,
	CHROME_BUTTON_DIALOG_CLOSE, CHROME_BUTTON_DIALOG_CLOSE_PRESSED,
	CHROME_BUTTON_DIALOG_ZOOM, CHROME_BUTTON_DIALOG_ZOOM_PRESSED,
	CHROME_BUTTON_DIALOG_GROW, CHROME_BUTTON_DIALOG_GROW_PRESSED,
	CHROME_COUNT
};

static const char *const chrome_keys[CHROME_COUNT] = {
	"bar-left", "bar-middle", "bar-right",
	"bar-left-inactive", "bar-middle-inactive", "bar-right-inactive",
	"button-close", "button-close-pressed",
	"button-zoom", "button-zoom-pressed",
	"button-grow", "button-grow-pressed",
	"bar-span", "bar-span-inactive",
	"bar-dialog-left", "bar-dialog-middle", "bar-dialog-right",
	"bar-dialog-left-inactive", "bar-dialog-middle-inactive", "bar-dialog-right-inactive",
	"button-dialog-close", "button-dialog-close-pressed",
	"button-dialog-zoom", "button-dialog-zoom-pressed",
	"button-dialog-grow", "button-dialog-grow-pressed",
};

struct chrome_tile {
	int w, h;
	uint32_t *px;
	char path[4096];
	struct timespec mtime;
	off_t size;
};

static struct chrome_tile chrome_cache[CHROME_COUNT];

static void chrome_tile_drop(int id) {
	free(chrome_cache[id].px);
	chrome_cache[id].px = NULL;
	chrome_cache[id].w = chrome_cache[id].h = 0;
	chrome_cache[id].path[0] = '\0';
	chrome_cache[id].mtime = (struct timespec){ 0 };
	chrome_cache[id].size = -1;
}

/* The tile for a chrome key, or NULL when the theme supplies none.
 * Inactive bar parts fall back to the active art. */
static const struct chrome_tile *chrome_tile_get(int id) {
	char rel[256];
	if (!zac_theme_string(chrome_keys[id], rel, sizeof(rel))) {
		chrome_tile_drop(id);
		return NULL;
	}
	char path[4096];
	if (!zac_theme_asset(rel, path, sizeof(path))) {
		chrome_tile_drop(id);
		return NULL;
	}
	struct chrome_tile *t = &chrome_cache[id];
	struct stat st;
	if (stat(path, &st) != 0) {
		chrome_tile_drop(id);
		return NULL;
	}
	if (t->px && strcmp(t->path, path) == 0 &&
			t->mtime.tv_sec == st.st_mtim.tv_sec &&
			t->mtime.tv_nsec == st.st_mtim.tv_nsec &&
			t->size == st.st_size) {
		return t;
	}
	chrome_tile_drop(id);
	FILE *f = fopen(path, "rb");
	if (!f) {
		return NULL;
	}
	uint32_t w = 0, h = 0;
	unsigned char head[8];
	if (fread(head, 1, sizeof(head), f) != sizeof(head)) {
		fclose(f);
		return NULL;
	}
	w = (uint32_t)head[0] | (uint32_t)head[1] << 8 | (uint32_t)head[2] << 16 | (uint32_t)head[3] << 24;
	h = (uint32_t)head[4] | (uint32_t)head[5] << 8 | (uint32_t)head[6] << 16 | (uint32_t)head[7] << 24;
	if (w == 0 || h == 0 || w > 4096 || h > 4096) {
		fclose(f);
		return NULL;
	}
	uint32_t *px = malloc(sizeof(uint32_t) * (size_t)w * h);
	if (!px) {
		fclose(f);
		return NULL;
	}
	for (uint32_t i = 0; i < w * h; i++) {
		unsigned char b[4];
		if (fread(b, 1, sizeof(b), f) != sizeof(b)) {
			free(px);
			fclose(f);
			return NULL;
		}
		px[i] = (uint32_t)b[3] << 24 | (uint32_t)b[0] << 16 | (uint32_t)b[1] << 8 | b[2];
	}
	fclose(f);
	t->px = px;
	t->w = (int)w;
	t->h = (int)h;
	snprintf(t->path, sizeof(t->path), "%s", path);
	t->mtime = st.st_mtim;
	t->size = st.st_size;
	return t;
}

/* Blit a tile rectangle, nearest-neighbour scaled to (dw, dh), clipped
 * to the canvas like pl_image_blend. */
static void chrome_blit(struct pl_canvas *c, const struct chrome_tile *t,
		int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh) {
	if (!t || !t->px || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) {
		return;
	}
	for (int y = 0; y < dh; y++) {
		int v = dy + y;
		if (v < c->y || v >= c->y + c->height) {
			continue;
		}
		int ty = sy + y * sh / dh;
		for (int x = 0; x < dw; x++) {
			int u = dx + x;
			if (u < c->x || u >= c->x + c->width) {
				continue;
			}
			int tx = sx + x * sw / dw;
			uint32_t p = t->px[(size_t)ty * t->w + tx];
			unsigned a = p >> 24;
			if (!a) {
				continue;
			}
			uint32_t *dst = &c->px[(size_t)(v - c->y) * c->stride + (u - c->x)];
			if (a == 255) {
				*dst = p;
			} else {
				uint32_t bg = *dst;
				unsigned r = ((((p >> 16) & 0xFF) * a + ((bg >> 16) & 0xFF) * (255 - a)) / 255);
				unsigned g = ((((p >> 8) & 0xFF) * a + ((bg >> 8) & 0xFF) * (255 - a)) / 255);
				unsigned b = (((p & 0xFF) * a + (bg & 0xFF) * (255 - a)) / 255);
				*dst = 0xFF000000u | r << 16 | g << 8 | b;
			}
		}
	}
}


#define PROXY_SIZE 16
#define PROXY_GAP 4
#define PROXY_Y 3
#define NO_BOX_STRIPE_X 5
#define TITLE_BAR_LAST_ROW 19

#define TITLE_BAR_H (TITLE_BAR_LAST_ROW + 1)

/* Whether the theme supplies a full bar set (inactive falls back to
 * the active art). */
static bool themed_bar_available(bool act) {
	int m = act ? CHROME_BAR_MIDDLE : CHROME_BAR_MIDDLE_INACTIVE;
	const struct chrome_tile *tm = chrome_tile_get(m);
	if (!tm) {
		tm = chrome_tile_get(CHROME_BAR_MIDDLE);
	}
	return tm != NULL;
}

/* The theme's full-width bar background (Kaleidoscope's Aqua blue):
 * inactive art falls back to the active span. */
static bool title_cluster_left(const struct decor_state *st);
static int title_group_end(const struct decor_state *st);
static bool themed_dialog_available(bool act);

static const struct chrome_tile *span_tile_get(bool act) {
	int m = act ? CHROME_BAR_SPAN : CHROME_BAR_SPAN_INACTIVE;
	const struct chrome_tile *tm = chrome_tile_get(m);
	if (!tm && !act) {
		tm = chrome_tile_get(CHROME_BAR_SPAN);
	}
	if (!tm && act) {
		tm = chrome_tile_get(CHROME_BAR_SPAN_INACTIVE);
	}
	return tm;
}

/* The window frame's top band below the title row (Kaleidoscope's
 * Aqua blue): the span tile's own height. Zero without a span. */
static int themed_band_h(bool act) {
	const struct chrome_tile *span = span_tile_get(act);
	if (!span) {
		span = span_tile_get(!act);
	}
	return span ? span->h : 0;
}

static int themed_band_max(void) {
	int a = themed_band_h(true);
	int b = themed_band_h(false);
	return a > b ? a : b;
}

/* The title bar with the theme's own art: end caps at each end and the
 * middle tiled horizontally, the band filling the title row (scaled when
 * the theme's bar has another height), and (left-clustered themes) the
 * span tiled full-width across the band below it. Returns false when the
 * theme supplies no bar. */
static bool paint_themed_bar(struct pl_canvas *c, const struct decor_state *st,
		int w, bool act) {
	const bool dialog = st->style == DECOR_STYLE_MOVABLE_MODAL;
	if (dialog ? !themed_dialog_available(act) : !themed_bar_available(act)) {
		return false;
	}
	int l = act ? CHROME_BAR_LEFT : CHROME_BAR_LEFT_INACTIVE;
	int m = act ? CHROME_BAR_MIDDLE : CHROME_BAR_MIDDLE_INACTIVE;
	int r = act ? CHROME_BAR_RIGHT : CHROME_BAR_RIGHT_INACTIVE;
	if (dialog) {
		l = act ? CHROME_BAR_DIALOG_LEFT : CHROME_BAR_DIALOG_LEFT_INACTIVE;
		m = act ? CHROME_BAR_DIALOG_MIDDLE : CHROME_BAR_DIALOG_MIDDLE_INACTIVE;
		r = act ? CHROME_BAR_DIALOG_RIGHT : CHROME_BAR_DIALOG_RIGHT_INACTIVE;
	}
	const struct chrome_tile *tl = chrome_tile_get(l);
	const struct chrome_tile *tm = chrome_tile_get(m);
	const struct chrome_tile *tr = chrome_tile_get(r);
	if (dialog) {
		if (!tl) {
			tl = chrome_tile_get(act ? CHROME_BAR_LEFT : CHROME_BAR_LEFT_INACTIVE);
		}
		if (!tm) {
			tm = chrome_tile_get(act ? CHROME_BAR_MIDDLE : CHROME_BAR_MIDDLE_INACTIVE);
		}
		if (!tr) {
			tr = chrome_tile_get(act ? CHROME_BAR_RIGHT : CHROME_BAR_RIGHT_INACTIVE);
		}
	}
	if (!tl) {
		tl = chrome_tile_get(CHROME_BAR_LEFT);
	}
	if (!tm) {
		tm = chrome_tile_get(CHROME_BAR_MIDDLE);
	}
	if (!tr) {
		tr = chrome_tile_get(CHROME_BAR_RIGHT);
	}
	if (!tm) {
		return false;
	}
	int lw = tl ? tl->w : 0;
	int rw = tr ? tr->w : 0;
	const struct chrome_tile *span = span_tile_get(act);
	if (span && !dialog && title_cluster_left(st)) {
		/* The title group sits in a raised pinstripe tab at the top-left
		 * (end caps hugging the cluster, transparent to the right of the
		 * tab above the 7 px window rim), and the blue band spans the
		 * window below the title row inside the frame borders. */
		int patchW = title_group_end(st) + (rw >= 12 ? 3 : rw);
		if (patchW < lw + rw) {
			patchW = lw + rw;
		}
		if (patchW > w) {
			patchW = w;
		}
		if (lw > 0) {
			chrome_blit(c, tl, 0, 0, tl->w, tl->h, 0, 0, lw, DECOR_TOP);
		}
		for (int x = lw; x < patchW - rw; x += tm->w) {
			int tilew = patchW - rw - x < tm->w ? patchW - rw - x : tm->w;
			chrome_blit(c, tm, 0, 0, tilew, tm->h, x, 0, tilew, DECOR_TOP);
		}
		if (rw > 0) {
			chrome_blit(c, tr, 0, 0, tr->w, tr->h, patchW - rw, 0, rw, DECOR_TOP);
		}
		if (patchW < w) {
			const uint32_t rim_border = act ? 0xFF525252u : 0xFF737373u;
			const uint32_t rim_hi = act ? 0xFFF6F6F6u : 0xFFDEDEDEu;
			const uint32_t rim_fill = act ? 0xFFDADADAu : 0xFFDEDEDEu;
			pl_hline(c, patchW, w - 2, DECOR_TOP - 7, rim_border);
			pl_hline(c, patchW, w - 3, DECOR_TOP - 6, rim_hi);
			pl_fill(c, patchW, DECOR_TOP - 5, w - 2, DECOR_TOP - 1, rim_fill);
			pl_vline(c, w - 1, DECOR_TOP - 7, DECOR_TOP - 1, rim_border);
		}
		if (st->collapsed) {
			pl_hline(c, 0, w - 1, DECOR_TOP - 1, act ? 0xFF525252u : 0xFF737373u);
			return true;
		}
		const int bandH = span->h;
		const int bx0 = DECOR_LEFT, bx1 = w - DECOR_RIGHT;
		for (int x = bx0; x < bx1; x += span->w) {
			int spanw = bx1 - x < span->w ? bx1 - x : span->w;
			chrome_blit(c, span, 0, 0, spanw, span->h < bandH ? span->h : bandH,
				x, DECOR_TOP, spanw, bandH);
		}
		return true;
	}
	/* The bar band fills the title row: the extractor writes every tile
	 * at the bar's own height, so a taller or shorter theme bar is scaled
	 * rather than cropped. */
	if (lw > 0) {
		chrome_blit(c, tl, 0, 0, tl->w, tl->h, 0, 0, lw, DECOR_TOP);
	}
	for (int x = lw; x < w - rw; x += tm->w) {
		int spanw = w - rw - x < tm->w ? w - rw - x : tm->w;
		chrome_blit(c, tm, 0, 0, spanw, tm->h, x, 0, spanw, DECOR_TOP);
	}
	if (rw > 0) {
		chrome_blit(c, tr, 0, 0, tr->w, tr->h, w - rw, 0, rw, DECOR_TOP);
	}
	return true;
}

/* A title-bar button tile, centred in the 13 px box; oversized art is
 * scaled down so it cannot paint over the title or its neighbours. */
static void blit_button_tile(struct pl_canvas *c, const struct chrome_tile *t,
		int bx, int by) {
	const int box = BOX_SIZE + 1;
	int dw = t->w, dh = t->h;
	if (dw > box) {
		dh = dh * box / dw;
		if (dh < 1) dh = 1;
		dw = box;
	}
	if (dh > box) {
		dw = dw * box / dh;
		if (dw < 1) dw = 1;
		dh = box;
	}
	chrome_blit(c, t, 0, 0, t->w, t->h, bx + (box - dw) / 2,
		by + (box - dh) / 2, dw, dh);
}

/* A themed title-bar button (13 px art over the 12 px box origin keeps
 * the existing hit testing). Pressed art while held. Dialogs prefer
 * their own smaller art, falling back to the document buttons. */
static bool paint_themed_box(struct pl_canvas *c, const struct decor_state *st,
		enum decor_part part, int bx, int by, bool pressed) {
	if (st->style == DECOR_STYLE_MOVABLE_MODAL) {
		int id = -1, pid = -1;
		switch (part) {
		case DECOR_PART_CLOSE:
			id = CHROME_BUTTON_DIALOG_CLOSE;
			pid = CHROME_BUTTON_DIALOG_CLOSE_PRESSED;
			break;
		case DECOR_PART_ZOOM:
			id = CHROME_BUTTON_DIALOG_ZOOM;
			pid = CHROME_BUTTON_DIALOG_ZOOM_PRESSED;
			break;
		case DECOR_PART_COLLAPSE:
			id = CHROME_BUTTON_DIALOG_GROW;
			pid = CHROME_BUTTON_DIALOG_GROW_PRESSED;
			break;
		default:
			break;
		}
		if (id >= 0) {
			const struct chrome_tile *t = chrome_tile_get(pressed ? pid : id);
			if (!t) {
				t = chrome_tile_get(id);
			}
			if (t) {
				blit_button_tile(c, t, bx, by);
				return true;
			}
		}
	}
	int id = -1, pid = -1;
	switch (part) {
	case DECOR_PART_CLOSE:
		id = CHROME_BUTTON_CLOSE;
		pid = CHROME_BUTTON_CLOSE_PRESSED;
		break;
	case DECOR_PART_ZOOM:
		id = CHROME_BUTTON_ZOOM;
		pid = CHROME_BUTTON_ZOOM_PRESSED;
		break;
	case DECOR_PART_COLLAPSE:
		id = CHROME_BUTTON_GROW;
		pid = CHROME_BUTTON_GROW_PRESSED;
		break;
	default:
		return false;
	}
	const struct chrome_tile *t = chrome_tile_get(pressed ? pid : id);
	if (!t) {
		t = chrome_tile_get(id);
	}
	if (!t) {
		return false;
	}
	blit_button_tile(c, t, bx, by);
	return true;
}

/* Active resize box, 20x20 from (W-21, H-21) to (W-2, H-2), measured from
 * HIG figure 5-1. Hex digit = gray level. */
static const char *const grow_active[20] = {
	"0000000000000000fcc9",
	"0ffffffffffffffffcc9",
	"0fccccccccccccccccc9",
	"0fccccccccccccccccc9",
	"0fcccccccffcccccccc9",
	"0fccccccfc7cccccccc9",
	"0fcccccfc7cffcccccc9",
	"0fccccfc7cfc7cccccc9",
	"0fcccfc7cfc7cffcccc9",
	"0fccfc7cfc7cfc7cccc9",
	"0fcca7cfc7cfc7ccccc9",
	"0fccccfc7cfc7cccccc9",
	"0fcccca7cfc7ccccccc9",
	"0fccccccfc7cccccccc9",
	"0fcccccca7ccccccccc9",
	"0fccccccccccccccccc9",
	"ffccccccccccccccccc9",
	"ccccccccccccccccccc9",
	"ccccccccccccccccccc9",
	"99999999999999999999",
};

/* A pop-up's tab: room either side of the title, and its narrowest. */
#define TAB_PAD 12
#define TAB_MIN_W 64

static int frame_height(const struct decor_state *st) {
	if (st->collapsed) {
		return st->style == DECOR_STYLE_POPUP ? DECOR_TAB_H : DECOR_COLLAPSED_H;
	}
	return st->height;
}

int decor_frame_height(const struct decor_state *st) {
	return frame_height(st);
}

/* Title groups that ride the theme's patch (Kaleidoscope's own Aqua):
 * close, title/proxy, zoom, grow clustered at the bar's left, the span
 * filling the rest. Other themes keep the Platinum box positions. */
static int title_ink_width(const struct decor_state *st);

static bool title_cluster_left(const struct decor_state *st) {
	char v[16];
	return st && st->style == DECOR_STYLE_DOCUMENT &&
		zac_theme_string("title-cluster", v, sizeof(v)) && strcmp(v, "left") == 0;
}

/* The close box's left edge in a left cluster: real tab caps (pxm#
 * 6176, 20 px wide) carry an 8 px shoulder outside the tab; synthetic
 * test tiles do not. */
static int cluster_close_x(const struct decor_state *st) {
	const struct chrome_tile *tl = chrome_tile_get(
		st->active ? CHROME_BAR_LEFT : CHROME_BAR_LEFT_INACTIVE);
	if (!tl) {
		tl = chrome_tile_get(CHROME_BAR_LEFT);
	}
	return tl && tl->w >= 16 ? CLOSE_X + 8 : CLOSE_X;
}

/* The title block's start in a left cluster: after the close box. */
static int cluster_title_x(const struct decor_state *st) {
	int x = cluster_close_x(st);
	if (st->has_close) {
		x += BOX_SIZE + 4;
	}
	return x;
}

/* A clustered box's left edge: close first, then zoom and grow after the
 * title block. -1 when the window has no such box. */
static int cluster_box_x(const struct decor_state *st, enum decor_part part) {
	if (part == DECOR_PART_CLOSE) {
		return st->has_close ? cluster_close_x(st) : -1;
	}
	int x = cluster_title_x(st);
	if (st->proxy) {
		x += PROXY_SIZE + PROXY_GAP;
	}
	x += title_ink_width(st);
	if (part == DECOR_PART_ZOOM) {
		return st->has_zoom ? x + 4 : -1;
	}
	if (st->has_zoom) {
		x += 4 + BOX_SIZE;
	}
	if (part == DECOR_PART_COLLAPSE) {
		return st->has_collapse ? x + 4 : -1;
	}
	return -1;
}

/* Whether the bar paints from theme art: documents use the title bar
 * tiles, movable-modals their dialog tiles. */
static bool bar_themed(const struct decor_state *st, bool act) {
	if (themed_bar_available(act)) {
		return true;
	}
	return st->style == DECOR_STYLE_MOVABLE_MODAL && themed_dialog_available(act);
}

/* The title group's right end in a left cluster: after the last box. */
static int title_group_end(const struct decor_state *st) {
	int x = cluster_title_x(st);
	if (st->proxy) {
		x += PROXY_SIZE + PROXY_GAP;
	}
	x += title_ink_width(st);
	if (st->has_zoom) {
		x += 4 + BOX_SIZE;
	}
	if (st->has_collapse) {
		x += 4 + BOX_SIZE;
	}
	return x + 4;
}

/* A movable-modal close box the theme draws itself (its dialog-close
 * art): Platinum dialogs show none, themed ones do. Zoom/grow stay
 * with the window's own flags (always off for dialogs). */
static bool dialog_close_box(const struct decor_state *st) {
	if (st->has_close || st->style != DECOR_STYLE_MOVABLE_MODAL) {
		return st->has_close;
	}
	return chrome_tile_get(CHROME_BUTTON_DIALOG_CLOSE) != NULL ||
		chrome_tile_get(CHROME_BUTTON_DIALOG_CLOSE_PRESSED) != NULL;
}

/* The dialog bar's own art (movable-modal windows): pinstripe, with a
 * close box but no patch/span split. Falls back to the document bar. */
static bool themed_dialog_available(bool act) {
	int m = act ? CHROME_BAR_DIALOG_MIDDLE : CHROME_BAR_DIALOG_MIDDLE_INACTIVE;
	const struct chrome_tile *tm = chrome_tile_get(m);
	if (!tm) {
		tm = chrome_tile_get(CHROME_BAR_DIALOG_MIDDLE);
	}
	return tm != NULL;
}

void decor_box_origin(const struct decor_state *st, enum decor_part part,
		int *bx, int *by) {
	*by = BOX_Y;
	if (title_cluster_left(st)) {
		*bx = cluster_box_x(st, part);
		if (*bx < 0) {
			*bx = *by = -1;
		}
		return;
	}
	switch (part) {
	case DECOR_PART_CLOSE:
		*bx = dialog_close_box(st) ? CLOSE_X : -1;
		break;
	case DECOR_PART_ZOOM:
		*bx = st->width - ZOOM_X_FROM_RIGHT;
		break;
	case DECOR_PART_COLLAPSE:
		*bx = st->width - COLLAPSE_X_FROM_RIGHT;
		break;
	default:
		*bx = *by = -1;
		break;
	}
}

static void paint_box(struct pl_canvas *c, int bx, int by,
		enum decor_part part, bool pressed) {
	pl_hline(c, bx, bx + 11, by, C_888);
	pl_vline(c, bx, by, by + 11, C_888);
	pl_outline(c, bx + 1, by + 1, bx + 11, by + 11, C_222);

	/* 9x9 interior: bevel ring around a 7x7 diagonal gradient.
	 * TODO: pressed state is not in the HIG figures; this inverted, darker
	 * rendering is a placeholder until measured. */
	int ix = bx + 2, iy = by + 2;
	uint32_t hi = pressed ? C_555 : C_CCC;
	uint32_t lo = pressed ? C_CCC : C_888;
	int base = pressed ? 0x5 : 0x9;
	pl_put(c, ix, iy, pressed ? C_555 : C_WHITE);
	pl_hline(c, ix + 1, ix + 8, iy, hi);
	pl_vline(c, ix, iy + 1, iy + 8, hi);
	pl_hline(c, ix + 1, ix + 8, iy + 8, lo);
	pl_vline(c, ix + 8, iy + 1, iy + 7, lo);
	for (int j = 0; j < 7; j++) {
		for (int i = 0; i < 7; i++) {
			pl_put(c, ix + 1 + i, iy + 1 + j, GRAY(base + (i + j) / 2));
		}
	}

	pl_vline(c, bx + 12, by + 1, by + 12, C_WHITE);
	pl_hline(c, bx + 1, bx + 12, by + 12, C_WHITE);

	if (part == DECOR_PART_ZOOM) {
		pl_vline(c, bx + 7, by + 2, by + 7, C_222);
		pl_hline(c, bx + 2, bx + 7, by + 7, C_222);
	} else if (part == DECOR_PART_COLLAPSE) {
		pl_hline(c, bx + 1, bx + 11, by + 5, C_222);
		pl_hline(c, bx + 1, bx + 11, by + 7, C_222);
	}
}

static void paint_stripes(struct pl_canvas *c, int x0, int x1) {
	if (x1 - x0 < 1) {
		return;
	}
	const uint32_t light = chrome("title-stripe-light", C_WHITE);
	const uint32_t dark = chrome("title-stripe-dark", C_777);
	for (int k = 0; k < 6; k++) {
		pl_hline(c, x0, x1 - 1, BOX_Y + 2 * k, light);
		pl_hline(c, x0 + 1, x1, BOX_Y + 2 * k + 1, dark);
	}
}

/* Horizontal extent available to stripes and title, inclusive. */
static void title_span(const struct decor_state *st, int *x0, int *x1) {
	/* Without boxes the stripes run from x=5 to W-6 (HIG figure 3-2). */
	*x0 = st->has_close ? CLOSE_X + BOX_SIZE + 1 + BOX_STRIPE_GAP : NO_BOX_STRIPE_X;
	int right_box = st->width;
	if (st->has_zoom) {
		right_box = st->width - ZOOM_X_FROM_RIGHT;
	} else if (st->has_collapse) {
		right_box = st->width - COLLAPSE_X_FROM_RIGHT;
	}
	*x1 = right_box < st->width ? right_box - BOX_STRIPE_GAP - 1
		: st->width - 1 - NO_BOX_STRIPE_X;
}

struct decor_margins decor_margins(enum decor_style style) {
	if (style == DECOR_STYLE_POPUP) {
		/* The body's edge is a document window's, under the tab. */
		return (struct decor_margins){ .left = DECOR_LEFT, .right = DECOR_RIGHT,
			.top = DECOR_TAB_H + 6, .bottom = DECOR_BOTTOM };
	}
	if (style == DECOR_STYLE_MOVABLE_MODAL) {
		return (struct decor_margins){ .left = 3, .right = 3, .top = 24, .bottom = 3 };
	}
	return (struct decor_margins){
		.left = DECOR_LEFT, .right = DECOR_RIGHT,
		.top = DECOR_TOP + (style == DECOR_STYLE_DOCUMENT ? themed_band_max() : 0),
		.bottom = DECOR_BOTTOM,
	};
}

int decor_title_max_width(const struct decor_state *st) {
	if (st->style == DECOR_STYLE_POPUP) {
		const int width = st->width - 2 * TAB_PAD;
		return width > 0 ? width : 0;
	}
	int x0, x1;
	title_span(st, &x0, &x1);
	int center = (st->width - 1) / 2;
	int half = center - x0 < x1 - center ? center - x0 : x1 - center;
	int width = half > 0 ? 2 * half + 1 : 0;
	if (st->proxy) {
		width -= PROXY_SIZE + PROXY_GAP;
	}
	return width > 0 ? width : 0;
}

/* Where the title's ink starts, and the proxy icon's left (if any): the
 * icon and the title are centred as one. Left clusters start the group
 * after the close box instead. */
static void title_layout(const struct decor_state *st, int ink_w, int *ink_l, int *proxy_x) {
	if (title_cluster_left(st)) {
		const int x = cluster_title_x(st);
		*proxy_x = x;
		*ink_l = st->proxy ? x + PROXY_SIZE + PROXY_GAP : x;
		return;
	}
	const int group = ink_w + (st->proxy ? PROXY_SIZE + PROXY_GAP : 0);
	const int left = (st->width - 1) / 2 - (group - 1) / 2;
	*proxy_x = left;
	*ink_l = st->proxy ? left + PROXY_SIZE + PROXY_GAP : left;
}

static int title_ink_width(const struct decor_state *st) {
	const struct plat_text *t = st->title;
	return t && t->ink_l >= 0 ? t->ink_r - t->ink_l + 1 : 0;
}

bool decor_title_hit(const struct decor_state *st, int x, int y) {
	const struct plat_text *t = st->title;
	if (st->style == DECOR_STYLE_POPUP || !t || t->ink_l < 0 || y < 0 || y >= DECOR_TOP - 2) {
		return false;
	}
	const int ink_w = title_ink_width(st);
	int ink_l, proxy_x;
	title_layout(st, ink_w, &ink_l, &proxy_x);
	return x >= ink_l - STRIPE_TEXT_GAP && x <= ink_l + ink_w - 1 + STRIPE_TEXT_GAP;
}

bool decor_proxy_hit(const struct decor_state *st, int x, int y) {
	if (!st->proxy || st->style != DECOR_STYLE_DOCUMENT) {
		return false;
	}
	int ink_l, proxy_x;
	title_layout(st, title_ink_width(st), &ink_l, &proxy_x);
	return x >= proxy_x - 1 && x <= proxy_x + PROXY_SIZE && y >= PROXY_Y - 1 &&
		y <= PROXY_Y + PROXY_SIZE;
}

/* Where an active title bar's stripes go: one span, or two either side
 * of the title (and proxy icon); x inclusive, as paint_stripes takes them. */
static int title_stripes(const struct decor_state *st, int x0[2], int x1[2]) {
	if (!st->active) {
		return 0;
	}
	int a, b;
	title_span(st, &a, &b);
	const struct plat_text *t = st->title;
	if ((!t || t->ink_l < 0) && !st->proxy) {
		x0[0] = a;
		x1[0] = b;
		return 1;
	}
	const int ink_w = title_ink_width(st);
	int ink_l, proxy_x;
	title_layout(st, ink_w, &ink_l, &proxy_x);
	int ink_r = ink_l + ink_w - 1, left = ink_l;
	if (st->proxy) {
		left = proxy_x;
		if (ink_w == 0) {
			ink_r = proxy_x + PROXY_SIZE - 1;
		}
	}
	x0[0] = a;
	x1[0] = left - STRIPE_TEXT_GAP - 1;
	x0[1] = ink_r + STRIPE_TEXT_GAP + 1;
	x1[1] = b;
	return 2;
}

static void paint_title(struct pl_canvas *c, const struct decor_state *st) {
	const bool themed = bar_themed(st, st->active);
	const struct plat_text *t = st->title;
	if ((t && t->ink_l >= 0) || st->proxy) {
		const int ink_w = title_ink_width(st);
		int ink_l, proxy_x;
		title_layout(st, ink_w, &ink_l, &proxy_x);
		if (ink_w > 0) {
			pl_text(c, t, ink_l, TITLE_BASELINE,
				title_ink(st->active, chrome("title-base", C_CCC)));
		}
		if (st->proxy) {
			pl_icon_paint(c, proxy_x, PROXY_Y, (enum pl_icon_kind)(st->proxy - 1), PROXY_SIZE, false);
		}
	}
	int x0[2], x1[2];
	if (themed) {
		return; /* the theme draws its own bar: no platinum stripes */
	}
	const int n = title_stripes(st, x0, x1);
	for (int i = 0; i < n; i++) {
		paint_stripes(c, x0[i], x1[i]);
	}
}

/* Fill [x0, x1) x [y0, y1) of a w x h buffer, clipped. */
static void fill_px(uint32_t *px, int w, int h, int x0, int y0, int x1, int y1, uint32_t color) {
	x0 = x0 < 0 ? 0 : x0;
	y0 = y0 < 0 ? 0 : y0;
	x1 = x1 > w ? w : x1;
	y1 = y1 > h ? h : y1;
	for (int y = y0; y < y1; y++) {
		for (int x = x0; x < x1; x++) {
			px[(size_t)y * w + x] = color;
		}
	}
}

void decor_paint_stripes_scaled(uint32_t *dst, int dw, int dh, const struct decor_state *st,
		int px, int py, int ux, int uy, int quarters) {
	int x0[2], x1[2];
	if (bar_themed(st, st->active)) {
		return; /* the theme draws its own bar: no platinum stripes */
	}
	const int n = title_stripes(st, x0, x1);
	if (n == 0 || quarters % 4 == 0) {
		return;
	}
	/* The part's screen origin, and the stripe band's rows, on screen. */
	const int ox = pl_snap(ux + px, quarters), oy = pl_snap(uy + py, quarters);
	const int top = pl_snap(uy + BOX_Y, quarters) - oy;
	const int band = pl_snap(uy + BOX_Y + 12, quarters) - pl_snap(uy + BOX_Y, quarters);
	/* Six ridges, each the same: as thick a white line as fits, the gray
	 * one below it the rest; what is left over split above and below. */
	const int period = band / 6, white = (period + 1) / 2, gray = period - white;
	const int margin = (band - 6 * period) / 2;
	for (int i = 0; i < n; i++) {
		if (x1[i] - x0[i] < 1) {
			continue;
		}
		const int wl = pl_snap(ux + x0[i], quarters) - ox, wr = pl_snap(ux + x1[i], quarters) - ox;
		const int gl = pl_snap(ux + x0[i] + 1, quarters) - ox;
		const int gr = pl_snap(ux + x1[i] + 1, quarters) - ox;
		fill_px(dst, dw, dh, wl, top, gr, top + band, chrome("title-base", C_CCC));
		const uint32_t ridge_light = chrome("title-stripe-light", C_WHITE);
		const uint32_t ridge_dark = chrome("title-stripe-dark", C_777);
		for (int k = 0; k < 6; k++) {
			const int y = top + margin + k * period;
			fill_px(dst, dw, dh, wl, y, wr, y + white, ridge_light);
			fill_px(dst, dw, dh, gl, y + white, gr, y + white + gray, ridge_dark);
		}
	}
}

static void paint_grow(struct pl_canvas *c, const struct decor_state *st) {
	int gx = st->width - DECOR_GROW_INSET, gy = st->height - DECOR_GROW_INSET;
	if (st->active) {
		pl_grays(c, gx, gy, grow_active, 20);
		return;
	}
	/* Inactive: blank cell; the well lines stop at it. */
	int x1 = st->width - DECOR_RIGHT, y1 = st->height - DECOR_BOTTOM;
	const uint32_t face = inactive_face(C_DDD);
	pl_fill(c, gx + 1, gy + 1, x1, y1, face);
	pl_hline(c, gx, x1, gy, inactive_border(C_555, face));
	pl_vline(c, gx, gy, y1, inactive_border(C_555, face));
}

/* Movable modal dialog (HIG figure 3-2): a document-style title bar
 * without boxes over a body edged by a 2 px bevel. Margins 3/3/24/3. */
/* Which boxes a themed movable-modal shows: close whenever its own
 * art exists (Platinum shows none); zoom and grow likewise, so an
 * About-style window keeps its whole candy row. */
static bool dialog_box_on(const struct decor_state *st, enum decor_part part, bool act) {
	if (part == DECOR_PART_CLOSE) {
		return dialog_close_box(st);
	}
	int id = part == DECOR_PART_ZOOM ? CHROME_BUTTON_DIALOG_ZOOM : CHROME_BUTTON_DIALOG_GROW;
	return act && themed_dialog_available(act) &&
		(chrome_tile_get(id) != NULL || chrome_tile_get(id + 1) != NULL);
}

static void paint_modal(struct pl_canvas *c, const struct decor_state *st) {
	const int W = st->width, H = st->height;
	const bool act = st->active;
	const uint32_t face = act ? chrome("frame-face", C_CCC) : inactive_face(C_DDD);
	const uint32_t border = act ? chrome("frame-border", C_BLACK) : inactive_border(C_555, face);

	/* Title bar: rows 0..21, as a collapsed document window's. */
	chrome_fill_slot(c, 0, 0, W - 1, 21, CHROME_MODAL_TOP, act, face);
	const bool themed = st->style == DECOR_STYLE_MOVABLE_MODAL && themed_dialog_available(act);
	/* Body edges (the client paints the interior). */
	chrome_fill_slot(c, 0, 22, 2, H - 1, CHROME_MODAL_LEFT, act, themed_or(face, C_DDD));
	chrome_fill_slot(c, W - 3, 22, W - 1, H - 1, CHROME_MODAL_RIGHT, act, themed_or(face, C_DDD));
	chrome_fill_slot(c, 0, 22, W - 1, 23, CHROME_MODAL_BOTTOM, act, themed_or(face, C_DDD));
	chrome_fill_slot(c, 0, H - 3, W - 1, H - 1, CHROME_MODAL_BOTTOM, act, themed_or(face, C_DDD));

	const uint32_t m_light = chrome("frame-light", C_WHITE);
	const uint32_t m_shadow = chrome("frame-dark", C_999);
	pl_outline(c, 0, 0, W - 1, H - 1, border);
	if (act) {
		pl_hline(c, 1, W - 3, 1, m_light);
		pl_vline(c, 1, 1, 19, m_light);
		pl_vline(c, W - 2, 2, 20, m_shadow);
		pl_hline(c, 2, W - 2, 20, m_shadow);
	}
	pl_hline(c, 0, W - 1, 21, border);

	if (act) {
		pl_hline(c, 2, W - 4, 23, C_WHITE);
		pl_vline(c, 2, 23, H - 4, C_WHITE);
		pl_vline(c, W - 3, 24, H - 3, C_999);
		pl_hline(c, 3, W - 3, H - 3, C_999);
		pl_vline(c, W - 2, 23, H - 2, C_555);
		pl_hline(c, 2, W - 2, H - 2, C_555);
	}

	pl_vline(c, W, 2, H, border);
	pl_hline(c, 2, W, H, border);
	if (themed) {
		paint_themed_bar(c, st, W, act);
		if (act) {
			/* Kaleidoscope skins every movable-modal box, even where
			 * Platinum shows none. */
			const enum decor_part parts[] = { DECOR_PART_CLOSE,
				DECOR_PART_ZOOM, DECOR_PART_COLLAPSE };
			for (int i = 0; i < 3; i++) {
				if (!dialog_box_on(st, parts[i], act)) {
					continue;
				}
				int bx, by;
				decor_box_origin(st, parts[i], &bx, &by);
				if (bx < 0) {
					continue;
				}
				if (!paint_themed_box(c, st, parts[i], bx, by,
						st->pressed == parts[i])) {
					break; /* plain art missing: keep Platinum's bare bar */
				}
			}
		}
	}
	paint_title(c, st);
}

int decor_tab_width(const struct decor_state *st) {
	int w = title_ink_width(st) + 2 * TAB_PAD;
	w = w < TAB_MIN_W ? TAB_MIN_W : w;
	return w > st->width ? st->width : w;
}

/* A pop-up window: the tab (rows 0..DECOR_TAB_H-1) with its top corners
 * cut, standing on a body edged like a document window, which opens into
 * the tab. Collapsed, the tab alone, at the bottom of the screen. */
static void paint_popup(struct pl_canvas *c, const struct decor_state *st) {
	const int W = st->width, H = frame_height(st);
	const bool act = st->active;
	const uint32_t face = act ? chrome("frame-face", C_CCC) : inactive_face(C_DDD);
	const uint32_t border = act ? chrome("frame-border", C_BLACK) : inactive_border(C_555, face);
	const int x0 = st->tab_x, x1 = st->tab_x + decor_tab_width(st) - 1;
	const int y0 = DECOR_TAB_H; /* the body's top row */

	if (!st->collapsed) {
		pl_fill(c, 0, y0, W - 1, y0 + 5, face);
		pl_fill(c, 0, H - DECOR_BOTTOM, W - 1, H - 1, face);
		pl_fill(c, 0, y0, DECOR_LEFT - 1, H - 1, face);
		pl_fill(c, W - DECOR_RIGHT, y0, W - 1, H - 1, face);
		const uint32_t p_light = chrome("frame-light", C_WHITE);
		const uint32_t p_shadow = chrome("frame-dark", C_999);
		pl_outline(c, 0, y0, W - 1, H - 1, border);
		if (act) {
			pl_hline(c, 1, W - 3, y0 + 1, p_light);
			pl_vline(c, 1, y0 + 1, H - 3, p_light);
			pl_vline(c, W - 2, y0 + 2, H - 2, p_shadow);
			pl_hline(c, 2, W - 2, H - 2, p_shadow);
			pl_hline(c, 4, W - 6, y0 + 4, p_shadow);
			pl_vline(c, 4, y0 + 4, H - 6, p_shadow);
		}
		pl_outline(c, 5, y0 + 5, W - 6, H - 6, border);
		if (act) {
			pl_vline(c, W - 5, y0 + 5, H - 5, p_light);
			pl_hline(c, 4, W - 5, H - 5, p_light);
		}
		/* The resizing corners, marked off on the top edge. */
		pl_vline(c, DECOR_POPUP_CORNER, y0 + 1, y0 + 4, C_888);
		pl_vline(c, W - 1 - DECOR_POPUP_CORNER, y0 + 1, y0 + 4, C_888);
	}

	/* The tab: face, a black edge with its two top corners cut, the
	 * emboss of a title bar. A closed one along the bottom of the screen
	 * always reads as active. */
	const bool tab_act = act || st->collapsed;
	const uint32_t active_face = chrome("frame-face", C_CCC);
	const uint32_t active_border = chrome("frame-border", C_BLACK);
	const uint32_t tab_border = tab_act ? active_border : inactive_border(C_555, face);
	const uint32_t tab_face = tab_act ? active_face : inactive_face(C_DDD);
	chrome_fill_slot(c, x0 + 1, 1, x1 - 1, H > y0 ? y0 : H - 1, CHROME_POPUP_TAB, tab_act, tab_face);
	pl_hline(c, x0 + 2, x1 - 2, 0, tab_border);
	pl_put(c, x0 + 1, 1, tab_border);
	pl_put(c, x1 - 1, 1, tab_border);
	pl_vline(c, x0, 2, y0 - 1, tab_border);
	pl_vline(c, x1, 2, y0 - 1, tab_border);
	if (tab_act) {
		const uint32_t tl = chrome("frame-light", C_WHITE);
		const uint32_t td = chrome("frame-dark", C_999);
		pl_hline(c, x0 + 2, x1 - 2, 1, tl);
		pl_vline(c, x0 + 1, 2, st->collapsed ? y0 - 1 : y0 + 1, tl);
		pl_vline(c, x1 - 1, 2, st->collapsed ? y0 - 1 : y0 + 1, td);
	}
	if (!st->collapsed) {
		/* Open: the tab's sides run on into the body's edge. */
		pl_put(c, x0, y0, tab_border);
		pl_put(c, x1, y0, tab_border);
	}

	const struct plat_text *t = st->title;
	const int ink_w = title_ink_width(st);
	if (ink_w > 0) {
		pl_text(c, t, x0 + (x1 - x0 + 1 - ink_w) / 2, TITLE_BASELINE,
			title_ink(tab_act, tab_face));
	}
}

void decor_paint(struct pl_canvas *c, const struct decor_state *st) {
	if (st->style == DECOR_STYLE_MOVABLE_MODAL) {
		paint_modal(c, st);
		return;
	}
	if (st->style == DECOR_STYLE_POPUP) {
		paint_popup(c, st);
		return;
	}
	const int W = st->width, H = frame_height(st);
	const bool act = st->active;
	const uint32_t face = act ? chrome("frame-face", C_CCC) : inactive_face(C_DDD);
	const uint32_t border = act ? chrome("frame-border", C_BLACK) : inactive_border(C_555, face);

	const bool has_bar = bar_themed(st, act);
	/* Frame fill, never touching the client area. */
	if (st->collapsed) {
		chrome_fill_slot(c, 0, 0, W - 1, H - 1, CHROME_DOC_TOP, act, face);
	} else {
		/* The title row is the themed bar's own canvas when one is
		 * installed (its tab stays transparent past its end caps);
		 * otherwise the frame skin fills it. */
		if (!has_bar) {
			chrome_fill_slot(c, 0, 0, W - 1, DECOR_TOP - 1, CHROME_DOC_TOP, act, face);
		}
		chrome_fill_slot(c, 0, H - DECOR_BOTTOM, W - 1, H - 1, CHROME_DOC_BOTTOM, act, face);
		chrome_fill_slot(c, 0, DECOR_TOP, DECOR_LEFT - 1, H - DECOR_BOTTOM - 1, CHROME_DOC_LEFT, act, face);
		chrome_fill_slot(c, W - DECOR_RIGHT, DECOR_TOP, W - 1, H - DECOR_BOTTOM - 1, CHROME_DOC_RIGHT, act, face);
	}

	const uint32_t highlight = chrome("frame-light", C_WHITE);
	const uint32_t shadow = chrome("frame-dark", C_999);
	if (has_bar) {
		if (!st->collapsed) {
			pl_vline(c, 0, DECOR_TOP, H - 1, border);
			pl_vline(c, W - 1, DECOR_TOP, H - 1, border);
			pl_hline(c, 0, W - 1, H - 1, border);
			if (act) {
				pl_vline(c, 1, DECOR_TOP, H - 3, highlight);
				pl_vline(c, W - 2, DECOR_TOP, H - 2, shadow);
				pl_hline(c, 2, W - 2, H - 2, shadow);
			}
		}
	} else {
		pl_outline(c, 0, 0, W - 1, H - 1, border);
		if (act) {
			pl_hline(c, 1, W - 3, 1, highlight);
			pl_vline(c, 1, 1, H - 3, highlight);
			pl_vline(c, W - 2, 2, H - 2, shadow);
			pl_hline(c, 2, W - 2, H - 2, shadow);
		}
	}

	/* Drop shadow: 1 px, offset 2 px in from the corners. */
	const int shadow_top = (has_bar && title_cluster_left(st) && span_tile_get(act))
		? DECOR_TOP - 5 : 2;
	pl_vline(c, W, shadow_top, H, border);
	pl_hline(c, 2, W, H, border);

	if (!st->collapsed) {
		if (act && !has_bar) {
			pl_hline(c, 4, W - 6, 20, shadow);
			pl_vline(c, 4, 20, H - 6, shadow);
		} else if (act) {
			pl_vline(c, 4, DECOR_TOP, H - 6, shadow);
		}
		pl_outline(c, 5, 21, W - 6, H - 6, border);
		if (act) {
			pl_vline(c, W - 5, 21, H - 5, highlight);
			pl_hline(c, 4, W - 5, H - 5, highlight);
		}
		if (st->has_grow) {
			paint_grow(c, st);
		}
	}
	if (has_bar) {
		paint_themed_bar(c, st, W, act);
	}

	if (act) {
		int bx, by;
		const enum decor_part boxes[] = {
			DECOR_PART_CLOSE, DECOR_PART_ZOOM, DECOR_PART_COLLAPSE,
		};
		const bool present[] = { dialog_close_box(st), st->has_zoom, st->has_collapse };
		for (int i = 0; i < 3; i++) {
			if (present[i]) {
				decor_box_origin(st, boxes[i], &bx, &by);
				if (!paint_themed_box(c, st, boxes[i], bx, by, st->pressed == boxes[i])) {
					paint_box(c, bx, by, boxes[i], st->pressed == boxes[i]);
				}
			}
		}
	}
	paint_title(c, st);
}

static bool in_box(const struct decor_state *st, enum decor_part part, int x, int y) {
	int bx, by;
	decor_box_origin(st, part, &bx, &by);
	return x >= bx && x <= bx + BOX_SIZE && y >= by && y <= by + BOX_SIZE;
}

enum decor_part decor_hit(const struct decor_state *st, int x, int y) {
	const int W = st->width, H = frame_height(st);
	if (x < 0 || y < 0 || x >= W || y >= H) {
		return DECOR_PART_NONE;
	}
	if (st->style == DECOR_STYLE_POPUP) {
		/* Above the body only the tab is there; the rest lets clicks
		 * through, as does all but the tab when it is closed. */
		if (y < DECOR_TAB_H) {
			return x >= st->tab_x && x < st->tab_x + decor_tab_width(st)
				? DECOR_PART_TAB : DECOR_PART_NONE;
		}
		if (st->collapsed) {
			return DECOR_PART_NONE;
		}
		const struct decor_margins m = decor_margins(st->style);
		if (y < DECOR_TAB_H + DECOR_POPUP_CORNER && (x < DECOR_POPUP_CORNER ||
				x >= W - DECOR_POPUP_CORNER) && !(x >= m.left && x < W - m.right &&
				y >= m.top)) {
			return x < DECOR_POPUP_CORNER ? DECOR_PART_GROW_TOP_LEFT
				: DECOR_PART_GROW_TOP_RIGHT;
		}
		if (x >= m.left && x < W - m.right && y >= m.top && y < H - m.bottom) {
			return DECOR_PART_CLIENT;
		}
		return DECOR_PART_BORDER;
	}
	if (st->style == DECOR_STYLE_MOVABLE_MODAL) {
		/* Dialogs move by their title bar, except for themed boxes. */
		const struct decor_margins m = decor_margins(st->style);
		if (y < 22) {
			if (st->active) {
				const enum decor_part boxes[] = { DECOR_PART_CLOSE,
					DECOR_PART_ZOOM, DECOR_PART_COLLAPSE };
				for (int i = 0; i < 3; i++) {
					if (!dialog_box_on(st, boxes[i], st->active)) {
						continue;
					}
					if (in_box(st, boxes[i], x, y)) {
						return boxes[i];
					}
				}
			}
			return DECOR_PART_DRAG;
		}
		if (x >= m.left && x < W - m.right && y >= m.top && y < H - m.bottom) {
			return DECOR_PART_CLIENT;
		}
		return DECOR_PART_BORDER;
	}
	if (y <= TITLE_BAR_LAST_ROW && st->active) {
		if (st->has_close && in_box(st, DECOR_PART_CLOSE, x, y)) {
			return DECOR_PART_CLOSE;
		}
		if (st->has_zoom && in_box(st, DECOR_PART_ZOOM, x, y)) {
			return DECOR_PART_ZOOM;
		}
		if (st->has_collapse && in_box(st, DECOR_PART_COLLAPSE, x, y)) {
			return DECOR_PART_COLLAPSE;
		}
	}
	if (st->collapsed) {
		return DECOR_PART_DRAG;
	}
	if (st->has_grow && x >= W - DECOR_GROW_INSET && y >= H - DECOR_GROW_INSET) {
		return DECOR_PART_GROW;
	}
	const struct decor_margins dm = decor_margins(st->style);
	if (x >= DECOR_LEFT && x < W - DECOR_RIGHT &&
			y >= dm.top && y < H - DECOR_BOTTOM) {
		return DECOR_PART_CLIENT;
	}
	return DECOR_PART_DRAG;
}
