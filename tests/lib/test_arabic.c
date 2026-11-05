#include <assert.h>
#include <fontconfig/fontconfig.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "shaping.h"
#include "text.h"

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

int main(void) {
	const char *arabic = "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85"; /* سلام */
	const char *mixed_rtl = "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 Sniffer";
	const char *mixed_ltr = "Sniffer \xd8\xb3\xd9\x84\xd8\xa7\xd9\x85";

	assert(pl_has_arabic(arabic));
	assert(pl_has_arabic(mixed_rtl));
	assert(!pl_has_arabic("Sniffer"));

	if (!font_covers(0x627)) {
		puts("skip  no Arabic font installed: shaping not checked");
		return 77;
	}

	struct plat_text *t = text_render_font(arabic, 1000, PL_FONT_SYSTEM);
	assert(t && t->rtl && t->ink_l >= 0 && t->advance > 0);
	const int shaped_advance = t->advance;
	text_destroy(t);

	/* Joined Arabic is narrower than the same letters drawn separately. */
	int separate = 0;
	for (const char *p = arabic; *p;) {
		const int n = (*p & 0x80) == 0 ? 1 : (*p & 0xE0) == 0xC0 ? 2 :
			(*p & 0xF0) == 0xE0 ? 3 : 4;
		char one[5] = { 0 };
		memcpy(one, p, (size_t)n);
		struct plat_text *c = text_render_font(one, 1000, PL_FONT_SYSTEM);
		separate += c ? c->advance : 0;
		text_destroy(c);
		p += n;
	}
	assert(shaped_advance < separate);

	t = text_render_font(mixed_rtl, 1000, PL_FONT_SYSTEM);
	assert(t && t->rtl && t->ink_l >= 0);
	text_destroy(t);
	t = text_render_font(mixed_ltr, 1000, PL_FONT_SYSTEM);
	assert(t && !t->rtl && t->ink_l >= 0);
	text_destroy(t);
	t = text_render_font("Sniffer", 1000, PL_FONT_SYSTEM);
	assert(t && !t->rtl && t->ink_l >= 0);
	text_destroy(t);

	puts("ok: Arabic joins, reads right-to-left, and keeps Latin runs");
	return 0;
}
