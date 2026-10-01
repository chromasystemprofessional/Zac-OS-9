#include <cairo.h>
#include <stdlib.h>
#include <string.h>

#include "text.h"

/* System-font metrics (HIG figures): cap height 9, descender 2. A 12 px
 * DejaVu Sans Bold, fully hinted, lands on the same cap height. */
#define TEXT_FONT_FAMILY "DejaVu Sans"
#define TEXT_FONT_PX 12.0
#define TEXT_HEIGHT 16
#define TEXT_BASELINE 12

static void set_font(cairo_t *cr) {
	const char *family = getenv("PLATINUM_TITLE_FONT");
	cairo_select_font_face(cr, family ? family : TEXT_FONT_FAMILY,
		CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, TEXT_FONT_PX);

	cairo_font_options_t *opts = cairo_font_options_create();
	cairo_font_options_set_antialias(opts, CAIRO_ANTIALIAS_NONE);
	cairo_font_options_set_hint_style(opts, CAIRO_HINT_STYLE_FULL);
	cairo_font_options_set_hint_metrics(opts, CAIRO_HINT_METRICS_ON);
	cairo_set_font_options(cr, opts);
	cairo_font_options_destroy(opts);
}

static struct plat_text *render_exact(const char *utf8) {
	/* Measure with a scratch context. */
	cairo_surface_t *scratch = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
	cairo_t *cr = cairo_create(scratch);
	set_font(cr);
	cairo_text_extents_t ext;
	cairo_text_extents(cr, utf8, &ext);
	cairo_destroy(cr);
	cairo_surface_destroy(scratch);

	int width = (int)ext.x_advance + 4;
	cairo_surface_t *surf =
		cairo_image_surface_create(CAIRO_FORMAT_A8, width, TEXT_HEIGHT);
	cr = cairo_create(surf);
	set_font(cr);
	cairo_move_to(cr, 1, TEXT_BASELINE);
	cairo_show_text(cr, utf8);
	cairo_destroy(cr);
	cairo_surface_flush(surf);

	struct plat_text *text = calloc(1, sizeof(*text));
	text->width = width;
	text->height = TEXT_HEIGHT;
	text->baseline = TEXT_BASELINE;
	text->stride = cairo_image_surface_get_stride(surf);
	text->mask = malloc((size_t)text->stride * TEXT_HEIGHT);
	memcpy(text->mask, cairo_image_surface_get_data(surf),
		(size_t)text->stride * TEXT_HEIGHT);
	cairo_surface_destroy(surf);

	text->ink_l = text->ink_r = -1;
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < TEXT_HEIGHT; y++) {
			uint8_t *p = &text->mask[y * text->stride + x];
			*p = *p >= 128 ? 255 : 0;
			if (*p) {
				if (text->ink_l < 0) {
					text->ink_l = x;
				}
				text->ink_r = x;
			}
		}
	}
	return text;
}

static int ink_width(const struct plat_text *t) {
	return t->ink_l < 0 ? 0 : t->ink_r - t->ink_l + 1;
}

struct plat_text *text_render(const char *utf8, int max_ink_width) {
	struct plat_text *text = render_exact(utf8);
	if (ink_width(text) <= max_ink_width) {
		return text;
	}
	text_destroy(text);

	/* Drop trailing characters (whole UTF-8 sequences) until it fits. */
	size_t len = strlen(utf8);
	char *buf = malloc(len + sizeof("…"));
	while (len > 0) {
		do {
			len--;
		} while (len > 0 && ((unsigned char)utf8[len] & 0xC0) == 0x80);
		memcpy(buf, utf8, len);
		strcpy(buf + len, "…");
		text = render_exact(buf);
		if (ink_width(text) <= max_ink_width) {
			break;
		}
		text_destroy(text);
		text = NULL;
	}
	if (!text) {
		text = render_exact("");
	}
	free(buf);
	return text;
}

void text_destroy(struct plat_text *text) {
	if (text) {
		free(text->mask);
		free(text);
	}
}
