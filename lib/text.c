#include <cairo.h>
#include <stdlib.h>
#include <string.h>

#include "text.h"

/*
 * Stand-in fonts: a fully hinted, non-antialiased DejaVu Sans sized to
 * match the measured metrics of the Mac OS 8 fonts.
 *
 * - System font (Charcoal 12): cap height 9, descender 2 (HIG chapters 4, 5).
 * - Views font (the Finder's Geneva): cap height 8 (HIG figure 2-24).
 */
struct font_spec {
	const char *env;    /* environment override for the family */
	double px;
	bool bold;
	int height, baseline;
};

static const struct font_spec fonts[] = {
	[PL_FONT_SYSTEM] = { "PLATINUM_TITLE_FONT", 12.0, true, 16, 12 },
	[PL_FONT_VIEWS] = { "PLATINUM_VIEWS_FONT", 11.0, false, 15, 11 },
};

static void set_font(cairo_t *cr, const struct font_spec *f) {
	const char *family = getenv(f->env);
	cairo_select_font_face(cr, family ? family : "DejaVu Sans",
		CAIRO_FONT_SLANT_NORMAL, f->bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, f->px);

	cairo_font_options_t *opts = cairo_font_options_create();
	cairo_font_options_set_antialias(opts, CAIRO_ANTIALIAS_NONE);
	cairo_font_options_set_hint_style(opts, CAIRO_HINT_STYLE_FULL);
	cairo_font_options_set_hint_metrics(opts, CAIRO_HINT_METRICS_ON);
	cairo_set_font_options(cr, opts);
	cairo_font_options_destroy(opts);
}

static struct plat_text *render_exact(const char *utf8, const struct font_spec *f) {
	/* Measure with a scratch context. */
	cairo_surface_t *scratch = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
	cairo_t *cr = cairo_create(scratch);
	set_font(cr, f);
	cairo_text_extents_t ext;
	cairo_text_extents(cr, utf8, &ext);
	cairo_destroy(cr);
	cairo_surface_destroy(scratch);

	int width = (int)ext.x_advance + 4;
	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_A8, width, f->height);
	cr = cairo_create(surf);
	set_font(cr, f);
	cairo_move_to(cr, 1, f->baseline);
	cairo_show_text(cr, utf8);
	cairo_destroy(cr);
	cairo_surface_flush(surf);

	struct plat_text *text = calloc(1, sizeof(*text));
	text->width = width;
	text->height = f->height;
	text->baseline = f->baseline;
	text->advance = (int)(ext.x_advance + 0.5);
	text->stride = cairo_image_surface_get_stride(surf);
	text->mask = malloc((size_t)text->stride * f->height);
	memcpy(text->mask, cairo_image_surface_get_data(surf), (size_t)text->stride * f->height);
	cairo_surface_destroy(surf);

	text->ink_l = text->ink_r = -1;
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < f->height; y++) {
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

struct plat_text *text_render_font(const char *utf8, int max_ink_width, enum pl_font font) {
	const struct font_spec *f = &fonts[font];
	struct plat_text *text = render_exact(utf8, f);
	if (ink_width(text) <= max_ink_width) {
		return text;
	}
	text_destroy(text);

	/* Drop trailing characters (whole UTF-8 sequences) until it fits. */
	size_t len = strlen(utf8);
	char *buf = malloc(len + sizeof("…"));
	text = NULL;
	while (len > 0) {
		do {
			len--;
		} while (len > 0 && ((unsigned char)utf8[len] & 0xC0) == 0x80);
		memcpy(buf, utf8, len);
		strcpy(buf + len, "…");
		text = render_exact(buf, f);
		if (ink_width(text) <= max_ink_width) {
			break;
		}
		text_destroy(text);
		text = NULL;
	}
	if (!text) {
		text = render_exact("", f);
	}
	free(buf);
	return text;
}

struct plat_text *text_render(const char *utf8, int max_ink_width) {
	return text_render_font(utf8, max_ink_width, PL_FONT_SYSTEM);
}

void text_destroy(struct plat_text *text) {
	if (text) {
		free(text->mask);
		free(text);
	}
}
