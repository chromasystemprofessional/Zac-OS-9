#include <string.h>

#include "patterns.h"

struct pattern {
	const char *id, *name;
	int width, height;
	const uint32_t *palette;
	const char *const *rows; /* palette index per pixel: 0-9, A-Z, a-z */
};

#include "patterns_data.h"

#define N_PATTERNS ((int)(sizeof(patterns) / sizeof(patterns[0])))

static int palette_index(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'A' && c <= 'Z') {
		return 10 + c - 'A';
	}
	return 36 + c - 'a';
}

int pl_pattern_count(void) {
	return N_PATTERNS;
}

const char *pl_pattern_id(int index) {
	return index >= 0 && index < N_PATTERNS ? patterns[index].id : NULL;
}

const char *pl_pattern_name(int index) {
	return index >= 0 && index < N_PATTERNS ? patterns[index].name : NULL;
}

int pl_pattern_find(const char *id) {
	for (int i = 0; id && i < N_PATTERNS; i++) {
		if (strcmp(patterns[i].id, id) == 0) {
			return i;
		}
	}
	return 0;
}

void pl_pattern_size(int index, int *w, int *h) {
	if (index < 0 || index >= N_PATTERNS) {
		index = 0;
	}
	*w = patterns[index].width;
	*h = patterns[index].height;
}

void pl_pattern_fill(struct pl_canvas *c, int index, int x0, int y0, int x1, int y1) {
	if (index < 0 || index >= N_PATTERNS) {
		index = 0;
	}
	const struct pattern *p = &patterns[index];
	for (int y = y0; y <= y1; y++) {
		const char *row = p->rows[((y % p->height) + p->height) % p->height];
		for (int x = x0; x <= x1; x++) {
			pl_put(c, x, y, p->palette[palette_index(row[((x % p->width) + p->width) % p->width])]);
		}
	}
}
