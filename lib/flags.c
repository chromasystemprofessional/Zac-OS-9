#include "flags.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* The palette is deliberately small and flat: these are 20x13 pixel flags,
 * not national emblems. The black outline matches the list's frame. */
static const uint32_t RED = 0xFFCC0000u;
static const uint32_t BLUE = 0xFF000099u;
static const uint32_t LIGHT_BLUE = 0xFF3333CCu;
static const uint32_t GREEN = 0xFF009933u;
static const uint32_t YELLOW = 0xFFFFCC00u;
static const uint32_t WHITE = 0xFFFFFFFFu;

static bool region_is(const char *locale, const char *region) {
	const char *u = strchr(locale, '_');
	return u && u[1] && u[2] &&
		toupper((unsigned char)u[1]) == region[0] &&
		toupper((unsigned char)u[2]) == region[1];
}

enum pl_flag pl_flag_for_locale(const char *locale) {
	if (!locale || !*locale || strcmp(locale, "C") == 0) {
		return PL_FLAG_GLOBE;
	}
	if (region_is(locale, "CA")) {
		return PL_FLAG_CANADA;
	}
	if (region_is(locale, "GB")) {
		return PL_FLAG_UNITED_KINGDOM;
	}
	if (region_is(locale, "US")) {
		return PL_FLAG_UNITED_STATES;
	}
	if (region_is(locale, "FR")) {
		return PL_FLAG_FRANCE;
	}
	if (region_is(locale, "IT")) {
		return PL_FLAG_ITALY;
	}
	if (region_is(locale, "RU")) {
		return PL_FLAG_RUSSIA;
	}
	if (region_is(locale, "ES")) {
		return PL_FLAG_SPAIN;
	}
	if (region_is(locale, "BR")) {
		return PL_FLAG_BRAZIL;
	}
	if (region_is(locale, "PT")) {
		return PL_FLAG_PORTUGAL;
	}
	if (region_is(locale, "JP")) {
		return PL_FLAG_JAPAN;
	}
	/* A language without a region still has a recognisable home only when
	 * it is one of the listed ones; everything else is a globe. */
	if (strncmp(locale, "en", 2) == 0 && (locale[2] == '\0' || locale[2] == '.' || locale[2] == '@')) {
		return PL_FLAG_UNITED_KINGDOM;
	}
	if (strncmp(locale, "fr", 2) == 0 && (locale[2] == '\0' || locale[2] == '.' || locale[2] == '@')) {
		return PL_FLAG_FRANCE;
	}
	if (strncmp(locale, "it", 2) == 0 && (locale[2] == '\0' || locale[2] == '.' || locale[2] == '@')) {
		return PL_FLAG_ITALY;
	}
	if (strncmp(locale, "ru", 2) == 0 && (locale[2] == '\0' || locale[2] == '.' || locale[2] == '@')) {
		return PL_FLAG_RUSSIA;
	}
	if (strncmp(locale, "es", 2) == 0 && (locale[2] == '\0' || locale[2] == '.' || locale[2] == '@')) {
		return PL_FLAG_SPAIN;
	}
	if (strncmp(locale, "pt", 2) == 0 && (locale[2] == '\0' || locale[2] == '.' || locale[2] == '@')) {
		return PL_FLAG_BRAZIL;
	}
	if (strncmp(locale, "ja", 2) == 0 && (locale[2] == '\0' || locale[2] == '.' || locale[2] == '@')) {
		return PL_FLAG_JAPAN;
	}
	return PL_FLAG_GLOBE;
}

static void circle(struct pl_canvas *c, int cx, int cy, int radius, uint32_t color) {
	for (int dy = -radius; dy <= radius; dy++) {
		for (int dx = -radius; dx <= radius; dx++) {
			if (dx * dx + dy * dy <= radius * radius) {
				pl_put(c, cx + dx, cy + dy, color);
			}
		}
	}
}

static void diamond(struct pl_canvas *c, int cx, int cy, int rx, int ry, uint32_t color) {
	for (int dy = -ry; dy <= ry; dy++) {
		for (int dx = -rx; dx <= rx; dx++) {
			if (abs(dx) * ry + abs(dy) * rx <= rx * ry) {
				pl_put(c, cx + dx, cy + dy, color);
			}
		}
	}
}

void pl_flag_paint(struct pl_canvas *c, int x, int y, enum pl_flag flag) {
	if (!c || flag == PL_FLAG_NONE) {
		return;
	}
	const int right = x + PL_FLAG_W - 1, bottom = y + PL_FLAG_H - 1;
	pl_fill(c, x, y, right, bottom, WHITE);
	switch (flag) {
	case PL_FLAG_CANADA: {
		pl_fill(c, x + 1, y + 1, x + 4, y + 11, RED);
		pl_fill(c, x + 15, y + 1, x + 18, y + 11, RED);
		static const char *const leaf[] = {
			"..###..", ".#####.", "#######", ".#####.", "..###..", "..#.#..", ".##.##.",
		};
		for (int r = 0; r < 7; r++) {
			for (int k = 0; k < 7; k++) {
				if (leaf[r][k] == '#') {
					pl_put(c, x + 6 + k, y + 2 + r, RED);
				}
			}
		}
		break;
	}
	case PL_FLAG_UNITED_KINGDOM:
		pl_fill(c, x + 1, y + 1, x + 18, y + 11, BLUE);
		/* White diagonals, then the narrower red ones. */
		for (int r = 1; r <= 11; r++) {
			const int dx = (r - 1) * 17 / 10;
			pl_fill(c, x + 1 + dx, y + r, x + 2 + dx, y + r, WHITE);
			pl_fill(c, x + 17 - dx, y + r, x + 18 - dx, y + r, WHITE);
		}
		for (int r = 2; r <= 10; r += 2) {
			const int dx = (r - 1) * 17 / 10;
			pl_put(c, x + 1 + dx, y + r, RED);
			pl_put(c, x + 18 - dx, y + r, RED);
		}
		pl_fill(c, x + 7, y + 1, x + 12, y + 11, WHITE);
		pl_fill(c, x + 1, y + 4, x + 18, y + 8, WHITE);
		pl_fill(c, x + 9, y + 1, x + 10, y + 11, RED);
		pl_fill(c, x + 1, y + 6, x + 18, y + 6, RED);
		break;
	case PL_FLAG_UNITED_STATES:
		for (int r = 1; r <= 11; r++) {
			pl_fill(c, x + 1, y + r, x + 18, y + r, r % 2 ? RED : WHITE);
		}
		pl_fill(c, x + 1, y + 1, x + 8, y + 6, BLUE);
		for (int r = 2; r <= 5; r += 2) {
			for (int k = 2; k <= 7; k += 2) {
				pl_put(c, x + k, y + r, WHITE);
			}
		}
		break;
	case PL_FLAG_FRANCE:
		pl_fill(c, x + 1, y + 1, x + 6, y + 11, BLUE);
		pl_fill(c, x + 14, y + 1, x + 18, y + 11, RED);
		break;
	case PL_FLAG_ITALY:
		pl_fill(c, x + 1, y + 1, x + 6, y + 11, GREEN);
		pl_fill(c, x + 14, y + 1, x + 18, y + 11, RED);
		break;
	case PL_FLAG_RUSSIA:
		pl_fill(c, x + 1, y + 4, x + 18, y + 7, BLUE);
		pl_fill(c, x + 1, y + 8, x + 18, y + 11, RED);
		break;
	case PL_FLAG_SPAIN:
		pl_fill(c, x + 1, y + 1, x + 18, y + 3, RED);
		pl_fill(c, x + 1, y + 4, x + 18, y + 9, YELLOW);
		pl_fill(c, x + 1, y + 10, x + 18, y + 11, RED);
		break;
	case PL_FLAG_BRAZIL:
		pl_fill(c, x + 1, y + 1, x + 18, y + 11, GREEN);
		diamond(c, x + 10, y + 6, 8, 5, YELLOW);
		circle(c, x + 10, y + 6, 2, BLUE);
		pl_put(c, x + 9, y + 5, WHITE);
		pl_put(c, x + 10, y + 5, WHITE);
		pl_put(c, x + 11, y + 5, WHITE);
		break;
	case PL_FLAG_PORTUGAL:
		pl_fill(c, x + 1, y + 1, x + 7, y + 11, GREEN);
		pl_fill(c, x + 8, y + 1, x + 18, y + 11, RED);
		circle(c, x + 7, y + 6, 2, YELLOW);
		break;
	case PL_FLAG_JAPAN:
		circle(c, x + 10, y + 6, 3, RED);
		break;
	case PL_FLAG_GLOBE:
		circle(c, x + 10, y + 6, 5, LIGHT_BLUE);
		pl_fill(c, x + 7, y + 4, x + 10, y + 5, GREEN);
		pl_fill(c, x + 9, y + 7, x + 13, y + 8, GREEN);
		pl_fill(c, x + 8, y + 9, x + 10, y + 10, GREEN);
		break;
	case PL_FLAG_NONE:
		break;
	}
	pl_outline(c, x, y, right, bottom, 0xFF000000u);
}
