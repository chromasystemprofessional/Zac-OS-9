/*
 * Font metrics: the ink width of text that appears in the HIG figures
 * must match the figures, so menus, titles and lists lay out exactly as
 * on Mac OS 8. Widths were measured with tools/measure/glyphs.py
 * (docs/reference/zacos9-fonts.md). Needs no figures.
 */
#include <assert.h>
#include <fontconfig/fontconfig.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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

/* Whether any installed face claims a code point. */
static bool font_covers(uint32_t cp) {
	FcPattern *pat = FcPatternCreate();
	FcConfigSubstitute(NULL, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);
	FcResult result;
	FcFontSet *set = FcFontSort(NULL, pat, FcFalse, NULL, &result);
	bool found = false;
	for (int i = 0; set && i < set->nfont && !found; i++) {
		FcCharSet *charset = NULL;
		if (FcPatternGetCharSet(set->fonts[i], FC_CHARSET, 0, &charset) == FcResultMatch) {
			found = FcCharSetHasChar(charset, cp);
		}
	}
	if (set) {
		FcFontSetDestroy(set);
	}
	FcPatternDestroy(pat);
	return found;
}

static bool same_mask(const struct plat_text *a, const struct plat_text *b) {
	if (!a || !b || a->width != b->width || a->height != b->height ||
			a->baseline != b->baseline) {
		return false;
	}
	for (int y = 0; y < a->height; y++) {
		if (memcmp(a->mask + y * a->stride, b->mask + y * b->stride, (size_t)a->width) != 0) {
			return false;
		}
	}
	return true;
}

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

/* The last row with ink. */
static int ink_bottom(const struct plat_text *t) {
	for (int y = t->height - 1; y >= 0; y--) {
		for (int x = 0; x < t->width; x++) {
			if (t->mask[y * t->stride + x]) {
				return y;
			}
		}
	}
	return -1;
}

/* One glyph's ink is the other's, rows reversed: Spanish's inverted marks
 * are drawn by the bitmap face rather than left to the outline fallback. */
static bool mirrored_ink(const struct plat_text *upright, const struct plat_text *inverted) {
	if (!upright || !inverted || upright->width != inverted->width) {
		return false;
	}
	const int ut = ink_top(upright), ub = ink_bottom(upright);
	const int it = ink_top(inverted), ib = ink_bottom(inverted);
	if (ut < 0 || it < 0 || ub - ut != ib - it) {
		return false;
	}
	for (int y = 0; y <= ub - ut; y++) {
		if (memcmp(upright->mask + (size_t)(ut + y) * upright->stride,
				inverted->mask + (size_t)(ib - y) * inverted->stride,
				(size_t)upright->width) != 0) {
			return false;
		}
	}
	return true;
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

	/* Spanish inverted punctuation is part of the bitmap faces, so it
	 * matches the upright mark instead of using DejaVu's outline. */
	for (enum pl_font f = PL_FONT_SYSTEM; f <= PL_FONT_VIEWS; f++) {
		struct plat_text *question = text_render_font("?", 100, f);
		struct plat_text *questiondown = text_render_font("\xc2\xbf", 100, f);
		struct plat_text *exclaim = text_render_font("!", 100, f);
		struct plat_text *exclaimdown = text_render_font("\xc2\xa1", 100, f);
		bool ok = mirrored_ink(question, questiondown) && mirrored_ink(exclaim, exclaimdown);
		printf("%s  inverted Spanish marks are drawn by the bitmap face\n", ok ? "ok  " : "FAIL");
		fails += !ok;
		text_destroy(question);
		text_destroy(questiondown);
		text_destroy(exclaim);
		text_destroy(exclaimdown);
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

	/* A character outside the fonts still renders (fallback face). A Han
	 * character must be a real glyph, not DejaVu's .notdef box: a private-use
	 * code point nothing covers draws that box, so the two masks must differ.
	 * The ISO installs fonts-noto-cjk; a machine with no CJK font skips. */
	struct plat_text *cjk = text_render_font("\xe6\x96\x87", 100, PL_FONT_VIEWS);
	ok = cjk && cjk->ink_l >= 0;
	printf("%s  fallback glyph for U+6587\n", ok ? "ok  " : "FAIL");
	fails += !ok;
	if (font_covers(0x6587)) {
		struct plat_text *box = text_render_font("\xf4\x8f\xbf\xbd", 100, PL_FONT_VIEWS);
		ok = cjk && box && !same_mask(cjk, box) && cjk->advance >= 9;
		printf("%s  the fallback is a CJK glyph, not .notdef\n", ok ? "ok  " : "FAIL");
		fails += !ok;
		struct plat_text *cjk15 = text_render_scaled(cjk, 6);
		ok = cjk15 && cjk15->ink_l >= 0 && cjk15->height > cjk->height &&
			cjk15->width > cjk->width;
		printf("%s  the CJK glyph draws again at 1.5x\n", ok ? "ok  " : "FAIL");
		fails += !ok;
		text_destroy(cjk15);
		text_destroy(box);
	} else {
		printf("skip  no CJK font installed: glyph fallback not checked\n");
	}
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

	/* A theme bundle's rasterized faces replace the built-ins while it is
	 * active; a broken file falls back to them. */
	{
		char cfg[] = "/tmp/zactextXXXXXX";
		assert(mkdtemp(cfg) != NULL);
		char bundle[256], fonts[256], conf[256], desk[256];
		snprintf(bundle, sizeof(bundle), "%s/zacos9/themes/Wide", cfg);
		snprintf(fonts, sizeof(fonts), "%s/zacos9/themes/Wide/fonts", cfg);
		{
			char tmp[256];
			snprintf(tmp, sizeof(tmp), "%s", fonts);
			for (char *p = tmp + 1; *p; p++) {
				if (*p == '/') {
					*p = '\0';
					mkdir(tmp, 0755);
					*p = '/';
				}
			}
			mkdir(tmp, 0755);
		}
		snprintf(conf, sizeof(conf), "%s/zacos9/themes/Wide/theme.conf", cfg);
		FILE *cf = fopen(conf, "w");
		assert(cf);
		fputs("name=Wide\n", cf);
		fclose(cf);
		/* Every letter 10 px wide and fully inked: "File" is 40 wide. */
		snprintf(conf, sizeof(conf), "%s/zacos9/themes/Wide/fonts/theme-system-12.pfont", cfg);
		FILE *pf = fopen(conf, "w");
		assert(pf);
		fputs("font Wide\nheight 16\nbaseline 12\n\n", pf);
		for (const char *g = "File"; *g; g++) {
			fprintf(pf, "char %c 10 -8\n", *g);
			for (int r = 0; r < 9; r++) {
				fputs("##########\n", pf);
			}
			fputs("\n", pf);
		}
		fputs("char space 10 0\n\n", pf);
		fclose(pf);
		snprintf(desk, sizeof(desk), "%s/zacos9/desktop.conf", cfg);
		FILE *df = fopen(desk, "w");
		assert(df);
		fputs("theme-bundle=Wide\n", df);
		fclose(df);
		const char *old_cfg = getenv("XDG_CONFIG_HOME");
		const char *old_home = getenv("HOME");
		char *save_cfg = old_cfg ? strdup(old_cfg) : NULL;
		char *save_home = old_home ? strdup(old_home) : NULL;
		setenv("XDG_CONFIG_HOME", cfg, 1);
		setenv("HOME", cfg, 1);

		struct plat_text *wide = text_render_font("File", 10000, PL_FONT_SYSTEM);
		ok = wide && wide->advance == 40 && wide->ink_r - wide->ink_l + 1 == 40 &&
			wide->height == 16 && wide->baseline == 12;
		printf("%s  theme window font replaces the built-in (adv %d)\n", ok ? "ok  " : "FAIL",
			wide ? wide->advance : -1);
		fails += !ok;
		text_destroy(wide);
		/* Views has no theme file here: still the built-in. */
		struct plat_text *views = text_render_font("External", 10000, PL_FONT_VIEWS);
		ok = views && views->ink_r - views->ink_l + 1 == 39;
		printf("%s  fonts the theme lacks stay built-in\n", ok ? "ok  " : "FAIL");
		fails += !ok;
		text_destroy(views);

		/* A broken theme font falls back instead of breaking text. */
		snprintf(conf, sizeof(conf), "%s/zacos9/themes/Wide/fonts/theme-system-12.pfont", cfg);
		pf = fopen(conf, "w");
		assert(pf);
		fputs("not a font\n", pf);
		fclose(pf);
		struct plat_text *plain = text_render_font("File", 10000, PL_FONT_SYSTEM);
		ok = plain && plain->advance == 23;
		printf("%s  broken theme font falls back to built-in\n", ok ? "ok  " : "FAIL");
		fails += !ok;
		text_destroy(plain);

		if (save_cfg) {
			setenv("XDG_CONFIG_HOME", save_cfg, 1);
		} else {
			unsetenv("XDG_CONFIG_HOME");
		}
		if (save_home) {
			setenv("HOME", save_home, 1);
		} else {
			unsetenv("HOME");
		}
		free(save_cfg);
		free(save_home);
		snprintf(conf, sizeof(conf), "%s/zacos9/themes/Wide/fonts/theme-system-12.pfont", cfg);
		unlink(conf);
		snprintf(conf, sizeof(conf), "%s/zacos9/themes/Wide/theme.conf", cfg);
		unlink(conf);
		rmdir(fonts);
		rmdir(bundle);
		snprintf(conf, sizeof(conf), "%s/zacos9/themes", cfg);
		rmdir(conf);
		snprintf(conf, sizeof(conf), "%s/zacos9", cfg);
		unlink(desk);
		rmdir(conf);
		rmdir(cfg);
	}

	printf("%d failure%s\n", fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
