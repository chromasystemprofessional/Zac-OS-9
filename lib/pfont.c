/*
 * A bitmap font from a .pfont file in a theme bundle (fonts/system.pfont,
 * fonts/views.pfont): the format tools/fonts/build_fonts.py compiles, read at
 * run time. The Mac OS 9 fonts are converted to it by the theme extractor.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "pfont.h"

#define PFONT_LINE 256

static int glyph_order(const void *a, const void *b) {
	const struct pl_glyph *x = a, *y = b;
	return (x->cp > y->cp) - (x->cp < y->cp);
}

/* A code point from a glyph line: U+XXXX, "space", or one character. */
static int parse_cp(const char *word, uint32_t *cp) {
	if (strcmp(word, "space") == 0) {
		*cp = ' ';
		return 1;
	}
	if (strncmp(word, "U+", 2) == 0) {
		char *end = NULL;
		unsigned long v = strtoul(word + 2, &end, 16);
		if (end == word + 2 || *end || v > 0x10FFFF) {
			return 0;
		}
		*cp = (uint32_t)v;
		return 1;
	}
	if (word[0] && !((unsigned char)word[0] & 0x80) && word[1] == '\0') {
		*cp = (unsigned char)word[0];
		return 1;
	}
	return 0;
}

struct pl_bitmap_font *pfont_load(const char *path) {
	FILE *f = fopen(path, "r");
	if (!f) {
		return NULL;
	}
	char line[PFONT_LINE];
	char name[PFONT_LINE] = "Theme";
	int height = 0, baseline = 0;
	struct pl_glyph *glyphs = NULL;
	uint32_t *rows = NULL;
	int n = 0, glyph_cap = 0, row_count = 0, row_cap = 0;
	struct pl_glyph *cur = NULL;
	int cur_rows_left = 0;
	int ok = 1;

	while (ok && fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = '\0';
		char *p = line;
		while (*p && isspace((unsigned char)*p)) {
			p++;
		}
		if (*p == '\0' || *p == ';') {
			cur_rows_left = 0;
			continue;
		}
		if (cur_rows_left > 0) {
			size_t len = strlen(p);
			if (len != cur->advance || len > 32 || strspn(p, "#.") != len) {
				ok = 0;
				break;
			}
			uint32_t bits = 0;
			for (size_t x = 0; x < len; x++) {
				if (p[x] == '#') {
					bits |= 1u << x;
				}
			}
			if (row_count == row_cap) {
				row_cap = row_cap ? row_cap * 2 : 1024;
				rows = realloc(rows, sizeof(uint32_t) * row_cap);
			}
			rows[row_count++] = bits;
			cur->rows++;
			cur_rows_left--;
			continue;
		}
		if (strncmp(p, "font ", 5) == 0) {
			snprintf(name, sizeof(name), "%s", p + 5);
		} else if (strncmp(p, "height ", 7) == 0) {
			height = atoi(p + 7);
		} else if (strncmp(p, "baseline ", 9) == 0) {
			baseline = atoi(p + 9);
		} else if (strncmp(p, "char ", 5) == 0) {
			char word[64], advance[16], top[16];
			uint32_t cp;
			if (sscanf(p + 5, "%63s %15s %15s", word, advance, top) != 3 || !parse_cp(word, &cp)) {
				ok = 0;
				break;
			}
			int adv = atoi(advance), t = atoi(top);
			if (adv < 1 || adv > 32 || t < -128 || t > 127) {
				ok = 0;
				break;
			}
			if (n == glyph_cap) {
				glyph_cap = glyph_cap ? glyph_cap * 2 : 256;
				glyphs = realloc(glyphs, sizeof(struct pl_glyph) * glyph_cap);
			}
			glyphs[n] = (struct pl_glyph){ .cp = cp, .top = (int8_t)t, .advance = (uint8_t)adv,
				.rows = 0, .first = (uint16_t)row_count };
			cur = &glyphs[n++];
			cur_rows_left = height;
		}
	}
	fclose(f);
	if (!ok || height <= 0 || baseline < 0 || baseline > height || n == 0 || row_count > 65535) {
		free(glyphs);
		free(rows);
		return NULL;
	}
	qsort(glyphs, n, sizeof(struct pl_glyph), glyph_order);
	struct pl_bitmap_font *font = calloc(1, sizeof(*font));
	font->name = strdup(name);
	font->height = height;
	font->baseline = baseline;
	font->glyphs = glyphs;
	font->n = n;
	font->rows = rows;
	return font;
}
