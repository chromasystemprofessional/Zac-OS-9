#include <stdlib.h>
#include <string.h>

#include "icons.h"

/* The icon set: assets/icons/platinum-icons.picon, compiled by
 * tools/icons/build_icons.py. */
#include "icons_data.h"

static uint32_t *decode(const char *const *rows, int size) {
	uint32_t *px = calloc((size_t)size * size, sizeof(*px));
	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			px[y * size + x] = icon_color(rows[y][x]);
		}
	}
	return px;
}

const uint32_t *pl_icon(enum pl_icon_kind kind, int size) {
	static uint32_t *cache[PL_ICON_COUNT][2];
	int s = size >= 32 ? 0 : 1;
	if (kind < 0 || kind >= PL_ICON_COUNT) {
		kind = PL_ICON_DOCUMENT;
	}
	if (!cache[kind][s]) {
		cache[kind][s] = decode(icon_rows[kind][s], s == 0 ? 32 : 16);
	}
	return cache[kind][s];
}

enum pl_icon_kind pl_icon_for_program(const char *desktop_id) {
	static const struct {
		const char *id;
		enum pl_icon_kind kind;
	} programs[] = {
		{ "zacos9-barkrecorder", PL_ICON_APP_BARKRECORDER },
		{ "zacos9-calculator", PL_ICON_APP_CALCULATOR },
		{ "zacos9-dogears", PL_ICON_APP_DOGEARS },
		{ "zacos9-jigsaw", PL_ICON_APP_JIGSAW },
		{ "zacos9-keytags", PL_ICON_APP_KEYTAGS },
		{ "zacos9-notepad", PL_ICON_APP_NOTEPAD },
		{ "zacos9-pedigree", PL_ICON_APP_PEDIGREE },
		{ "zacos9-plotter", PL_ICON_APP_PLOTTER },
		{ "zacos9-stash", PL_ICON_APP_STASH },
	};
	if (!desktop_id) {
		return PL_ICON_APPLICATION;
	}
	const char *base = strrchr(desktop_id, '/');
	base = base ? base + 1 : desktop_id;
	const size_t len = strcspn(base, ".");
	for (size_t i = 0; i < sizeof(programs) / sizeof(programs[0]); i++) {
		if (strlen(programs[i].id) == len && strncmp(base, programs[i].id, len) == 0) {
			return programs[i].kind;
		}
	}
	return PL_ICON_APPLICATION;
}

const struct pl_label pl_labels[PL_LABEL_COUNT] = {
	{ "None", 0 },
	{ "Essential", 0xFFFF6403 },
	{ "Hot", 0xFFDD0806 },
	{ "In Progress", 0xFFF20884 },
	{ "Cool", 0xFF02ABEA },
	{ "Personal", 0xFF0000D4 },
	{ "Project 1", 0xFF1FB714 },
	{ "Project 2", 0xFF562C05 },
};

/* Each channel scaled by the label's: white becomes the label color and
 * black stays black. TODO: compare with a real Mac's labelled icons. */
static uint32_t tint(uint32_t v, uint32_t label) {
	uint32_t out = 0xFF000000u;
	for (int shift = 0; shift < 24; shift += 8) {
		uint32_t a = (v >> shift) & 0xFF, b = (label >> shift) & 0xFF;
		out |= (a * b / 255) << shift;
	}
	return out;
}

void pl_icon_paint(struct pl_canvas *c, int x, int y, enum pl_icon_kind kind,
		int size, bool selected) {
	pl_icon_paint_label(c, x, y, kind, size, selected, 0);
}

void pl_icon_paint_label(struct pl_canvas *c, int x, int y, enum pl_icon_kind kind,
		int size, bool selected, uint32_t label_color) {
	const uint32_t *px = pl_icon(kind, size);
	for (int j = 0; j < size; j++) {
		for (int i = 0; i < size; i++) {
			uint32_t v = px[j * size + i];
			if (!(v >> 24)) {
				continue;
			}
			if (label_color) {
				v = tint(v, label_color);
			}
			if (selected) {
				v = 0xFF000000u | ((v >> 1) & 0x7F7F7F);
			}
			pl_put(c, x + i, y + j, v);
		}
	}
}
