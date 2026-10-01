#include <cairo.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "text.h"

#include "fonts_data.h"

/*
 * Text is drawn with our own bitmap fonts (assets/fonts), which carry the
 * measured Mac OS 8 metrics. A character they don't have (CJK in a file
 * name, say) falls back to a non-antialiased DejaVu Sans glyph, sized to
 * sit on the same baseline.
 */

struct style {
	const struct pl_bitmap_font *font;
	bool italic;
	/* Fallback TrueType face for missing characters. */
	double fallback_px;
	bool fallback_bold;
};

static const struct style styles[] = {
	[PL_FONT_SYSTEM] = { &font_system, false, 12.0, true },
	[PL_FONT_VIEWS] = { &font_views, false, 11.0, false },
	[PL_FONT_VIEWS_ITALIC] = { &font_views, true, 11.0, false },
};

/* QuickDraw-style italic: rows shift right one pixel per two rows above
 * the baseline (and left below it). */
static int italic_shift(int y) {
	return y <= 0 ? (-y) / 2 : -((y + 1) / 2);
}

static const struct pl_glyph *find_glyph(const struct pl_bitmap_font *f, uint32_t cp) {
	int lo = 0, hi = f->n - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (f->glyphs[mid].cp == cp) {
			return &f->glyphs[mid];
		}
		if (f->glyphs[mid].cp < cp) {
			lo = mid + 1;
		} else {
			hi = mid - 1;
		}
	}
	return NULL;
}

/* Next code point of a UTF-8 string; invalid bytes come back as U+FFFD. */
static uint32_t next_cp(const char **s) {
	const unsigned char *p = (const unsigned char *)*s;
	uint32_t cp;
	int extra;
	if (p[0] < 0x80) {
		cp = p[0];
		extra = 0;
	} else if ((p[0] & 0xE0) == 0xC0) {
		cp = p[0] & 0x1F;
		extra = 1;
	} else if ((p[0] & 0xF0) == 0xE0) {
		cp = p[0] & 0x0F;
		extra = 2;
	} else if ((p[0] & 0xF8) == 0xF0) {
		cp = p[0] & 0x07;
		extra = 3;
	} else {
		*s += 1;
		return 0xFFFD;
	}
	for (int i = 1; i <= extra; i++) {
		if ((p[i] & 0xC0) != 0x80) {
			*s += i;
			return 0xFFFD;
		}
		cp = (cp << 6) | (p[i] & 0x3F);
	}
	*s += 1 + extra;
	return cp;
}

/* ---- fallback glyphs ------------------------------------------------------ */

struct fallback {
	int width, advance;
	uint8_t *mask; /* the line box's height, `width` wide; pen at column 0 */
};

static void fallback_font(cairo_t *cr, const struct style *st) {
	cairo_select_font_face(cr, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL,
		st->fallback_bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, st->fallback_px);
	cairo_font_options_t *opts = cairo_font_options_create();
	cairo_font_options_set_antialias(opts, CAIRO_ANTIALIAS_NONE);
	cairo_font_options_set_hint_style(opts, CAIRO_HINT_STYLE_FULL);
	cairo_font_options_set_hint_metrics(opts, CAIRO_HINT_METRICS_ON);
	cairo_set_font_options(cr, opts);
	cairo_font_options_destroy(opts);
}

static struct fallback render_fallback(uint32_t cp, const struct style *st) {
	char utf8[5] = { 0 };
	if (cp < 0x80) {
		utf8[0] = (char)cp;
	} else if (cp < 0x800) {
		utf8[0] = (char)(0xC0 | (cp >> 6));
		utf8[1] = (char)(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		utf8[0] = (char)(0xE0 | (cp >> 12));
		utf8[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		utf8[2] = (char)(0x80 | (cp & 0x3F));
	} else {
		utf8[0] = (char)(0xF0 | (cp >> 18));
		utf8[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
		utf8[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
		utf8[3] = (char)(0x80 | (cp & 0x3F));
	}
	const int h = st->font->height;
	cairo_surface_t *scratch = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
	cairo_t *cr = cairo_create(scratch);
	fallback_font(cr, st);
	cairo_text_extents_t ext;
	cairo_text_extents(cr, utf8, &ext);
	cairo_destroy(cr);
	cairo_surface_destroy(scratch);

	struct fallback fb = { .advance = (int)(ext.x_advance + 0.5) };
	fb.width = fb.advance + 2;
	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_A8, fb.width, h);
	cr = cairo_create(surf);
	fallback_font(cr, st);
	cairo_move_to(cr, 0, st->font->baseline);
	cairo_show_text(cr, utf8);
	cairo_destroy(cr);
	cairo_surface_flush(surf);
	const int stride = cairo_image_surface_get_stride(surf);
	const uint8_t *data = cairo_image_surface_get_data(surf);
	fb.mask = calloc((size_t)fb.width * h, 1);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < fb.width; x++) {
			fb.mask[y * fb.width + x] = data[y * stride + x] >= 128 ? 255 : 0;
		}
	}
	cairo_surface_destroy(surf);
	return fb;
}

/* ---- rendering ------------------------------------------------------------ */

static struct plat_text *render_exact(const char *utf8, const struct style *st) {
	const struct pl_bitmap_font *f = st->font;

	/* Measure: the pen starts at column 1, as callers expect. */
	int advance = 0;
	for (const char *s = utf8; *s;) {
		uint32_t cp = next_cp(&s);
		const struct pl_glyph *g = find_glyph(f, cp);
		if (g) {
			advance += g->advance;
		} else {
			struct fallback fb = render_fallback(cp, st);
			advance += fb.advance;
			free(fb.mask);
		}
	}
	const int slant = st->italic ? (f->baseline + 1) / 2 : 0;
	struct plat_text *text = calloc(1, sizeof(*text));
	text->width = 1 + advance + slant + 4;
	text->height = f->height;
	text->baseline = f->baseline;
	text->advance = advance;
	text->stride = text->width;
	text->mask = calloc((size_t)text->stride * text->height, 1);

	int pen = 1;
	for (const char *s = utf8; *s;) {
		uint32_t cp = next_cp(&s);
		const struct pl_glyph *g = find_glyph(f, cp);
		if (g) {
			for (int r = 0; r < g->rows; r++) {
				const int y = g->top + r;
				const int row = f->baseline + y;
				const int dx = st->italic ? italic_shift(y) : 0;
				const uint16_t bits = f->rows[g->first + r];
				for (int x = 0; x < 16; x++) {
					const int col = pen + x + dx;
					if ((bits >> x) & 1 && col >= 0 && col < text->width) {
						text->mask[row * text->stride + col] = 255;
					}
				}
			}
			pen += g->advance;
			continue;
		}
		struct fallback fb = render_fallback(cp, st);
		for (int y = 0; y < f->height; y++) {
			for (int x = 0; x < fb.width && pen + x < text->width; x++) {
				if (fb.mask[y * fb.width + x]) {
					text->mask[y * text->stride + pen + x] = 255;
				}
			}
		}
		pen += fb.advance;
		free(fb.mask);
	}

	text->ink_l = text->ink_r = -1;
	for (int x = 0; x < text->width; x++) {
		for (int y = 0; y < text->height; y++) {
			if (text->mask[y * text->stride + x]) {
				if (text->ink_l < 0) {
					text->ink_l = x;
				}
				text->ink_r = x;
				break;
			}
		}
	}
	return text;
}

static int ink_width(const struct plat_text *t) {
	return t->ink_l < 0 ? 0 : t->ink_r - t->ink_l + 1;
}

struct plat_text *text_render_font(const char *utf8, int max_ink_width, enum pl_font font) {
	const struct style *st = &styles[font];
	struct plat_text *text = render_exact(utf8, st);
	if (ink_width(text) <= max_ink_width) {
		return text;
	}
	text_destroy(text);

	/* Drop trailing characters (whole UTF-8 sequences) until it fits. */
	size_t len = strlen(utf8);
	char *buf = malloc(len + sizeof("…"));
	text = NULL;
	while (len > 0) {
		do {
			len--;
		} while (len > 0 && ((unsigned char)utf8[len] & 0xC0) == 0x80);
		memcpy(buf, utf8, len);
		strcpy(buf + len, "…");
		text = render_exact(buf, st);
		if (ink_width(text) <= max_ink_width) {
			break;
		}
		text_destroy(text);
		text = NULL;
	}
	if (!text) {
		text = render_exact("", st);
	}
	free(buf);
	return text;
}

struct plat_text *text_render(const char *utf8, int max_ink_width) {
	return text_render_font(utf8, max_ink_width, PL_FONT_SYSTEM);
}

void text_destroy(struct plat_text *text) {
	if (text) {
		free(text->mask);
		free(text);
	}
}
