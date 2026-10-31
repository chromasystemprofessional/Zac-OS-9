#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "font.h"
#include "pfont.h"

static void write_file(const char *path, const char *text) {
	FILE *f = fopen(path, "w");
	assert(f);
	fputs(text, f);
	fclose(f);
}

int main(void) {
	const char *good = "/tmp/zacos9-test-pfont-good.pfont";
	const char *bad = "/tmp/zacos9-test-pfont-bad.pfont";
	write_file(good,
		"; a comment\n"
		"font Test\nheight 3\nbaseline 2\n\n"
		"char U+0042 3 -2\n"    /* B, written after A in the file */
		"#.#\n"
		"###\n"
		"#..\n\n"
		"char U+0041 2 -2\n"
		".#\n"
		"##\n"
		"#.\n\n");
	struct pl_bitmap_font *f = pfont_load(good);
	assert(f);
	assert(strcmp(f->name, "Test") == 0 && f->height == 3 && f->baseline == 2);
	assert(f->n == 2 && f->glyphs[0].cp == 0x41 && f->glyphs[1].cp == 0x42);
	assert(f->glyphs[0].advance == 2 && f->glyphs[0].top == -2 && f->glyphs[0].rows == 3);
	assert(f->rows[f->glyphs[0].first] == 0x2 && f->rows[f->glyphs[0].first + 1] == 0x3);
	assert(f->rows[f->glyphs[1].first] == 0x5);
	write_file(bad, "font Bad\nheight 3\nbaseline 2\n\nchar U+0041 2 -2\n#x\n##\n#.\n");
	assert(pfont_load(bad) == NULL);
	assert(pfont_load("/tmp/zacos9-test-pfont-missing.pfont") == NULL);
	remove(good);
	remove(bad);
	printf("ok    pfont loads, sorts glyphs, rejects malformed files\n");
	return 0;
}
