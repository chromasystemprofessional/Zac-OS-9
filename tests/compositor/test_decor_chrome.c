/*
 * Themed title-bar art: a bundle's chrome keys (bar-middle etc. naming
 * flat .argb tiles) replace the Platinum stripes and boxes, while the
 * box origins, the bar rows and the margins stay exactly where the
 * geometry test pins them. Bundles without chrome keys keep drawing
 * Platinum.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "decor.h"

static void write_argb(const char *path, int w, int h, uint32_t fill) {
	unsigned char rgba[4] = {
		(unsigned char)((fill >> 16) & 0xFF),
		(unsigned char)((fill >> 8) & 0xFF),
		(unsigned char)(fill & 0xFF),
		(unsigned char)((fill >> 24) & 0xFF),
	};
	FILE *f = fopen(path, "wb");
	assert(f);
	unsigned char head[8] = {
		(unsigned char)(w & 0xFF), (unsigned char)((w >> 8) & 0xFF),
		(unsigned char)((w >> 16) & 0xFF), (unsigned char)((w >> 24) & 0xFF),
		(unsigned char)(h & 0xFF), (unsigned char)((h >> 8) & 0xFF),
		(unsigned char)((h >> 16) & 0xFF), (unsigned char)((h >> 24) & 0xFF),
	};
	assert(fwrite(head, 1, sizeof(head), f) == sizeof(head));
	for (int i = 0; i < w * h; i++) {
		assert(fwrite(rgba, 1, sizeof(rgba), f) == sizeof(rgba));
	}
	fclose(f);
}

static void write_file(const char *path, const char *text) {
	FILE *f = fopen(path, "w");
	assert(f);
	assert(fputs(text, f) >= 0);
	fclose(f);
}

static uint32_t *render(const struct decor_state *st, int *w, int *h) {
	*w = st->width + 1;
	*h = (st->collapsed ? DECOR_COLLAPSED_H : st->height) + 1;
	uint32_t *px = calloc((size_t)*w * *h, sizeof(*px));
	struct pl_canvas c = { .px = px, .stride = *w, .x = 0, .y = 0,
		.width = *w, .height = *h };
	decor_paint(&c, st);
	return px;
}

int main(void) {
	char home[] = "/tmp/zacos9-chrome-test-XXXXXX";
	assert(mkdtemp(home));
	char cfg[4096], dir[4096], bundle[4096], chrome[4096], conf[4096], desk[4096];
	snprintf(cfg, sizeof(cfg), "%s/config", home);
	snprintf(dir, sizeof(dir), "%s/config/zacos9", home);
	snprintf(bundle, sizeof(bundle), "%s/config/zacos9/themes/Aqua", home);
	snprintf(chrome, sizeof(chrome), "%s/config/zacos9/themes/Aqua/chrome", home);
	snprintf(conf, sizeof(conf), "%s/config/zacos9/themes/Aqua/theme.conf", home);
	snprintf(desk, sizeof(desk), "%s/config/zacos9/desktop.conf", home);
	assert(mkdir(cfg, 0755) == 0);
	assert(mkdir(dir, 0755) == 0);
	char themes[4096];
	snprintf(themes, sizeof(themes), "%s/config/zacos9/themes", home);
	assert(mkdir(themes, 0755) == 0);
	assert(mkdir(bundle, 0755) == 0);
	assert(mkdir(chrome, 0755) == 0);
	/* Blue bar, gray when inactive, red/bright-red close buttons. */
	char path[8192];
	snprintf(path, sizeof(path), "%s/bar-middle.argb", chrome);
	write_argb(path, 4, 2, 0xFF0000FFu);
	snprintf(path, sizeof(path), "%s/bar-middle-inactive.argb", chrome);
	write_argb(path, 4, 2, 0xFF888888u);
	snprintf(path, sizeof(path), "%s/button-close.argb", chrome);
	write_argb(path, 3, 3, 0xFFFF0000u);
	snprintf(path, sizeof(path), "%s/button-close-pressed.argb", chrome);
	write_argb(path, 3, 3, 0xFF880000u);
	write_file(conf,
		"name=Aqua\n"
		"bar-middle=chrome/bar-middle.argb\n"
		"bar-middle-inactive=chrome/bar-middle-inactive.argb\n"
		"button-close=chrome/button-close.argb\n"
		"button-close-pressed=chrome/button-close-pressed.argb\n");
	write_file(desk, "theme-bundle=Aqua\n");
	setenv("XDG_CONFIG_HOME", cfg, 1);
	setenv("HOME", home, 1);

	struct decor_state st = { .style = DECOR_STYLE_DOCUMENT,
		.width = 300, .height = 120, .active = true,
		.has_close = true, .has_zoom = true, .has_collapse = true,
		.pressed = DECOR_PART_NONE, .title = NULL, .proxy = 0 };
	int w, h;
	uint32_t *px = render(&st, &w, &h);
	/* The themed middle replaces the stripes: solid blue. */
	assert(px[5 * w + 150] == 0xFF0000FFu);
	/* The themed close button replaces the Platinum box: solid red. */
	assert(px[10 * w + 10] == 0xFFFF0000u);
	free(px);

	/* Pressed: the pressed art shows instead. */
	st.pressed = DECOR_PART_CLOSE;
	px = render(&st, &w, &h);
	assert(px[10 * w + 10] == 0xFF880000u);
	free(px);
	st.pressed = DECOR_PART_NONE;

	/* Inactive: the gray bar, still no stripes. */
	st.active = false;
	px = render(&st, &w, &h);
	assert(px[5 * w + 150] == 0xFF888888u);
	free(px);
	st.active = true;

	/* No chrome keys: the Platinum box draws as before. */
	write_file(desk, "theme-bundle=NoSuchTheme\n");
	px = render(&st, &w, &h);
	assert(px[5 * w + 150] != 0xFF0000FFu);
	assert(px[5 * w + 4] == 0xFF888888u);
	free(px);

	/* Two-part bar with a left title cluster: the patch tiles over
	 * the title group, the span across the rest; the zoom and grow
	 * boxes move left after the title. */
	char bundle2[4096], chrome2[4096], conf2[4096];
	snprintf(bundle2, sizeof(bundle2), "%s/config/zacos9/themes/TwoPart", home);
	snprintf(chrome2, sizeof(chrome2), "%s/config/zacos9/themes/TwoPart/chrome", home);
	snprintf(conf2, sizeof(conf2), "%s/config/zacos9/themes/TwoPart/theme.conf", home);
	assert(mkdir(bundle2, 0755) == 0);
	assert(mkdir(chrome2, 0755) == 0);
	snprintf(path, sizeof(path), "%s/bar-middle.argb", chrome2);
	write_argb(path, 4, 22, 0xFFFF0000u);
	snprintf(path, sizeof(path), "%s/bar-span.argb", chrome2);
	write_argb(path, 4, 22, 0xFF0000FFu);
	snprintf(path, sizeof(path), "%s/bar-left.argb", chrome2);
	write_argb(path, 4, 22, 0xFF00FF00u);
	snprintf(path, sizeof(path), "%s/bar-right.argb", chrome2);
	write_argb(path, 4, 22, 0xFFFFFF00u);
	write_file(conf2,
		"name=TwoPart\n"
		"bar-middle=chrome/bar-middle.argb\n"
		"bar-span=chrome/bar-span.argb\n"
		"bar-left=chrome/bar-left.argb\n"
		"bar-right=chrome/bar-right.argb\n"
		"title-cluster=left\n");
	write_file(desk, "theme-bundle=TwoPart\n");
	px = render(&st, &w, &h);
	/* Raised pinstripe tab at the top-left with the end caps hugging
	 * the group (green left, yellow right of it), transparent above
	 * the 7 px window rim to the right of the tab, blue band below. */
	assert(px[5 * w + 2] == 0xFF00FF00u);
	assert(px[5 * w + 22] == 0xFFFF0000u);
	assert(px[5 * w + 38] == 0xFFFF0000u);
	assert(px[5 * w + 58] == 0xFFFFFF00u);
	assert(px[5 * w + 200] == 0x00000000u);
	assert(px[18 * w + 200] == 0xFFDADADAu);
	assert(px[30 * w + 30] == 0xFF0000FFu);
	assert(px[30 * w + 200] == 0xFF0000FFu);
	free(px);
	/* The boxes followed the cluster left: close, zoom, grow in a row
	 * from x=4; the title row past them and the whole band are drag. */
	assert(decor_hit(&st, 10, 10) == DECOR_PART_CLOSE);
	assert(decor_hit(&st, 26, 10) == DECOR_PART_ZOOM);
	assert(decor_hit(&st, 44, 10) == DECOR_PART_COLLAPSE);
	assert(decor_hit(&st, 270, 10) == DECOR_PART_DRAG);
	assert(decor_hit(&st, 200, 30) == DECOR_PART_DRAG);
	/* Collapsed (windowshade): the title row stays themed. */
	st.collapsed = true;
	px = render(&st, &w, &h);
	assert(px[5 * w + 22] == 0xFFFF0000u);
	assert(px[18 * w + 200] == 0xFFDADADAu);
	assert(decor_hit(&st, 10, 10) == DECOR_PART_CLOSE);
	free(px);
	st.collapsed = false;

	/* Dialogs use their own pinstripe bar and close box, even though
	 * Platinum dialogs show none. */
	char bundle3[4096], chrome3[4096], conf3[4096];
	snprintf(bundle3, sizeof(bundle3), "%s/config/zacos9/themes/Dialog", home);
	snprintf(chrome3, sizeof(chrome3), "%s/config/zacos9/themes/Dialog/chrome", home);
	snprintf(conf3, sizeof(conf3), "%s/config/zacos9/themes/Dialog/theme.conf", home);
	assert(mkdir(bundle3, 0755) == 0);
	assert(mkdir(chrome3, 0755) == 0);
	snprintf(path, sizeof(path), "%s/bar-dialog-middle.argb", chrome3);
	write_argb(path, 4, 22, 0xFF00FF00u);
	snprintf(path, sizeof(path), "%s/button-dialog-close.argb", chrome3);
	write_argb(path, 3, 3, 0xFFFFFF00u);
	snprintf(path, sizeof(path), "%s/button-dialog-zoom.argb", chrome3);
	write_argb(path, 3, 3, 0xFF00FFFFu);
	write_file(conf3,
		"name=Dialog\n"
		"bar-dialog-middle=chrome/bar-dialog-middle.argb\n"
		"button-dialog-close=chrome/button-dialog-close.argb\n"
		"button-dialog-zoom=chrome/button-dialog-zoom.argb\n");
	write_file(desk, "theme-bundle=Dialog\n");
	st.style = DECOR_STYLE_MOVABLE_MODAL;
	st.has_close = st.has_zoom = st.has_collapse = false;
	px = render(&st, &w, &h);
	assert(px[5 * w + 200] == 0xFF00FF00u);
	assert(decor_hit(&st, 10, 10) == DECOR_PART_CLOSE);
	assert(decor_hit(&st, 270, 10) == DECOR_PART_ZOOM);
	assert(decor_hit(&st, 200, 10) == DECOR_PART_DRAG);
	free(px);

	printf("decor-chrome: ok\n");
	return 0;
}
