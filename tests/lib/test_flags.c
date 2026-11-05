#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "flags.h"

static bool has(const struct pl_canvas *c, uint32_t color) {
	for (int y = 0; y < c->height; y++) {
		for (int x = 0; x < c->width; x++) {
			if (c->px[y * c->stride + x] == color) {
				return true;
			}
		}
	}
	return false;
}

int main(void) {
	uint32_t px[PL_FLAG_W * PL_FLAG_H];
	struct pl_canvas c = {
		.px = px, .stride = PL_FLAG_W, .x = 0, .y = 0,
		.width = PL_FLAG_W, .height = PL_FLAG_H, .log = NULL,
	};
	const uint32_t red = 0xFFCC0000u, blue = 0xFF000099u, green = 0xFF009933u,
		yellow = 0xFFFFCC00u;

	assert(pl_flag_for_locale("en_CA.UTF-8") == PL_FLAG_CANADA);
	assert(pl_flag_for_locale("en_GB.UTF-8") == PL_FLAG_UNITED_KINGDOM);
	assert(pl_flag_for_locale("en_US.UTF-8") == PL_FLAG_UNITED_STATES);
	assert(pl_flag_for_locale("fr_CA.UTF-8") == PL_FLAG_CANADA);
	assert(pl_flag_for_locale("fr_FR.UTF-8") == PL_FLAG_FRANCE);
	assert(pl_flag_for_locale("pt_BR.UTF-8") == PL_FLAG_BRAZIL);
	assert(pl_flag_for_locale("pt_PT.UTF-8") == PL_FLAG_PORTUGAL);
	assert(pl_flag_for_locale("de_DE.UTF-8") == PL_FLAG_GLOBE);
	assert(pl_flag_for_locale("C") == PL_FLAG_GLOBE);

	memset(px, 0, sizeof(px));
	pl_flag_paint(&c, 0, 0, PL_FLAG_CANADA);
	assert(has(&c, red) && has(&c, 0xFFFFFFFFu));

	memset(px, 0, sizeof(px));
	pl_flag_paint(&c, 0, 0, PL_FLAG_UNITED_KINGDOM);
	assert(has(&c, blue) && has(&c, red));

	memset(px, 0, sizeof(px));
	pl_flag_paint(&c, 0, 0, PL_FLAG_FRANCE);
	assert(has(&c, blue) && has(&c, red) && has(&c, 0xFFFFFFFFu));

	memset(px, 0, sizeof(px));
	pl_flag_paint(&c, 0, 0, PL_FLAG_BRAZIL);
	assert(has(&c, green) && has(&c, yellow) && has(&c, blue));

	memset(px, 0, sizeof(px));
	pl_flag_paint(&c, 0, 0, PL_FLAG_JAPAN);
	assert(has(&c, red) && has(&c, 0xFFFFFFFFu));

	memset(px, 0, sizeof(px));
	pl_flag_paint(&c, 0, 0, PL_FLAG_GLOBE);
	assert(has(&c, 0xFF3333CCu) && has(&c, green));

	puts("ok: flags map locales and draw inside their 20x13 cell");
	return 0;
}
