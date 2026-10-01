/*
 * Font metrics: the ink width of text that appears in the HIG figures
 * must match the figures, so menus, titles and lists lay out exactly as
 * on Mac OS 8. Widths were measured with tools/measure/glyphs.py
 * (docs/reference/platinum-fonts.md). Needs no figures.
 */
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

	printf("%d failure%s\n", fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
