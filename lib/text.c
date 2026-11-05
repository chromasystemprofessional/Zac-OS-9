#include <stdbool.h>
#include <cairo.h>
#include <fontconfig/fontconfig.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "draw.h"
#include "font.h"
#include "pfont.h"
#include "text.h"
#include "theme.h"
#include "shaping.h"

#include "fonts_data.h"

/*
 * Text is drawn with our own bitmap fonts (assets/fonts), which carry the
 * measured Mac OS 8 metrics. A character they don't have (an accented letter
 * outside Latin-1, a Greek or Cyrillic name, Japanese kana and kanji) falls
 * back to a non-antialiased outline glyph, sized to sit on the same baseline.
 * Fontconfig finds the face: DejaVu Sans for European scripts, a Japanese
 * face (fonts-noto-cjk) for CJK. docs/i18n.md has the rules.
 */

struct style {
	const struct pl_bitmap_font *font;
	bool italic;
	/* Fallback TrueType face for missing characters. */
	double fallback_px;
	bool fallback_bold;
	int quarters; /* a 1.25x, 1.5x or 1.75x screen's font (5, 6, 7); 0 at 1x */
};

static struct style styles[] = {
	[PL_FONT_SYSTEM] = { &font_system, false, 12.0, true, 0 },
	[PL_FONT_VIEWS] = { &font_views, false, 11.0, false, 0 },
	[PL_FONT_VIEWS_ITALIC] = { &font_views, true, 11.0, false, 0 },
};

/* The fonts for 1.25x, 1.5x and 1.75x screens, by quarters - 5. */
static const struct style scaled_styles[3][3] = {
	{
		[PL_FONT_SYSTEM] = { &font_system_125, false, 15.0, true, 5 },
		[PL_FONT_VIEWS] = { &font_views_125, false, 13.75, false, 5 },
		[PL_FONT_VIEWS_ITALIC] = { &font_views_125, true, 13.75, false, 5 },
	},
	{
		[PL_FONT_SYSTEM] = { &font_system_150, false, 18.0, true, 6 },
		[PL_FONT_VIEWS] = { &font_views_150, false, 16.5, false, 6 },
		[PL_FONT_VIEWS_ITALIC] = { &font_views_150, true, 16.5, false, 6 },
	},
	{
		[PL_FONT_SYSTEM] = { &font_system_175, false, 21.0, true, 7 },
		[PL_FONT_VIEWS] = { &font_views_175, false, 19.25, false, 7 },
		[PL_FONT_VIEWS_ITALIC] = { &font_views_175, true, 19.25, false, 7 },
	},
};

/* ---- theme window-chrome fonts ---------------------------------------------- */

/* The extractor rasterizes the theme's own faces (fonts/theme-*.pfont)
 * from its TrueType at the text sizes, each at 1x and the fractional
 * scales; while a theme is active the chrome uses them instead of the
 * built-in bitmaps. A file they lack (or a broken one) falls back per
 * glyph at draw time, so partial coverage stays readable. */
static const int theme_sizes[2][4] = {
	{ 12, 15, 18, 21 }, /* system */
	{ 11, 14, 17, 19 }, /* views */
};
static const char *theme_roles[2] = { "system", "views" };

struct theme_pfont {
	char *path; /* resolved file, or NULL when absent/broken */
	struct timespec mtime;
	off_t size;
	struct pl_bitmap_font *font; /* NULL when the slot holds nothing usable */
};

static struct theme_pfont theme_pfonts[2][4];

static void theme_pfont_free(struct pl_bitmap_font *f) {
	if (!f) {
		return;
	}
	free((void *)f->glyphs);
	free((void *)f->rows);
	free((void *)f->name);
	free(f);
}

static void theme_slot_clear(int family, int scale) {
	free(theme_pfonts[family][scale].path);
	theme_pfonts[family][scale].path = NULL;
	theme_pfonts[family][scale].mtime = (struct timespec){ 0 };
	theme_pfonts[family][scale].size = 0;
	theme_pfont_free(theme_pfonts[family][scale].font);
	theme_pfonts[family][scale].font = NULL;
}

/* First code point of a NUL-terminated token; -1 unless it is exactly one. */
static uint32_t next_cp(const char **s);
static int token_cp(const char *token) {
	const char *s = token;
	uint32_t cp = next_cp(&s);
	if (cp == 0xFFFD && strcmp(token, "\xEF\xBF\xBD") != 0) {
		return -1;
	}
	return *s ? -1 : (int)cp;
}

static int glyph_cmp(const void *a, const void *b) {
	const struct pl_glyph *ga = a, *gb = b;
	return ga->cp < gb->cp ? -1 : ga->cp > gb->cp;
}

/* Parse a .pfont file (assets/fonts format) into a heap font. NULL on any
 * failure; a file with some bad glyphs still loads the good ones. */
static struct pl_bitmap_font *parse_pfont(const char *path) {
	FILE *f = fopen(path, "r");
	if (!f) {
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	rewind(f);
	if (size <= 0 || size > 1024 * 1024) {
		fclose(f);
		return NULL;
	}
	char *text = malloc((size_t)size + 1);
	if (!text) {
		fclose(f);
		return NULL;
	}
	if (fread(text, 1, (size_t)size, f) != (size_t)size) {
		free(text);
		fclose(f);
		return NULL;
	}
	text[size] = '\0';
	fclose(f);

	int height = 0, baseline = -1;
	char *name = NULL;
	struct pl_glyph *glyphs = NULL;
	int nglyphs = 0, capglyphs = 0;
	char **rowstrings = NULL;
	int nrows = 0, caprows = 0;
	/* Pending glyph (or mark, when pending_mark) its rows belong to. */
	bool pending = false, pending_mark = false;
	uint32_t pending_cp = 0;
	int pending_adv = 0, pending_top = 0;
	int pending_first = 0; /* index into rowstrings of its first row */

	struct pl_bitmap_font *fail = NULL;
#define PFONT_FAIL() do { \
		free(text); free(name); free(glyphs); \
		if (rowstrings) { for (int _i = 0; _i < nrows; _i++) free(rowstrings[_i]); free(rowstrings); } \
		return fail; \
	} while (0)

	char *lsave = NULL;
	for (char *line = strtok_r(text, "\n", &lsave); line;
			line = strtok_r(NULL, "\n", &lsave)) {
		size_t len = strlen(line);
		while (len && (line[len - 1] == '\r')) {
			line[--len] = '\0';
		}
		char *stripped = line;
		while (*stripped == ' ' || *stripped == '\t') {
			stripped++;
		}
		if (!*stripped || *stripped == ';') {
			continue;
		}
		bool is_row = true;
		for (const char *p = stripped; *p; p++) {
			if (*p != '#' && *p != '.') {
				is_row = false;
				break;
			}
		}
		if (is_row) {
			if (!pending || pending_mark) {
				continue;
			}
			if (nrows >= caprows) {
				int ncap = caprows ? caprows * 2 : 64;
				char **nr = realloc(rowstrings, sizeof(*nr) * (size_t)ncap);
				if (!nr) {
					PFONT_FAIL();
				}
				rowstrings = nr;
				caprows = ncap;
			}
			rowstrings[nrows] = strdup(stripped);
			if (!rowstrings[nrows]) {
				PFONT_FAIL();
			}
			nrows++;
			continue;
		}
		/* A new directive ends the pending glyph: keep it when its shape
		 * is sane (advance, row count, no duplicate). Whether it fits
		 * the line box is checked at the end, so the header may come
		 * in any order. */
		if (pending && !pending_mark) {
			int count = nrows - pending_first;
			bool ok = pending_adv > 0 && pending_adv <= 32 && count > 0 && count <= 255 &&
				pending_top >= -128 && pending_top <= 127;
			if (ok) {
				for (int i = 0; i < nglyphs; i++) {
					if (glyphs[i].cp == pending_cp) {
						ok = false;
						break;
					}
				}
			}
			if (ok) {
				if (nglyphs >= capglyphs) {
					int ncap = capglyphs ? capglyphs * 2 : 64;
					struct pl_glyph *ng = realloc(glyphs, sizeof(*ng) * (size_t)ncap);
					if (!ng) {
						PFONT_FAIL();
					}
					glyphs = ng;
					capglyphs = ncap;
				}
				glyphs[nglyphs].cp = pending_cp;
				glyphs[nglyphs].top = (int8_t)pending_top;
				glyphs[nglyphs].advance = (uint8_t)pending_adv;
				glyphs[nglyphs].rows = (uint8_t)count;
				glyphs[nglyphs].first = (uint16_t)pending_first;
				nglyphs++;
			} else {
				for (int i = pending_first; i < nrows; i++) {
					free(rowstrings[i]);
				}
				nrows = pending_first;
			}
		}
		pending = false;
		pending_mark = false;

		char *words[5];
		int nwords = 0;
		char *wsave = NULL;
		for (char *w = strtok_r(stripped, " \t", &wsave); w && nwords < 5;
				w = strtok_r(NULL, " \t", &wsave)) {
			words[nwords++] = w;
		}
		/* NB: strtok_r on `stripped` has its own save pointer, so the
		 * outer line loop above is unaffected. */
		if (nwords == 0) {
			continue;
		}
		if (strcmp(words[0], "char") == 0) {
			if (nwords != 4) {
				continue;
			}
			uint32_t cp;
			if (strcmp(words[1], "space") == 0) {
				cp = 32;
			} else if (strncmp(words[1], "U+", 2) == 0) {
				char *end = NULL;
				unsigned long v = strtoul(words[1] + 2, &end, 16);
				if (!end || *end || v == 0 || v > 0x10FFFF) {
					continue;
				}
				cp = (uint32_t)v;
			} else {
				int v = token_cp(words[1]);
				if (v < 0) {
					continue;
				}
				cp = (uint32_t)v;
			}
			pending = true;
			pending_cp = cp;
			pending_adv = atoi(words[2]);
			pending_top = atoi(words[3]);
			pending_first = nrows;
		} else if (strcmp(words[0], "mark") == 0) {
			pending = true;
			pending_mark = true;
			pending_first = nrows;
		} else if (strcmp(words[0], "font") == 0) {
			/* Name runs to end of line; strtok ate spaces, so rejoined
			 * with single spaces (good enough for debugging). */
			free(name);
			size_t nlen = 0;
			for (int i = 1; i < nwords; i++) {
				nlen += strlen(words[i]) + 1;
			}
			name = malloc(nlen + 1);
			if (!name) {
				PFONT_FAIL();
			}
			name[0] = '\0';
			for (int i = 1; i < nwords; i++) {
				if (i > 1) {
					strcat(name, " ");
				}
				strcat(name, words[i]);
			}
		} else if (nwords == 2) {
			int v = atoi(words[1]);
			if (strcmp(words[0], "height") == 0) {
				height = v;
			} else if (strcmp(words[0], "baseline") == 0) {
				baseline = v;
			}
		}
	}
	/* Flush a trailing glyph (same shape checks as above). */
	if (pending && !pending_mark) {
		int count = nrows - pending_first;
		bool ok = pending_adv > 0 && pending_adv <= 32 && count > 0 && count <= 255 &&
			pending_top >= -128 && pending_top <= 127;
		if (ok) {
			for (int i = 0; i < nglyphs; i++) {
				if (glyphs[i].cp == pending_cp) {
					ok = false;
					break;
				}
			}
		}
		if (ok) {
			if (nglyphs >= capglyphs) {
				int ncap = capglyphs ? capglyphs * 2 : 64;
				struct pl_glyph *ng = realloc(glyphs, sizeof(*ng) * (size_t)ncap);
				if (!ng) {
					PFONT_FAIL();
				}
				glyphs = ng;
				capglyphs = ncap;
			}
			glyphs[nglyphs].cp = pending_cp;
			glyphs[nglyphs].top = (int8_t)pending_top;
			glyphs[nglyphs].advance = (uint8_t)pending_adv;
			glyphs[nglyphs].rows = (uint8_t)count;
			glyphs[nglyphs].first = (uint16_t)pending_first;
			nglyphs++;
		} else {
			for (int i = pending_first; i < nrows; i++) {
				free(rowstrings[i]);
			}
			nrows = pending_first;
		}
	} else if (pending && pending_mark) {
		for (int i = pending_first; i < nrows; i++) {
			free(rowstrings[i]);
		}
		nrows = pending_first;
	}
	free(text);

	if (height <= 0 || height > 256 || baseline < 0 || baseline >= height || nglyphs == 0) {
		free(name);
		free(glyphs);
		for (int i = 0; i < nrows; i++) {
			free(rowstrings[i]);
		}
		free(rowstrings);
		return NULL;
	}
	qsort(glyphs, (size_t)nglyphs, sizeof(*glyphs), glyph_cmp);
	uint32_t *rows = calloc((size_t)nrows, sizeof(*rows));
	if (!rows) {
		free(name);
		free(glyphs);
		for (int i = 0; i < nrows; i++) {
			free(rowstrings[i]);
		}
		free(rowstrings);
		return NULL;
	}
	/* Rows were stored per glyph in file order; repack in sorted order,
	 * dropping glyphs outside the line box (an accent above the ascent:
	 * the fallback face covers them, as build_fonts.py does). */
	uint32_t *packed = calloc((size_t)nrows, sizeof(*packed));
	uint16_t *remap = malloc(sizeof(*remap) * (size_t)nglyphs);
	if (!packed || !remap) {
		free(name);
		free(glyphs);
		free(rows);
		free(packed);
		free(remap);
		for (int i = 0; i < nrows; i++) {
			free(rowstrings[i]);
		}
		free(rowstrings);
		return NULL;
	}
	int at = 0, kept = 0;
	for (int i = 0; i < nglyphs; i++) {
		int top = glyphs[i].top, count = glyphs[i].rows;
		if (top < -baseline || top + count - 1 > height - baseline - 1) {
			continue;
		}
		if (kept != i) {
			glyphs[kept] = glyphs[i];
		}
		remap[kept] = (uint16_t)at;
		int first = glyphs[kept].first;
		for (int r = 0; r < count; r++) {
			const char *s = rowstrings[first + r];
			uint32_t bits = 0;
			for (int x = 0; x < glyphs[kept].advance && s[x]; x++) {
				if (s[x] == '#') {
					bits |= 1u << x;
				}
			}
			packed[at++] = bits;
		}
		kept++;
	}
	if (kept == 0) {
		free(name);
		free(glyphs);
		free(rows);
		free(packed);
		free(remap);
		for (int i = 0; i < nrows; i++) {
			free(rowstrings[i]);
		}
		free(rowstrings);
		return NULL;
	}
	nglyphs = kept;
	for (int i = 0; i < nglyphs; i++) {
		glyphs[i].first = remap[i];
	}
	free(remap);
	for (int i = 0; i < nrows; i++) {
		free(rowstrings[i]);
	}
	free(rowstrings);
	free(rows);

	struct pl_bitmap_font *font = malloc(sizeof(*font));
	if (!font) {
		free(name);
		free(glyphs);
		free(packed);
		return NULL;
	}
	font->name = name ? name : strdup("theme");
	font->height = height;
	font->baseline = baseline;
	font->glyphs = glyphs;
	font->n = nglyphs;
	font->rows = packed;
	return font;
#undef PFONT_FAIL
}

/* The theme's font for a family/scale, or NULL (built-in holds). Reloads
 * when the bundle changes or the file is replaced. */
static const struct pl_bitmap_font *theme_font(int family, int scale) {
	char relative[64];
	snprintf(relative, sizeof(relative), "fonts/theme-%s-%d.pfont",
		theme_roles[family], theme_sizes[family][scale]);
	char path[4096];
	if (!zac_theme_asset(relative, path, sizeof(path))) {
		theme_slot_clear(family, scale);
		return NULL;
	}
	struct stat st;
	if (stat(path, &st) != 0) {
		theme_slot_clear(family, scale);
		return NULL;
	}
	struct theme_pfont *slot = &theme_pfonts[family][scale];
	if (slot->path && strcmp(slot->path, path) == 0 &&
			slot->mtime.tv_sec == st.st_mtim.tv_sec &&
			slot->mtime.tv_nsec == st.st_mtim.tv_nsec && slot->size == st.st_size) {
		return slot->font;
	}
	theme_slot_clear(family, scale);
	slot->font = parse_pfont(path);
	slot->path = strdup(path);
	if (!slot->path) {
		theme_pfont_free(slot->font);
		slot->font = NULL;
		return NULL;
	}
	slot->mtime = st.st_mtim;
	slot->size = st.st_size;
	return slot->font;
}

/* The effective style: the built-in metrics with the theme's bitmap
 * swapped in when the bundle supplies one. */
static void style_for(enum pl_font font, int quarters, struct style *out) {
	const struct style *base;
	int scale;
	if (quarters >= 5 && quarters <= 7) {
		base = &scaled_styles[quarters - 5][font];
		scale = quarters - 4;
	} else {
		base = &styles[font];
		scale = 0;
	}
	*out = *base;
	int family = font == PL_FONT_SYSTEM ? 0 : 1;
	const struct pl_bitmap_font *themed = theme_font(family, scale);
	if (themed) {
		out->font = themed;
	}
}
/* QuickDraw-style italic: rows shift right one pixel per two rows above
 * the baseline (and left below it); on a scaled font's rows, as its 1x
 * rows there shift, scaled. */
static int italic_shift(int y, int quarters) {
	if (quarters) {
		return pl_snap(italic_shift(pl_unsnap(y, quarters), 0), quarters);
	}
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

/* A fallback face, and the characters it says it has. DejaVu Sans is asked
 * for first, so European letters keep their old look; a code point it lacks
 * is looked up in Fontconfig, which on a Japanese system finds Noto Sans CJK
 * JP. The charsets let later characters skip the Fontconfig query. */
struct fallback_face {
	char *family;
	FcCharSet *charset;
	bool bold;
};

#define FALLBACK_FACES_MAX 8
static struct fallback_face fallback_faces[FALLBACK_FACES_MAX];
static int fallback_face_count;
static bool fallback_faces_ready;

/* Kana, Han, CJK punctuation and full-width forms: scripts for which a
 * Japanese face is the right default. */
static bool cjk_cp(uint32_t cp) {
	return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
		(cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFFEF) ||
		(cp >= 0x20000 && cp <= 0x2FA1F) || (cp >= 0x30000 && cp <= 0x3FFFF);
}

/* Arabic and its supplement/extended blocks: ask Fontconfig for an Arabic
 * face if the shaper could not load one. */
static bool arabic_cp(uint32_t cp) {
	return (cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x0750 && cp <= 0x077F) ||
		(cp >= 0x08A0 && cp <= 0x08FF) || (cp >= 0xFB50 && cp <= 0xFDFF) ||
		(cp >= 0xFE70 && cp <= 0xFEFF);
}

static void remember_face(const char *family, FcCharSet *charset, bool bold) {
	if (!charset || fallback_face_count >= FALLBACK_FACES_MAX) {
		if (charset) {
			FcCharSetDestroy(charset);
		}
		return;
	}
	struct fallback_face *face = &fallback_faces[fallback_face_count++];
	face->family = strdup(family);
	face->charset = charset;
	face->bold = bold;
}

/* Ask Fontconfig for a named face and remember what it actually matched. */
static void remember_family(const char *family, bool bold) {
	FcPattern *pat = FcPatternCreate();
	FcPatternAddString(pat, FC_FAMILY, (const FcChar8 *)family);
	FcPatternAddInteger(pat, FC_WEIGHT, bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
	FcPatternAddBool(pat, FC_SCALABLE, FcTrue);
	FcConfigSubstitute(NULL, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);
	FcResult result;
	FcPattern *match = FcFontMatch(NULL, pat, &result);
	FcPatternDestroy(pat);
	if (!match) {
		return;
	}
	FcCharSet *charset = NULL;
	FcChar8 *matched = NULL;
	if (FcPatternGetCharSet(match, FC_CHARSET, 0, &charset) == FcResultMatch &&
			FcPatternGetString(match, FC_FAMILY, 0, &matched) == FcResultMatch && matched) {
		remember_face((const char *)matched, FcCharSetCopy(charset), bold);
	}
	FcPatternDestroy(match);
}

/* Ask Fontconfig for a face that covers `cp`. FcFontMatch can return the
 * default face even when it lacks the character, so the sorted list is
 * searched for the first face whose charset really covers it. A CJK
 * character asks for a Japanese face first, then for any face at all, so a
 * Chinese or Korean font is better than a box. */
static const struct fallback_face *find_face(uint32_t cp, bool bold) {
	const char *langs[] = { cjk_cp(cp) ? "ja" : arabic_cp(cp) ? "ar" : NULL, NULL };
	for (size_t i = 0; i < sizeof(langs) / sizeof(langs[0]); i++) {
		if (i > 0 && !langs[0]) {
			break; /* not CJK: one query, without a language */
		}
		FcPattern *pat = FcPatternCreate();
		FcPatternAddInteger(pat, FC_WEIGHT, bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
		FcPatternAddBool(pat, FC_SCALABLE, FcTrue);
		if (langs[i]) {
			FcPatternAddString(pat, FC_LANG, (const FcChar8 *)langs[i]);
		}
		FcConfigSubstitute(NULL, pat, FcMatchPattern);
		FcDefaultSubstitute(pat);
		FcResult result;
		FcFontSet *set = FcFontSort(NULL, pat, FcFalse, NULL, &result);
		FcPatternDestroy(pat);
		if (!set) {
			continue;
		}
		for (int f = 0; f < set->nfont; f++) {
			FcPattern *font = set->fonts[f];
			FcCharSet *charset = NULL;
			FcChar8 *family = NULL;
			if (FcPatternGetCharSet(font, FC_CHARSET, 0, &charset) != FcResultMatch ||
					!FcCharSetHasChar(charset, cp) ||
					FcPatternGetString(font, FC_FAMILY, 0, &family) != FcResultMatch ||
					!family) {
				continue;
			}
			const int before = fallback_face_count;
			remember_face((const char *)family, FcCharSetCopy(charset), bold);
			FcFontSetDestroy(set);
			return fallback_face_count > before ? &fallback_faces[before] : NULL;
		}
		FcFontSetDestroy(set);
	}
	return NULL;
}

static const struct fallback_face *fallback_face_for(uint32_t cp, bool bold) {
	if (!fallback_faces_ready) {
		fallback_faces_ready = true;
		remember_family("DejaVu Sans", false);
		remember_family("DejaVu Sans", true);
	}
	for (int i = 0; i < fallback_face_count; i++) {
		if (fallback_faces[i].bold == bold && fallback_faces[i].charset &&
				FcCharSetHasChar(fallback_faces[i].charset, cp)) {
			return &fallback_faces[i];
		}
	}
	const struct fallback_face *found = find_face(cp, bold);
	if (found) {
		return found;
	}
	/* Nothing covers it: DejaVu draws its .notdef box, as it always did. */
	for (int i = 0; i < fallback_face_count; i++) {
		if (fallback_faces[i].bold == bold) {
			return &fallback_faces[i];
		}
	}
	return fallback_face_count ? &fallback_faces[0] : NULL;
}

struct fallback {
	int width, advance;
	uint8_t *mask; /* the line box's height, `width` wide; pen at column 0 */
};

static void fallback_font(cairo_t *cr, const struct style *st,
		const struct fallback_face *face) {
	cairo_select_font_face(cr, face ? face->family : "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL,
		st->fallback_bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, st->fallback_px);
	cairo_font_options_t *opts = cairo_font_options_create();
	cairo_font_options_set_antialias(opts, CAIRO_ANTIALIAS_NONE);
	cairo_font_options_set_hint_style(opts, CAIRO_HINT_STYLE_FULL);
	cairo_font_options_set_hint_metrics(opts, CAIRO_HINT_METRICS_ON);
	cairo_set_font_options(cr, opts);
	cairo_font_options_destroy(opts);
}

static struct fallback render_fallback(uint32_t cp, const struct style *st,
		const struct fallback_face *face) {
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
	fallback_font(cr, st, face);
	cairo_text_extents_t ext;
	cairo_text_extents(cr, utf8, &ext);
	cairo_destroy(cr);
	cairo_surface_destroy(scratch);

	struct fallback fb = { .advance = (int)(ext.x_advance + 0.5) };
	fb.width = fb.advance + 2;
	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_A8, fb.width, h);
	cr = cairo_create(surf);
	fallback_font(cr, st, face);
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

	/* Arabic joins and reads right-to-left; the bitmap path below draws one
	 * isolated glyph per codepoint, so hand the whole line to the shaper. */
	if (pl_has_arabic(utf8)) {
		struct plat_text *shaped = pl_shape_arabic(utf8, f->height, f->baseline,
			(int)(st->fallback_px + 0.5), st->fallback_bold);
		if (shaped) {
			return shaped;
		}
	}

	/* Measure: the pen starts at column 1, as callers expect. */
	int advance = 0;
	for (const char *s = utf8; *s;) {
		uint32_t cp = next_cp(&s);
		const struct pl_glyph *g = find_glyph(f, cp);
		if (g) {
			advance += g->advance;
		} else {
			const struct fallback_face *face = fallback_face_for(cp, st->fallback_bold);
			struct fallback fb = render_fallback(cp, st, face);
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
	text->utf8 = strdup(utf8);

	int pen = 1;
	for (const char *s = utf8; *s;) {
		uint32_t cp = next_cp(&s);
		const struct pl_glyph *g = find_glyph(f, cp);
		if (g) {
			for (int r = 0; r < g->rows; r++) {
				const int y = g->top + r;
				const int row = f->baseline + y;
				const int dx = st->italic ? italic_shift(y, st->quarters) : 0;
				const uint32_t bits = f->rows[g->first + r];
				for (int x = 0; x < 32; x++) {
					const int col = pen + x + dx;
					if ((bits >> x) & 1 && col >= 0 && col < text->width) {
						text->mask[row * text->stride + col] = 255;
					}
				}
			}
			pen += g->advance;
			continue;
		}
		const struct fallback_face *face = fallback_face_for(cp, st->fallback_bold);
		struct fallback fb = render_fallback(cp, st, face);
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

static struct plat_text *render(const char *utf8, int max_ink_width, enum pl_font font);

struct plat_text *text_render_font(const char *utf8, int max_ink_width, enum pl_font font) {
	struct plat_text *text = render(utf8, max_ink_width, font);
	if (text) {
		text->font = font;
	}
	return text;
}

struct plat_text *text_render_scaled(const struct plat_text *text, int quarters) {
	if (!text || !text->utf8 || quarters < 5 || quarters > 7 || text->font < 0 || text->font > 2) {
		return NULL;
	}
	struct style st;
	style_for(text->font, quarters, &st);
	struct plat_text *scaled = render_exact(text->utf8, &st);
	if (scaled) {
		scaled->font = text->font;
	}
	return scaled;
}

/* The active theme's own system and views fonts (its fonts folder's .pfont files)
 * replace ZacOS's at 1x, loaded once per program; a missing or malformed file
 * keeps ZacOS's font. The scaled fonts stay ZacOS's. */
static void theme_fonts_once(void) {
	static bool done = false;
	if (done) {
		return;
	}
	done = true;
	char path[4096];
	if (zac_theme_asset("fonts/system.pfont", path, sizeof(path))) {
		struct pl_bitmap_font *f = pfont_load(path);
		if (f) {
			styles[PL_FONT_SYSTEM].font = f;
		}
	}
	if (zac_theme_asset("fonts/views.pfont", path, sizeof(path))) {
		struct pl_bitmap_font *f = pfont_load(path);
		if (f) {
			styles[PL_FONT_VIEWS].font = f;
			styles[PL_FONT_VIEWS_ITALIC].font = f;
		}
	}
}

/* The pseudo-locale check (docs/i18n.md) sets this: a string that had to be
 * cut is a translation that no longer fits, and this names the string to fix. */
static bool text_strict(void) {
	const char *v = getenv("ZACOS9_TEXT_STRICT");
	return v && *v && strcmp(v, "0") != 0;
}

static struct plat_text *render(const char *utf8, int max_ink_width, enum pl_font font) {
	theme_fonts_once();
	struct style storage;
	style_for(font, 0, &storage);
	const struct style *st = &storage;
	struct plat_text *text = render_exact(utf8, st);
	if (ink_width(text) <= max_ink_width) {
		return text;
	}
	if (text_strict()) {
		fprintf(stderr, "zacos9: text truncated to %d px: \"%s\"\n", max_ink_width, utf8);
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
		free(text->utf8);
		free(text->mask);
		free(text);
	}
}
