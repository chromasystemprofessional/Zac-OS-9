/*
 * Font metrics: the ink width of text that appears in the HIG figures
 * must match the figures, so menus, titles and lists lay out exactly as
 * on Mac OS 8. Widths were measured with tools/measure/glyphs.py
 * (docs/reference/zacos9-fonts.md). Needs no figures.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "text.h"

struct sample {
	enum pl_font font;
	const char *text;
	int ink;
	const char *where;
};

static const struct sample samples[] = {
	{ PL_FONT_SYSTEM, "File", 21, "figure 4-1 menu bar" },
	{ PL_FONT_SYSTEM, "Edit", 23, "figure 4-1 menu bar" },
	{ PL_FONT_SYSTEM, "Show Clipboard", 99, "figure 4-1 Edit menu" },
	{ PL_FONT_SYSTEM, "Select All", 58, "figure 4-1 Edit menu" },
	{ PL_FONT_SYSTEM, "Preferences...", 91, "figure 4-1 Edit menu" },
	{ PL_FONT_SYSTEM, "Copy", 30, "figure 4-1 Edit menu" },
	{ PL_FONT_SYSTEM, "Make Alias", 69, "figure 4-3" },
	{ PL_FONT_SYSTEM, "Get Printer Configuration", 164, "figure 4-3" },
	{ PL_FONT_SYSTEM, "Set as Default Printer", 141, "figure 4-3" },
	{ PL_FONT_VIEWS, "External", 39, "figure 2-24" },
	{ PL_FONT_VIEWS, "Internal", 37, "figure 2-24" },
	{ PL_FONT_VIEWS, "Promotion", 49, "figure 2-24" },
	{ PL_FONT_VIEWS, "Vacation", 39, "figure 2-24" },
	{ PL_FONT_VIEWS, "Name", 24, "figure 2-24" },
	{ PL_FONT_VIEWS, "2 items, 494 MB available", 129, "figure 2-24" },
};

/* The first row with ink. */
static int ink_top(const struct plat_text *t) {
	for (int y = 0; y < t->height; y++) {
		for (int x = 0; x < t->width; x++) {
			if (t->mask[y * t->stride + x]) {
				return y;
			}
		}
	}
	return -1;
}

int main(void) {
	int fails = 0;
	for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
		const struct sample *s = &samples[i];
		struct plat_text *t = text_render_font(s->text, 10000, s->font);
		int w = t->ink_l < 0 ? 0 : t->ink_r - t->ink_l + 1;
		bool ok = w == s->ink;
		printf("%s  %-28s ink %3d (%s %d)\n", ok ? "ok  " : "FAIL", s->text, w, s->where, s->ink);
		fails += !ok;
		text_destroy(t);
	}

	/* Cap height 9 / 8 and the baseline rows the layout code relies on. */
	struct {
		enum pl_font font;
		int cap;
	} caps[] = { { PL_FONT_SYSTEM, 9 }, { PL_FONT_VIEWS, 8 } };
	for (size_t i = 0; i < 2; i++) {
		struct plat_text *t = text_render_font("H", 100, caps[i].font);
		int cap = t->baseline - ink_top(t) + 1;
		bool ok = cap == caps[i].cap;
		printf("%s  cap height %d (want %d)\n", ok ? "ok  " : "FAIL", cap, caps[i].cap);
		fails += !ok;
		text_destroy(t);
	}

	/* Italic keeps the advance and slants right above the baseline. */
	struct plat_text *up = text_render_font("Alias", 1000, PL_FONT_VIEWS);
	struct plat_text *it = text_render_font("Alias", 1000, PL_FONT_VIEWS_ITALIC);
	bool ok = up->advance == it->advance && it->ink_r > up->ink_r;
	printf("%s  italic advance %d = %d, slants right\n", ok ? "ok  " : "FAIL", it->advance,
		up->advance);
	fails += !ok;
	text_destroy(up);
	text_destroy(it);

	/* A character outside the fonts still renders (fallback face). */
	struct plat_text *cjk = text_render_font("\xe6\x96\x87", 100, PL_FONT_VIEWS);
	ok = cjk->ink_l >= 0;
	printf("%s  fallback glyph for U+6587\n", ok ? "ok  " : "FAIL");
	fails += !ok;
	text_destroy(cjk);

	/* The fonts for 1.25x, 1.5x and 1.75x screens: a plain stem ("|") is
	 * one width on every row, top to bottom, in each of them, and taller
	 * than at 1x. */
	for (int q = 5; q <= 7; q++) {
		for (enum pl_font f = PL_FONT_SYSTEM; f <= PL_FONT_VIEWS; f++) {
			struct plat_text *one = text_render_font("|", 100, f);
			struct plat_text *l = text_render_scaled(one, q);
			int width = -1, rows = 0;
			bool even = l != NULL;
			for (int y = 0; l && y < l->height; y++) {
				int n = 0;
				for (int x = 0; x < l->width; x++) {
					n += l->mask[y * l->stride + x] != 0;
				}
				if (n) {
					rows++;
					even = even && (width < 0 || n == width);
					width = n;
				}
			}
			int rows1 = 0;
			for (int y = 0; y < one->height; y++) {
				for (int x = 0; x < one->width; x++) {
					if (one->mask[y * one->stride + x]) {
						rows1++;
						break;
					}
				}
			}
			ok = even && rows > rows1;
			printf("%s  at %d/4 the %s font's | is %d px wide on all %d rows\n", ok ? "ok  " : "FAIL", q,
				f == PL_FONT_SYSTEM ? "System" : "Views", width, rows);
			fails += !ok;
			text_destroy(l);
			text_destroy(one);
		}
	}
	struct plat_text *hi = text_render("Hi", 100);
	ok = text_render_scaled(hi, 4) == NULL && text_render_scaled(hi, 8) == NULL;
	printf("%s  no scaled font at whole-number scales\n", ok ? "ok  " : "FAIL");
	fails += !ok;
	/* Italic and truncated text keep what they show. */
	struct plat_text *cut = text_render_font("A long name for a file", 40, PL_FONT_VIEWS_ITALIC);
	struct plat_text *cut15 = text_render_scaled(cut, 6);
	ok = cut->utf8 && strstr(cut->utf8, "\xe2\x80\xa6") && cut15 && strcmp(cut15->utf8, cut->utf8) == 0 &&
		cut15->height > cut->height;
	printf("%s  truncated italic text drawn again at 1.5x as shown (%s)\n", ok ? "ok  " : "FAIL",
		cut->utf8 ? cut->utf8 : "?");
	fails += !ok;
	text_destroy(cut15);
	text_destroy(cut);
	text_destroy(hi);

	printf("%d failure%s\n", fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
