/*
 * Arabic shaping for lib/text.c. The bitmap faces have no Arabic, and the
 * old per-codepoint fallback drew isolated letters left-to-right. This file
 * keeps the Latin path untouched: only strings with an Arabic character
 * come here.
 *
 * Runs are split at the strong direction of each character, shaped in
 * logical order by HarfBuzz (so joining and marks are right), then placed
 * in visual order. That is enough for the UI's Arabic labels; the full
 * Unicode bidi algorithm is not needed for a single-line label, but
 * numbers inside Arabic stay left-to-right, which is what matters most.
 */
#define _GNU_SOURCE
#include "shaping.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <fribidi.h>
#include <hb-ft.h>
#include <hb.h>
#include <fontconfig/fontconfig.h>
#include <stdlib.h>
#include <string.h>

#include "text.h"

#define PAD 4

struct face {
	int pixels;
	bool bold;
	bool arabic;
	FT_Face ft;
	hb_font_t *hb;
};

#define FACES_MAX 8
static struct face faces[FACES_MAX];
static int face_count;
static FT_Library ft_library;
static bool ft_ready;

static bool arabic_cp(uint32_t cp) {
	return (cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x0750 && cp <= 0x077F) ||
		(cp >= 0x08A0 && cp <= 0x08FF) || (cp >= 0xFB50 && cp <= 0xFDFF) ||
		(cp >= 0xFE70 && cp <= 0xFEFF) || (cp >= 0x1EE00 && cp <= 0x1EEFF);
}

/* Arabic punctuation and signs that belong with the Arabic run. */
static bool arabic_punctuation(uint32_t cp) {
	return cp == 0x060C || cp == 0x061B || cp == 0x061F || cp == 0x06D4 ||
		(cp >= 0x066A && cp <= 0x066D);
}

bool pl_has_arabic(const char *utf8) {
	if (!utf8) {
		return false;
	}
	const unsigned char *p = (const unsigned char *)utf8;
	while (*p) {
		uint32_t cp;
		int extra;
		if (*p < 0x80) {
			cp = *p; extra = 0;
		} else if ((*p & 0xE0) == 0xC0) {
			cp = *p & 0x1F; extra = 1;
		} else if ((*p & 0xF0) == 0xE0) {
			cp = *p & 0x0F; extra = 2;
		} else {
			cp = *p & 0x07; extra = 3;
		}
		p++;
		for (int i = 0; i < extra && (*p & 0xC0) == 0x80; i++, p++) {
			cp = (cp << 6) | (*p & 0x3F);
		}
		if (arabic_cp(cp)) {
			return true;
		}
	}
	return false;
}

static uint32_t next_cp(const unsigned char **p) {
	uint32_t cp = **p;
	int extra;
	if (cp < 0x80) {
		(*p)++; return cp;
	}
	if ((cp & 0xE0) == 0xC0) { cp &= 0x1F; extra = 1; }
	else if ((cp & 0xF0) == 0xE0) { cp &= 0x0F; extra = 2; }
	else { cp &= 0x07; extra = 3; }
	(*p)++;
	for (int i = 0; i < extra && (**p & 0xC0) == 0x80; i++, (*p)++) {
		cp = (cp << 6) | (**p & 0x3F);
	}
	return cp;
}

static struct face *load_face(int pixels, bool bold, bool arabic) {
	for (int i = 0; i < face_count; i++) {
		if (faces[i].pixels == pixels && faces[i].bold == bold &&
				faces[i].arabic == arabic) {
			return &faces[i];
		}
	}
	if (!ft_ready) {
		if (FT_Init_FreeType(&ft_library)) {
			return NULL;
		}
		ft_ready = true;
	}
	FcPattern *pat = FcPatternCreate();
	FcPatternAddString(pat, FC_FAMILY,
		(const FcChar8 *)(arabic ? "Noto Sans Arabic" : "DejaVu Sans"));
	FcPatternAddInteger(pat, FC_WEIGHT, bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
	FcPatternAddString(pat, FC_LANG, (const FcChar8 *)(arabic ? "ar" : "en"));
	FcConfigSubstitute(NULL, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);
	FcResult result;
	FcPattern *match = FcFontMatch(NULL, pat, &result);
	FcPatternDestroy(pat);
	if (!match) {
		return NULL;
	}
	FcChar8 *file = NULL;
	int index = 0;
	if (FcPatternGetString(match, FC_FILE, 0, &file) != FcResultMatch ||
			FcPatternGetInteger(match, FC_INDEX, 0, &index) != FcResultMatch || !file) {
		FcPatternDestroy(match);
		return NULL;
	}
	FT_Face ft = NULL;
	if (FT_New_Face(ft_library, (const char *)file, index, &ft)) {
		FcPatternDestroy(match);
		return NULL;
	}
	FcPatternDestroy(match);
	if (FT_Set_Pixel_Sizes(ft, 0, (FT_UInt)pixels)) {
		FT_Done_Face(ft);
		return NULL;
	}
	hb_font_t *hb = hb_ft_font_create_referenced(ft);
	if (!hb) {
		FT_Done_Face(ft);
		return NULL;
	}
	if (face_count >= FACES_MAX) {
		hb_font_destroy(hb);
		FT_Done_Face(ft);
		return NULL;
	}
	struct face *f = &faces[face_count++];
	f->pixels = pixels;
	f->bold = bold;
	f->arabic = arabic;
	f->ft = ft;
	f->hb = hb;
	return f;
}

struct run {
	char *text;
	bool rtl;
	int advance;
	int width;
	uint8_t *mask;
};

static void free_run(struct run *r) {
	free(r->text);
	free(r->mask);
}

static int shape_run(struct run *r, int height, int baseline, int pixels, bool bold) {
	struct face *face = load_face(pixels, bold, r->rtl);
	if (!face) {
		return -1;
	}
	struct face *latin = load_face(pixels, bold, false);
	hb_buffer_t *buf = hb_buffer_create();
	hb_buffer_add_utf8(buf, r->text, -1, 0, -1);
	hb_buffer_set_direction(buf, r->rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
	hb_buffer_set_script(buf, r->rtl ? HB_SCRIPT_ARABIC : HB_SCRIPT_LATIN);
	hb_buffer_set_language(buf, hb_language_from_string(r->rtl ? "ar" : "en", -1));
	hb_shape(face->hb, buf, NULL, 0);
	unsigned count = 0;
	const hb_glyph_info_t *info = hb_buffer_get_glyph_infos(buf, &count);
	const hb_glyph_position_t *pos = hb_buffer_get_glyph_positions(buf, NULL);
	int total = 0;
	for (unsigned i = 0; i < count; i++) {
		total += pos[i].x_advance / 64;
	}
	r->advance = total;
	r->width = total + 2 * PAD;
	r->mask = calloc((size_t)r->width * (size_t)height, 1);
	if (!r->mask) {
		hb_buffer_destroy(buf);
		return -1;
	}
	int pen = PAD;
	for (unsigned i = 0; i < count; i++) {
		const uint32_t cluster_cp = *(const unsigned char *)(r->text + info[i].cluster) < 0x80
			? (uint32_t)(unsigned char)r->text[info[i].cluster]
			: 0;
		if (info[i].codepoint && !FT_Load_Glyph(face->ft, info[i].codepoint, FT_LOAD_DEFAULT) &&
				!FT_Render_Glyph(face->ft->glyph, FT_RENDER_MODE_MONO)) {
			const FT_GlyphSlot slot = face->ft->glyph;
			const FT_Bitmap *bm = &slot->bitmap;
			const int gx = pen + pos[i].x_offset / 64 + slot->bitmap_left;
			const int gy = baseline - slot->bitmap_top - pos[i].y_offset / 64;
			for (int y = 0; y < (int)bm->rows; y++) {
				const int dy = gy + y;
				if (dy < 0 || dy >= height) {
					continue;
				}
				const unsigned char *row = bm->buffer +
					(bm->pitch >= 0 ? y : (int)bm->rows - 1 - y) * abs(bm->pitch);
				for (int x = 0; x < (int)bm->width; x++) {
					const int dx = gx + x;
					if (dx >= 0 && dx < r->width && (row[x / 8] & (0x80 >> (x % 8)))) {
						r->mask[dy * r->width + dx] = 255;
					}
				}
			}
			pen += pos[i].x_advance / 64;
		} else if (latin && cluster_cp && !FT_Load_Char(latin->ft,
				r->rtl && cluster_cp == '(' ? ')' : r->rtl && cluster_cp == ')' ? '(' : cluster_cp,
				FT_LOAD_DEFAULT) &&
				!FT_Render_Glyph(latin->ft->glyph, FT_RENDER_MODE_MONO)) {
			/* Neutral punctuation (the parentheses around a country) is not
			 * in Noto Sans Arabic. Draw it from the Latin face, mirrored when
			 * the run is right-to-left, and take its own advance. */
			const FT_GlyphSlot slot = latin->ft->glyph;
			const FT_Bitmap *bm = &slot->bitmap;
			const int gx = pen + slot->bitmap_left;
			const int gy = baseline - slot->bitmap_top;
			for (int y = 0; y < (int)bm->rows; y++) {
				const int dy = gy + y;
				if (dy < 0 || dy >= height) {
					continue;
				}
				const unsigned char *row = bm->buffer +
					(bm->pitch >= 0 ? y : (int)bm->rows - 1 - y) * abs(bm->pitch);
				for (int x = 0; x < (int)bm->width; x++) {
					const int dx = gx + x;
					if (dx >= 0 && dx < r->width && (row[x / 8] & (0x80 >> (x % 8)))) {
						r->mask[dy * r->width + dx] = 255;
					}
				}
			}
			pen += slot->advance.x / 64;
		} else {
			pen += pos[i].x_advance / 64;
		}
	}
	hb_buffer_destroy(buf);
	return 0;
}

/* 1 = right-to-left, 0 = left-to-right, -1 = neutral. */
static int direction(uint32_t cp) {
	if (arabic_cp(cp) || arabic_punctuation(cp)) {
		return 1;
	}
	if ((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
			(cp >= 0x00C0 && cp <= 0x024F)) {
		return 0;
	}
	if (cp >= 0x0660 && cp <= 0x0669) {
		return 0; /* Arabic-Indic digits, read left-to-right */
	}
	if (cp >= 0x06F0 && cp <= 0x06F9) {
		return 0; /* extended Arabic-Indic digits */
	}
	return -1;
}

/* FriBidi decides the paragraph direction, including the first-strong
 * fallback; the run split above only needs that base bit. */
static bool base_is_rtl(const char *utf8) {
	const size_t len = strlen(utf8);
	FriBidiChar *chars = malloc((len + 1) * sizeof(*chars));
	if (!chars) {
		return false;
	}
	const unsigned char *p = (const unsigned char *)utf8;
	FriBidiStrIndex n = 0;
	while (*p) {
		chars[n++] = next_cp(&p);
	}
	FriBidiCharType *types = malloc((size_t)n * sizeof(*types));
	if (!types) {
		free(chars);
		return false;
	}
	fribidi_get_bidi_types(chars, n, types);
	const FriBidiParType dir = fribidi_get_par_direction(types, n);
	free(types);
	free(chars);
	return dir == FRIBIDI_PAR_RTL || dir == FRIBIDI_PAR_WRTL;
}

struct plat_text *pl_shape_arabic(const char *utf8, int height, int baseline, int pixels,
		bool bold) {
	if (!utf8 || !pl_has_arabic(utf8)) {
		return NULL;
	}
	const unsigned char *p = (const unsigned char *)utf8;
	const bool base_rtl = base_is_rtl(utf8);
	const int n = (int)strlen(utf8);
	struct run *runs = calloc((size_t)n + 1, sizeof(*runs));
	int nruns = 0;
	if (!runs) {
		return NULL;
	}
	p = (const unsigned char *)utf8;
	int start = 0;
	int current = -1;
	for (int i = 0; i < n;) {
		const int at = i;
		uint32_t cp = next_cp(&p);
		i = (int)((const unsigned char *)p - (const unsigned char *)utf8);
		int d = direction(cp);
		if (d < 0) {
			d = current >= 0 ? current : (base_rtl ? 1 : 0);
		}
		if (current < 0) {
			current = d;
			start = at;
		} else if (d != current) {
			runs[nruns].text = strndup(utf8 + start, (size_t)(at - start));
			runs[nruns].rtl = current == 1;
			if (!runs[nruns].text || shape_run(&runs[nruns], height, baseline, pixels, bold)) {
				for (int k = 0; k <= nruns; k++) free_run(&runs[k]);
				free(runs);
				return NULL;
			}
			nruns++;
			current = d;
			start = at;
		}
	}
	if (current >= 0 && start < n) {
		runs[nruns].text = strndup(utf8 + start, (size_t)(n - start));
		runs[nruns].rtl = current == 1;
		if (!runs[nruns].text || shape_run(&runs[nruns], height, baseline, pixels, bold)) {
			for (int k = 0; k <= nruns; k++) free_run(&runs[k]);
			free(runs);
			return NULL;
		}
		nruns++;
	}
	int total = 0;
	for (int i = 0; i < nruns; i++) {
		total += runs[i].advance;
	}
	struct plat_text *text = calloc(1, sizeof(*text));
	if (!text) {
		for (int i = 0; i < nruns; i++) free_run(&runs[i]);
		free(runs);
		return NULL;
	}
	text->width = total + 2 * PAD;
	text->height = height;
	text->baseline = baseline;
	text->advance = total;
	text->stride = text->width;
	text->mask = calloc((size_t)text->width * (size_t)height, 1);
	text->utf8 = strdup(utf8);
	text->rtl = base_rtl;
	if (!text->mask || !text->utf8) {
		text_destroy(text);
		for (int i = 0; i < nruns; i++) free_run(&runs[i]);
		free(runs);
		return NULL;
	}
	int x = PAD;
	if (base_rtl) {
		x = PAD + total;
	}
	for (int k = 0; k < nruns; k++) {
		const int i = k;
		if (base_rtl) {
			x -= runs[i].advance;
		}
		for (int y = 0; y < height; y++) {
			for (int dx = 0; dx < runs[i].width; dx++) {
				const int tx = x + dx;
				if (tx >= 0 && tx < text->width &&
						runs[i].mask[y * runs[i].width + dx]) {
					text->mask[y * text->stride + tx] = 255;
				}
			}
		}
		if (!base_rtl) {
			x += runs[i].advance;
		}
	}
	text->ink_l = text->ink_r = -1;
	for (int cx = 0; cx < text->width; cx++) {
		for (int y = 0; y < height; y++) {
			if (text->mask[y * text->stride + cx]) {
				if (text->ink_l < 0) text->ink_l = cx;
				text->ink_r = cx;
				break;
			}
		}
	}
	for (int i = 0; i < nruns; i++) {
		free_run(&runs[i]);
	}
	free(runs);
	return text;
}
