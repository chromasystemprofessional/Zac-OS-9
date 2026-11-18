/*
 * Pixel tests for Platinum controls against HIG figure 2-24 (Finder list
 * view windows, lavender accent).
 *
 * usage: test-widgets FIGS_DIR
 */
#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "figcompare.h"
#include "settings.h"
#include "text.h"
#include "widgets.h"

/* Horizontal scroll bar along the bottom of each Finder window: from the
 * content edge (x=5 / x=285) to where it meets the vertical bar, thumb at
 * the far left. */
static int run_hbar(const char *dir, const char *name, int fig_x, int *ran) {
	const struct pl_scrollbar sb = { .length = 254, .enabled = true, .thumb = 0 };
	int w = sb.length, h = SB_WIDTH;
	uint32_t *px = calloc((size_t)w * h, sizeof(*px));
	struct pl_canvas c = { .px = px, .stride = w, .width = w, .height = h };
	pl_scrollbar_paint(&c, 0, 0, &sb, PL_ACCENT_DEFAULT);
	/* The bottom border row is the window's well line; compare all of it. */
	const struct fig_rect regions[] = { { 0, 0, w, h } };
	int fails = fig_compare(name, dir, "img-038-058.png", fig_x, 153, px, w, h, regions, 1);
	free(px);
	if (fails >= 0) {
		*ran = 1;
	}
	return fails > 0 ? fails : 0;
}

/* Push buttons in HIG figure 3-2: "OK" (standard, 60 wide at 223,161) and
 * "Cancel" (default, ring at 220,125). The label area is excluded. */
static int run_button(const char *dir, const char *name, unsigned flags,
		int fig_x, int fig_y, int *ran) {
	const int bw = 60, ring = (flags & PL_BUTTON_DEFAULT) ? PL_BUTTON_RING : 0;
	const int w = bw + 2 * ring, h = PL_BUTTON_H + 2 * ring;
	uint32_t *px = calloc((size_t)w * h, sizeof(*px));
	/* The dialog background behind the transparent corners. */
	for (int i = 0; i < w * h; i++) {
		px[i] = GRAY(0xD);
	}
	struct pl_canvas c = { .px = px, .stride = w, .width = w, .height = h };
	pl_button_paint(&c, ring, ring, bw, NULL, flags);
	const struct fig_rect regions[] = {
		{ 0, 0, w, ring + 4 },                     /* top edge, ring */
		{ 0, h - ring - 4, w, ring + 4 },          /* bottom edge, ring */
		{ 0, 0, ring + 10, h },                    /* left end */
		{ w - ring - 9, 0, ring + 9, h },          /* right end */
	};
	int fails = fig_compare(name, dir, "img-059-088.png", fig_x, fig_y, px, w, h, regions, 4);
	free(px);
	if (fails >= 0) {
		*ran = 1;
	}
	return fails > 0 ? fails : 0;
}

static int run_hit_tests(void) {
	const struct pl_scrollbar sb = { .length = 254, .enabled = true, .thumb = 40 };
	int fails = 0;
	struct { int along; enum sb_part want; } cases[] = {
		{ 0, SB_NONE }, { 5, SB_DEC_ARROW }, { 30, SB_DEC_PAGE },
		{ 60, SB_THUMB_PART }, { 120, SB_INC_PAGE }, { 245, SB_INC_ARROW },
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		enum sb_part got = sb_hit(&sb, cases[i].along);
		if (got != cases[i].want) {
			fprintf(stderr, "FAIL sb_hit(%d) = %d, want %d\n",
				cases[i].along, got, cases[i].want);
			fails++;
		}
	}
	printf("%s scroll bar hit testing\n", fails ? "FAIL" : "ok  ");
	return fails;
}

/* Smart Scrolling: arrows together at the far end, a longer thumb. */
static int run_smart_tests(void) {
	struct pl_scrollbar sb = { .length = 254, .enabled = true, .thumb = 40,
		.together = true, .thumb_len = 60 };
	int fails = 0;
	struct { int along; enum sb_part want; } cases[] = {
		{ 0, SB_NONE }, { 5, SB_DEC_PAGE }, { 60, SB_THUMB_PART }, { 150, SB_INC_PAGE },
		{ 223, SB_NONE }, { 230, SB_DEC_ARROW }, { 245, SB_INC_ARROW },
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		enum sb_part got = sb_hit(&sb, cases[i].along);
		if (got != cases[i].want) {
			fprintf(stderr, "FAIL smart sb_hit(%d) = %d, want %d\n",
				cases[i].along, got, cases[i].want);
			fails++;
		}
	}
	if (sb_thumb_span(&sb) != 223 - 61) {
		fprintf(stderr, "FAIL smart span %d\n", sb_thumb_span(&sb));
		fails++;
	}
	/* The setting on, half of the content shown: half the track. */
	char dir[] = "/tmp/zacos9-widgets-XXXXXX";
	if (mkdtemp(dir)) {
		char path[sizeof(dir) + 32];
		snprintf(path, sizeof(path), "%s/zacos9", dir);
		mkdir(path, 0700);
		snprintf(path, sizeof(path), "%s/zacos9/desktop.conf", dir);
		FILE *f = fopen(path, "w");
		fputs("smart-scrolling=true\n", f);
		fclose(f);
		setenv("XDG_CONFIG_HOME", dir, 1);
		struct pl_scrollbar half = { .length = 254, .enabled = true };
		sb_smart(&half, 50, 100);
		if (!half.together || half.thumb_len != 222 / 2) {
			fprintf(stderr, "FAIL sb_smart: together %d, thumb %d\n", half.together, half.thumb_len);
			fails++;
		}
		unsetenv("XDG_CONFIG_HOME");
		unlink(path);
		snprintf(path, sizeof(path), "%s/zacos9", dir);
		rmdir(path);
		rmdir(dir);
	}
	printf("%s smart scrolling layout and thumb size\n", fails ? "FAIL" : "ok  ");
	return fails;
}

/* Theme control faces: a bundle's chrome/button.png, checkbox-*.png and
 * radio-*.png replace the Platinum marks while their geometry stays put. */
static void write_skin_png(const char *path, int w, int h, uint32_t argb) {
	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	unsigned char *data = cairo_image_surface_get_data(surface);
	const int stride = cairo_image_surface_get_stride(surface);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			memcpy(data + y * stride + x * 4, &argb, 4);
		}
	}
	cairo_surface_mark_dirty(surface);
	cairo_surface_write_to_png(surface, path);
	cairo_surface_destroy(surface);
}

/* A red-to-blue gradient across the stretchable axis. Tiling it shows a
 * repeating pattern; stretching it runs once from red to blue. */
static void write_gradient_png(const char *path, int w, int h, int horizontal) {
	cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	unsigned char *data = cairo_image_surface_get_data(surface);
	const int stride = cairo_image_surface_get_stride(surface);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			const int t = horizontal ? x * 255 / (w - 1) : y * 255 / (h - 1);
			const uint32_t argb = 0xFF000000u | (uint32_t)(255 - t) << 16 | (uint32_t)t;
			memcpy(data + y * stride + x * 4, &argb, 4);
		}
	}
	cairo_surface_mark_dirty(surface);
	cairo_surface_write_to_png(surface, path);
	cairo_surface_destroy(surface);
}

static int run_skin_test(void) {
	char dir[] = "/tmp/zacos9-widgets-skin-XXXXXX";
	if (!mkdtemp(dir)) {
		return 0;
	}
	char path[sizeof(dir) + 64];
	const char *parts[] = { "zacos9", "zacos9/themes", "zacos9/themes/Test",
		"zacos9/themes/Test/chrome" };
	for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
		snprintf(path, sizeof(path), "%s/%s", dir, parts[i]);
		mkdir(path, 0700);
	}
	snprintf(path, sizeof(path), "%s/zacos9/desktop.conf", dir);
	FILE *f = fopen(path, "w");
	fputs("theme-bundle=Test\n", f);
	fclose(f);
	snprintf(path, sizeof(path), "%s/zacos9/themes/Test/theme.conf", dir);
	f = fopen(path, "w");
	fputs("name=Test\nscroll-arrow-dec=7\nscroll-arrow-inc=8\n", f);
	fclose(f);
	const char *skins[] = { "button", "checkbox-off", "checkbox-on",
		"checkbox-mixed", "radio-off", "radio-on" };
	for (size_t i = 0; i < sizeof(skins) / sizeof(skins[0]); i++) {
		snprintf(path, sizeof(path), "%s/zacos9/themes/Test/chrome/%s.png", dir, skins[i]);
		write_skin_png(path, 12, 12, 0xFF010203u);
	}
	setenv("XDG_CONFIG_HOME", dir, 1);
	int w = 80, h = 30;
	uint32_t *px = calloc((size_t)w * h, sizeof(*px));
	struct pl_canvas c = { .px = px, .stride = w, .width = w, .height = h };
	int fails = 0;
	pl_button_paint(&c, 2, 2, 60, NULL, 0);
	if (px[10 * w + 20] != 0xFF010203u) {
		fprintf(stderr, "FAIL themed button face\n");
		fails++;
	}
	pl_checkbox_paint(&c, 65, 2, PL_CHECK_ON, false, true, NULL);
	if (px[7 * w + 70] != 0xFF010203u) {
		fprintf(stderr, "FAIL themed checkbox\n");
		fails++;
	}
	/* The theme's 7/8 px arrow boxes move the hit boundaries. */
	struct pl_scrollbar sb = { .length = 100, .enabled = true };
	if (sb_hit(&sb, 3) != SB_DEC_ARROW) {
		fprintf(stderr, "FAIL themed decrement arrow hit\n");
		fails++;
	}
	if (sb_hit(&sb, 95) != SB_INC_ARROW) {
		fprintf(stderr, "FAIL themed increment arrow hit\n");
		fails++;
	}
	/* A thumb's middle slice stretches, so a gradient runs once instead
	 * of repeating every slice. */
	snprintf(path, sizeof(path),
		"%s/zacos9/themes/Test/chrome/scroll-thumb-vert-stretch.png", dir);
	write_gradient_png(path, 16, 8, 0);
	snprintf(path, sizeof(path),
		"%s/zacos9/themes/Test/chrome/scroll-thumb-horz-stretch.png", dir);
	write_gradient_png(path, 8, 16, 1);
	int vw = 16, vh = 240;
	uint32_t *vpx = calloc((size_t)vw * vh, sizeof(*vpx));
	struct pl_canvas vc = { .px = vpx, .stride = vw, .width = vw, .height = vh };
	struct pl_scrollbar vsb = { .vertical = true, .length = vh, .enabled = true,
		.thumb = 40, .thumb_len = 60 };
	pl_scrollbar_paint(&vc, 0, 0, &vsb, PL_ACCENT_DEFAULT);
	const uint32_t vtop = vpx[50 * vw + 8], vbottom = vpx[107 * vw + 8];
	if (((vtop >> 16) & 0xFF) < 200 || (vbottom & 0xFF) < 200) {
		fprintf(stderr, "FAIL vertical thumb stretch\n");
		fails++;
	}
	free(vpx);
	int hw = 240, hh = 16;
	uint32_t *hpx = calloc((size_t)hw * hh, sizeof(*hpx));
	struct pl_canvas hc = { .px = hpx, .stride = hw, .width = hw, .height = hh };
	struct pl_scrollbar hsb = { .length = hw, .enabled = true, .thumb = 40,
		.thumb_len = 60 };
	pl_scrollbar_paint(&hc, 0, 0, &hsb, PL_ACCENT_DEFAULT);
	const uint32_t hleft = hpx[8 * hw + 50], hright = hpx[8 * hw + 107];
	if (((hleft >> 16) & 0xFF) < 200 || (hright & 0xFF) < 200) {
		fprintf(stderr, "FAIL horizontal thumb stretch\n");
		fails++;
	}
	free(hpx);
	free(px);
	unsetenv("XDG_CONFIG_HOME");
	printf("%s themed control faces\n", fails ? "FAIL" : "ok  ");
	return fails;
}

/* A themed thumb's caps may be any length: the renderer reads the cap
 * image's along-axis size instead of assuming Platinum's 8 px. */
static int run_thumb_caps_test(void) {
	char dir[] = "/tmp/zacos9-widgets-caps-XXXXXX";
	if (!mkdtemp(dir)) {
		return 0;
	}
	char path[sizeof(dir) + 64];
	const char *parts[] = { "zacos9", "zacos9/themes", "zacos9/themes/Caps",
		"zacos9/themes/Caps/chrome" };
	for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
		snprintf(path, sizeof(path), "%s/%s", dir, parts[i]);
		mkdir(path, 0700);
	}
	snprintf(path, sizeof(path), "%s/zacos9/desktop.conf", dir);
	FILE *f = fopen(path, "w");
	fputs("theme-bundle=Caps\n# cap-length test\n", f);
	fclose(f);
	snprintf(path, sizeof(path), "%s/zacos9/themes/Caps/theme.conf", dir);
	f = fopen(path, "w");
	fputs("name=Caps\n", f);
	fclose(f);
	snprintf(path, sizeof(path), "%s/zacos9/themes/Caps/chrome/scroll-thumb-vert-top.png", dir);
	write_skin_png(path, 16, 3, 0xFFFF0000u);
	snprintf(path, sizeof(path), "%s/zacos9/themes/Caps/chrome/scroll-thumb-vert-stretch.png", dir);
	write_skin_png(path, 16, 4, 0xFF00FF00u);
	snprintf(path, sizeof(path), "%s/zacos9/themes/Caps/chrome/scroll-thumb-vert-bottom.png", dir);
	write_skin_png(path, 16, 3, 0xFF0000FFu);
	setenv("XDG_CONFIG_HOME", dir, 1);
	int w = 16, h = 240;
	uint32_t *px = calloc((size_t)w * h, sizeof(*px));
	struct pl_canvas c = { .px = px, .stride = w, .width = w, .height = h };
	struct pl_scrollbar sb = { .vertical = true, .length = h, .enabled = true,
		.thumb = 40, .thumb_len = 60 };
	pl_scrollbar_paint(&c, 0, 0, &sb, PL_ACCENT_DEFAULT);
	const int t0 = sb_track_start(&sb) + sb.thumb + 1;
	int fails = 0;
	if (px[(t0 + 1) * w + 8] != 0xFFFF0000u) {
		fprintf(stderr, "FAIL thumb top cap is not three pixels\n");
		fails++;
	}
	if (px[(t0 + 5) * w + 8] != 0xFF00FF00u) {
		fprintf(stderr, "FAIL thumb stretch does not start after its cap\n");
		fails++;
	}
	if (px[(t0 + 60 - 2) * w + 8] != 0xFF0000FFu) {
		fprintf(stderr, "FAIL thumb bottom cap is not three pixels\n");
		fails++;
	}
	free(px);
	unsetenv("XDG_CONFIG_HOME");
	printf("%s themed thumb caps\n", fails ? "FAIL" : "ok  ");
	return fails;
}

/* Aqua's thumb art is 14 px across in a 16 px bar, with a 7 px top cap
 * and an 8 px bottom cap. The art is centred inside the frame, and the
 * unequal caps are drawn at their own lengths instead of one guessed
 * size. */
static int run_thumb_inset_test(void) {
	char dir[] = "/tmp/zacos9-widgets-inset-XXXXXX";
	if (!mkdtemp(dir)) {
		return 0;
	}
	char path[sizeof(dir) + 64];
	const char *parts[] = { "zacos9", "zacos9/themes", "zacos9/themes/Inset",
		"zacos9/themes/Inset/chrome" };
	for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
		snprintf(path, sizeof(path), "%s/%s", dir, parts[i]);
		mkdir(path, 0700);
	}
	snprintf(path, sizeof(path), "%s/zacos9/desktop.conf", dir);
	FILE *f = fopen(path, "w");
	fputs("theme-bundle=Inset\n# thumb inset test\n", f);
	fclose(f);
	snprintf(path, sizeof(path), "%s/zacos9/themes/Inset/theme.conf", dir);
	f = fopen(path, "w");
	fputs("name=Inset\n", f);
	fclose(f);
	snprintf(path, sizeof(path), "%s/zacos9/themes/Inset/chrome/scroll-thumb-vert-top.png", dir);
	write_skin_png(path, 14, 7, 0xFFFF0000u);
	snprintf(path, sizeof(path), "%s/zacos9/themes/Inset/chrome/scroll-thumb-vert-stretch.png", dir);
	write_skin_png(path, 14, 1, 0xFF00FF00u);
	snprintf(path, sizeof(path), "%s/zacos9/themes/Inset/chrome/scroll-thumb-vert-bottom.png", dir);
	write_skin_png(path, 14, 8, 0xFF0000FFu);
	setenv("XDG_CONFIG_HOME", dir, 1);
	int w = 16, h = 240;
	uint32_t *px = calloc((size_t)w * h, sizeof(*px));
	struct pl_canvas c = { .px = px, .stride = w, .width = w, .height = h };
	struct pl_scrollbar sb = { .vertical = true, .length = h, .enabled = true,
		.thumb = 40, .thumb_len = 60 };
	pl_scrollbar_paint(&c, 0, 0, &sb, PL_ACCENT_DEFAULT);
	const int t0 = sb_track_start(&sb) + sb.thumb + 1;
	int fails = 0;
	if (px[(t0 + 1) * w + 1] != 0xFFFF0000u ||
			px[(t0 + 1) * w + 14] != 0xFFFF0000u) {
		fprintf(stderr, "FAIL 14 px thumb top cap is not centred\n");
		fails++;
	}
	if (px[(t0 + 1) * w] == 0xFFFF0000u || px[(t0 + 1) * w + 15] == 0xFFFF0000u) {
		fprintf(stderr, "FAIL thumb cap painted over the 1 px frame\n");
		fails++;
	}
	if (px[(t0 + 10) * w + 8] != 0xFF00FF00u) {
		fprintf(stderr, "FAIL thumb stretch does not follow the 7 px top cap\n");
		fails++;
	}
	if (px[(t0 + 60 - 2) * w + 8] != 0xFF0000FFu) {
		fprintf(stderr, "FAIL unequal 8 px bottom cap is not drawn\n");
		fails++;
	}
	free(px);
	unsetenv("XDG_CONFIG_HOME");
	printf("%s themed thumb inset and unequal caps\n", fails ? "FAIL" : "ok  ");
	return fails;
}

/* A titled group box (figure 2-38): the engraved frame, everywhere but
 * the title and the controls inside it. */
static int run_group_box(const char *dir, int *ran) {
	const int w = 170, h = 132;
	uint32_t *px = calloc((size_t)w * h, sizeof(*px));
	struct pl_canvas c = { .px = px, .stride = w, .width = w, .height = h };
	pl_fill(&c, 0, 0, w - 1, h - 1, GRAY(0xD));
	struct plat_text *title = text_render_font("Format", 1000, PL_FONT_SYSTEM);
	pl_group_box_paint(&c, 5, 14, 165, 127, title, GRAY(0xD));
	const struct fig_rect regions[] = {
		{ 0, 0, 14, 132 },      /* left of the title, the left side */
		{ 18 + 39 + 4 + 8, 0, 101, 20 }, /* right of the title (ours is wider), top right */
		{ 160, 0, 10, 132 },    /* the right side */
		{ 0, 120, 170, 12 },    /* the bottom */
	};
	int fails = fig_compare("group box (fig 2-38)", dir, "img-048-073.png", 0, 0, px, w, h,
		regions, 4);
	text_destroy(title);
	free(px);
	if (fails >= 0) {
		*ran = 1;
	}
	return fails > 0 ? fails : 0;
}

/* A focused list box (figure 2-25), first row selected with a yellow
 * highlight as in the figure: ring, frame and highlight edges (not the
 * text or the scroll bar, whose content length the figure doesn't say). */
static int run_list_box(const char *dir, int *ran) {
	const int w = 248, h = 107;
	uint32_t *px = calloc((size_t)w * h, sizeof(*px));
	struct pl_canvas c = { .px = px, .stride = w, .width = w, .height = h };
	pl_fill(&c, 0, 0, w - 1, h - 1, GRAY(0xD));
	const char *names[] = { "Cupertino, U.S.A.", "Dacca, Bangladesh", "Dakar, Senegal",
		"Dallas, U.S.A.", "Damascus, Syria", "Dar es Salaam, Tanzania", "Darwin, Australia" };
	struct plat_text *rows[7];
	for (int i = 0; i < 7; i++) {
		rows[i] = text_render_font(names[i], 1000, PL_FONT_SYSTEM);
	}
	struct pl_list list = { .rows = (const struct plat_text *const *)rows, .n = 7,
		.selected = 0, .top = 0, .focused = true };
	pl_list_paint(&c, 5, 5, 243, 102, &list, PL_ACCENT_DEFAULT, RGB(0xFF, 0xFF, 0x00));
	const struct fig_rect regions[] = {
		{ 0, 0, 248, 7 },     /* ring and top frame */
		{ 0, 0, 8, 107 },     /* ring, frame, highlight's left edge */
		{ 0, 101, 248, 6 },   /* bottom frame and ring */
		{ 226, 0, 22, 6 },    /* where the scroll bar meets the frame */
		{ 243, 0, 5, 107 },   /* right frame and ring */
	};
	int fails = fig_compare("list box (fig 2-25)", dir, "img-039-059.png", 0, 0, px, w, h,
		regions, 5);
	for (int i = 0; i < 7; i++) {
		text_destroy(rows[i]);
	}
	free(px);
	if (fails >= 0) {
		*ran = 1;
	}
	return fails > 0 ? fails : 0;
}

/* The tab control of figure 2-30 ("Protocol" in front), everything but
 * the label text. The figure's greys are shifted; map them. */
static int run_tabs(const char *dir, int *ran) {
	static const uint32_t map[][2] = {
		{ 0xEFEFEF, 0xEEEEEE }, { 0xCECECE, 0xCCCCCC }, { 0xBDBDBD, 0xBBBBBB },
		{ 0xADADAD, 0xAAAAAA }, { 0xDEDEDE, 0xDDDDDD }, { 0x9C9C9C, 0x999999 },
		{ 0x8C8C8C, 0x888888 }, { 0x424242, 0x444444 }, { 0x313131, 0x333333 },
		{ 0x212121, 0x222222 },
	};
	const int w = 303, h = 47;
	uint32_t *px = calloc((size_t)w * h, sizeof(*px));
	struct pl_canvas c = { .px = px, .stride = w, .width = w, .height = h };
	pl_fill(&c, 0, 0, w - 1, h - 1, C_WHITE);
	struct plat_text *labels[3] = {
		text_render_font("Protocol", 1000, PL_FONT_SYSTEM),
		text_render_font("Connection", 1000, PL_FONT_SYSTEM),
		text_render_font("Dialing", 1000, PL_FONT_SYSTEM),
	};
	pl_tabs_paint(&c, 1, 1, 300, 44, (const struct plat_text *const *)labels, 3, 0);
	/* Everything except the label rows inside each tab. */
	struct fig_rect regions[8];
	int nr = 0;
	regions[nr++] = (struct fig_rect){ 0, 0, w, 7 };
	regions[nr++] = (struct fig_rect){ 0, 20, w, h - 20 };
	int prev = 0;
	for (int i = 0; i < 3; i++) {
		int ink = labels[i]->ink_r - labels[i]->ink_l + 1;
		int tx0 = 1 + 4 + 11;
		for (int k = 0; k < i; k++) {
			tx0 += 15 + (labels[k]->ink_r - labels[k]->ink_l + 1) - 1 + 21;
		}
		regions[nr++] = (struct fig_rect){ prev, 7, tx0 + 6 - prev, 13 };
		prev = tx0 + 7 + ink + 1;
	}
	regions[nr++] = (struct fig_rect){ prev, 7, w - prev, 13 };
	fig_set_color_map(map, (int)(sizeof(map) / sizeof(map[0])));
	int fails = fig_compare("tabs (fig 2-30)", dir, "img-043-065.png", 0, 0, px, w, h,
		regions, nr);
	fig_set_color_map(NULL, 0);
	for (int i = 0; i < 3; i++) {
		text_destroy(labels[i]);
	}
	free(px);
	if (fails >= 0) {
		*ran = 1;
	}
	return fails > 0 ? fails : 0;
}

/* Draw into a canvas filled with `bg`, compare the given regions. */
static int compare_drawing(const char *dir, const char *name, const char *fig, int fx, int fy,
		int w, int h, uint32_t bg, void (*draw)(struct pl_canvas *),
		const struct fig_rect *regions, int nr, int *ran) {
	uint32_t *px = calloc((size_t)w * h, sizeof(*px));
	struct pl_canvas c = { .px = px, .stride = w, .width = w, .height = h };
	pl_fill(&c, 0, 0, w - 1, h - 1, bg);
	draw(&c);
	int fails = fig_compare(name, dir, fig, fx, fy, px, w, h, regions, nr);
	free(px);
	if (fails >= 0) {
		*ran = 1;
	}
	return fails > 0 ? fails : 0;
}

static void draw_checked(struct pl_canvas *c) {
	pl_checkbox_paint(c, 0, 0, PL_CHECK_ON, false, true, NULL);
}

static void draw_unchecked(struct pl_canvas *c) {
	pl_checkbox_paint(c, 1, 0, PL_CHECK_OFF, false, true, NULL);
}

static void draw_arrows(struct pl_canvas *c) {
	pl_little_arrows_paint(c, 1, 1, PL_ARROWS_NONE, true);
}

static void draw_clock(struct pl_canvas *c) {
	/* Focus ring around the field and the arrows, then the field frame
	 * (black, white inside), then the arrows. */
	pl_clock_paint(c, 6, 6, 104, true, PL_ARROWS_NONE, true, PL_ACCENT_DEFAULT, GRAY(0xD));
}

static void draw_radio_off(struct pl_canvas *c) {
	pl_radio_paint(c, 0, 0, false, false, true, NULL);
}

static void draw_radio_on(struct pl_canvas *c) {
	pl_radio_paint(c, 0, 0, true, false, true, NULL);
}

static void draw_slider_thumb(struct pl_canvas *c) {
	/* Thumb at the track's left end: its left column is 1 px in. */
	pl_slider_paint(c, -1, 0, 120, 2, 0, true, PL_ACCENT_DEFAULT);
}

static void draw_popup(struct pl_canvas *c) {
	pl_popup_button_paint(c, 0, 0, 130, 19, NULL, true);
}

static void draw_popup_20(struct pl_canvas *c) {
	pl_popup_button_paint(c, 0, 0, 89, PL_POPUP_H, NULL, true);
}

/* Pop-up menu buttons: figure 2-6 (19 tall) pixel for pixel but the
 * text, and the frame, corners and arrows of figure 3-25's 20 px "Size"
 * button, which is drawn in other shades. */
static int run_popups(const char *dir, int *ran) {
	int fails = 0;
	const struct fig_rect fig26[] = {
		{ 0, 0, 130, 3 }, { 0, 16, 130, 3 }, { 0, 0, 5, 19 }, { 101, 0, 29, 19 },
	};
	fails += compare_drawing(dir, "pop-up menu button (fig 2-6)", "img-026-022.png", 0, 0,
		130, 19, C_WHITE, draw_popup, fig26, 4, ran);
	const struct fig_rect fig325[] = {
		{ 0, 0, 89, 3 }, { 0, 17, 89, 3 }, { 0, 0, 3, 20 }, { 66, 0, 23, 20 },
	};
	fig_set_black_only(1);
	fails += compare_drawing(dir, "pop-up menu button, 20 px (fig 3-25)", "img-079-112.png",
		69, 57, 89, PL_POPUP_H, GRAY(0xD), draw_popup_20, fig325, 4, ran);
	fig_set_black_only(0);
	return fails;
}

static int run_small_controls(const char *dir, int *ran) {
	int fails = 0;
	const struct fig_rect box[] = { { 0, 0, 14, 12 } };
	fails += compare_drawing(dir, "checkbox, checked (fig 6-1)", "img-111-136.png", 30, 87,
		14, 12, GRAY(0xE), draw_checked, box, 1, ran);
	const struct fig_rect off[] = { { 1, 0, 11, 12 } };
	fails += compare_drawing(dir, "checkbox, unchecked (fig 2-8)", "img-028-024.png", 0, 0,
		12, 12, GRAY(0xD), draw_unchecked, off, 1, ran);
	const struct fig_rect arrows[] = { { 0, 0, 15, 25 } };
	fails += compare_drawing(dir, "little arrows (fig 2-20)", "img-036-053.png", 0, 0,
		15, 25, C_WHITE, draw_arrows, arrows, 1, ran);
	/* The whole clock control but the date text and its highlight. */
	const struct fig_rect clock[] = {
		{ 0, 0, 133, 8 }, { 0, 0, 16, 35 }, { 0, 24, 133, 11 }, { 90, 0, 43, 35 },
	};
	fails += compare_drawing(dir, "clock control (fig 2-22)", "img-037-055.png",
		0, 0, 133, 35, GRAY(0xD), draw_clock, clock, 4, ran);
	/* Radio buttons in figure 2-4, whose greys are shifted. */
	static const uint32_t radio_map[][2] = {
		{ 0xE3E4E4, 0xDDDDDD }, { 0x565656, 0x555555 }, { 0x686868, 0x666666 },
		{ 0xC7C7C7, 0xCCCCCC }, { 0xF1F1F1, 0xEEEEEE }, { 0x9A9A9A, 0x999999 },
		{ 0xB8B8B8, 0xBBBBBB }, { 0x2D2D2D, 0x333333 }, { 0x8A8A8A, 0x888888 },
		{ 0xA9A9A9, 0xAAAAAA },
	};
	fig_set_color_map(radio_map, (int)(sizeof(radio_map) / sizeof(radio_map[0])));
	const struct fig_rect bead[] = { { 0, 0, 12, 12 } };
	fails += compare_drawing(dir, "radio button, off (fig 2-4)", "img-024-012.png", 37, 100,
		12, 12, GRAY(0xD), draw_radio_off, bead, 1, ran);
	fails += compare_drawing(dir, "radio button, on (fig 2-4)", "img-024-012.png", 37, 122,
		12, 12, GRAY(0xD), draw_radio_on, bead, 1, ran);
	fig_set_color_map(NULL, 0);
	/* The slider's thumb (figure 2-17), down to where the figure's ghost
	 * thumb and pointer overlap it. */
	const struct fig_rect thumb[] = { { 0, 0, 14, 1 }, { 0, 1, 15, 10 } };
	fails += compare_drawing(dir, "slider thumb (fig 2-17)", "img-034-050.png", 21, 13,
		15, 23, GRAY(0xD), draw_slider_thumb, thumb, 2, ran);
	return fails;
}

int main(int argc, char *argv[]) {
	char config_home[128];
	snprintf(config_home, sizeof(config_home), "/tmp/zacos9-widgets-test-%ld", (long)getpid());
	setenv("XDG_CONFIG_HOME", config_home, 1);
	const char *dir = argc > 1 ? argv[1] : "tools/measure/figs";
	int ran = 0;
	int fails = run_hit_tests() + run_smart_tests() + run_skin_test() +
		run_thumb_caps_test() + run_thumb_inset_test();
	fails += run_hbar(dir, "horizontal scroll bar, left window (fig 2-24)", 5, &ran);
	fails += run_button(dir, "standard push button (fig 3-2)", 0, 223, 161, &ran);
	fails += run_button(dir, "default push button (fig 3-2)", PL_BUTTON_DEFAULT, 220, 125, &ran);
	fails += run_group_box(dir, &ran);
	fails += run_list_box(dir, &ran);
	fails += run_tabs(dir, &ran);
	fails += run_small_controls(dir, &ran);
	fails += run_popups(dir, &ran);
	return fig_exit_status(fails, ran);
}
