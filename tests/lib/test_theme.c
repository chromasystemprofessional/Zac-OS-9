#include "theme.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "icons.h"

/* Minimal PNG writer (stored deflate) for fixtures: 8-bit RGBA. */
static uint32_t crc32buf(const unsigned char *p, size_t n) {
	static uint32_t table[256];
	static int made = 0;
	if (!made) {
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++) {
				c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			}
			table[i] = c;
		}
		made = 1;
	}
	uint32_t crc = 0xFFFFFFFFu;
	while (n--) {
		crc = table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
	}
	return crc ^ 0xFFFFFFFFu;
}

static void chunk(FILE *f, const char *type, const unsigned char *data, size_t n) {
	fputc((n >> 24) & 0xFF, f); fputc((n >> 16) & 0xFF, f);
	fputc((n >> 8) & 0xFF, f); fputc(n & 0xFF, f);
	fwrite(type, 1, 4, f);
	if (n) {
		fwrite(data, 1, n, f);
	}
	unsigned char *buf = malloc(4 + n);
	memcpy(buf, type, 4);
	if (n) {
		memcpy(buf + 4, data, n);
	}
	uint32_t c = crc32buf(buf, 4 + n);
	free(buf);
	fputc((c >> 24) & 0xFF, f); fputc((c >> 16) & 0xFF, f);
	fputc((c >> 8) & 0xFF, f); fputc(c & 0xFF, f);
}

static void write_flat_png(const char *path, int size, uint32_t argb) {
	unsigned char px[4] = { (unsigned char)(argb >> 16), (unsigned char)(argb >> 8),
		(unsigned char)argb, (unsigned char)(argb >> 24) };
	size_t row = 1 + (size_t)size * 4;
	size_t n = row * size;
	unsigned char *raw = malloc(n);
	for (int y = 0; y < size; y++) {
		raw[y * row] = 0;
		for (int x = 0; x < size; x++) {
			memcpy(raw + y * row + 1 + x * 4, px, 4);
		}
	}
	uint32_t a = 1, b = 0;
	for (size_t i = 0; i < n; i++) {
		a = (a + raw[i]) % 65521;
		b = (b + a) % 65521;
	}
	uint32_t adler = b << 16 | a;
	FILE *f = fopen(path, "wb");
	assert(f);
	fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
	unsigned char ihdr[13] = { 0, 0, 0, 0, 0, 0, 0, 0, 8, 6, 0, 0, 0 };
	ihdr[2] = (size >> 8) & 0xFF; ihdr[3] = size & 0xFF;
	ihdr[6] = (size >> 8) & 0xFF; ihdr[7] = size & 0xFF;
	chunk(f, "IHDR", ihdr, 13);
	size_t zlen = 2 + 5 + n + 4;
	unsigned char *z = malloc(zlen);
	z[0] = 0x78; z[1] = 0x01; z[2] = 0x01;
	z[3] = n & 0xFF; z[4] = (n >> 8) & 0xFF;
	z[5] = (~n) & 0xFF; z[6] = ((~n) >> 8) & 0xFF;
	memcpy(z + 7, raw, n);
	z[7 + n] = (adler >> 24) & 0xFF; z[8 + n] = (adler >> 16) & 0xFF;
	z[9 + n] = (adler >> 8) & 0xFF; z[10 + n] = adler & 0xFF;
	chunk(f, "IDAT", z, zlen);
	chunk(f, "IEND", NULL, 0);
	fclose(f);
	free(raw);
	free(z);
}

int main(void) {
	char dir[64];
	strcpy(dir, "/tmp/zacthemeXXXXXX");
	assert(mkdtemp(dir) != NULL);
	char conf[128];
	snprintf(conf, sizeof(conf), "%s/theme.conf", dir);
	FILE *f = fopen(conf, "w");
	assert(f);
	fputs("name=Test Theme\n", f);
	fclose(f);

	ZacTheme t;
	assert(zac_theme_load(&t, dir) == 0);
	assert(strcmp(t.name, "Test Theme") == 0);
	assert(t.directory && strcmp(t.directory, dir) == 0);
	zac_theme_free(&t);
	assert(t.name == NULL && t.directory == NULL);

	/* Missing theme.conf: still loads with a default name. */
	char dir2[64];
	strcpy(dir2, "/tmp/zacthemeXXXXXX");
	assert(mkdtemp(dir2) != NULL);
	ZacTheme t2;
	assert(zac_theme_load(&t2, dir2) == 0);
	assert(t2.name != NULL);
	zac_theme_free(&t2);

	/* zac_theme_list */
	char root[64];
	strcpy(root, "/tmp/zacthemesXXXXXX");
	assert(mkdtemp(root) != NULL);
	char sub[128];
	snprintf(sub, sizeof(sub), "%s/Alpha", root);
	mkdir(sub, 0755);
	snprintf(conf, sizeof(conf), "%s/Alpha/theme.conf", root);
	f = fopen(conf, "w");
	assert(f); fputs("name=Alpha\n", f); fclose(f);
	char **dirs = NULL;
	int n = zac_theme_list(root, &dirs);
	assert(n == 1);
	assert(dirs && dirs[0]);
	free(dirs[0]);
	free(dirs);
	/* cleanup */
	snprintf(conf, sizeof(conf), "%s/Alpha/theme.conf", root);
	unlink(conf);
	rmdir(sub);
	rmdir(root);

	/* zac_theme_asset: resolve from the active appearance theme. */
	char cfg[64];
	strcpy(cfg, "/tmp/zaccfgXXXXXX");
	assert(mkdtemp(cfg) != NULL);
	setenv("XDG_CONFIG_HOME", cfg, 1);
	setenv("HOME", cfg, 1);
	char themeroot[256];
	snprintf(themeroot, sizeof(themeroot), "%s/zacos9/themes", cfg);
	char bundle[256];
	snprintf(bundle, sizeof(bundle), "%s/zacos9/themes/Test", cfg);
	/* mkdir -p */
	{
		char tmp[256];
		snprintf(tmp, sizeof(tmp), "%s", bundle);
		for (char *p = tmp + 1; *p; p++) if (*p == '/') { *p = '\0'; mkdir(tmp, 0755); *p = '/'; }
		mkdir(tmp, 0755);
	}
	char iconpath[256];
	snprintf(iconpath, sizeof(iconpath), "%s/zacos9/themes/Test/menu-icon.png", cfg);
	FILE *ic = fopen(iconpath, "w");
	assert(ic); fputs("png", ic); fclose(ic);
	char confpath[256];
	snprintf(confpath, sizeof(confpath), "%s/zacos9/desktop.conf", cfg);
	FILE *cf = fopen(confpath, "w");
	assert(cf); fputs("theme-bundle=Test\n", cf); fclose(cf);
	char themeconf[256];
	snprintf(themeconf, sizeof(themeconf), "%s/zacos9/themes/Test/theme.conf", cfg);
	FILE *tf = fopen(themeconf, "w");
	assert(tf); fputs("name=Test\naccent=ocean\nhighlight=46A0FA\n", tf); fclose(tf);

	char resolved[256];
	assert(zac_theme_asset("menu-icon.png", resolved, sizeof(resolved)) == true);
	assert(strstr(resolved, "/Test/menu-icon.png") != NULL);
	assert(zac_theme_asset("boot/missing.png", resolved, sizeof(resolved)) == false);

	/* zac_theme_color: reads RRGGBB colour keys from the active theme. */
	{
		char tc[256];
		snprintf(tc, sizeof(tc), "%s/zacos9/themes/Test/theme.conf", cfg);
		FILE *tcf = fopen(tc, "a");
		assert(tcf);
		fputs("title-base=FF0000\nframe-light=00FF00\n", tcf);
		fclose(tcf);
		uint32_t v = 0;
		assert(zac_theme_color("title-base", &v) == true);
		assert(v == 0xFFFF0000u);
		assert(zac_theme_color("frame-light", &v) == true);
		assert(v == 0xFF00FF00u);
		assert(zac_theme_color("no-such-key", &v) == false);
	}

	/* pl_icon: the bundle's icons/<kind>-<size>.png overrides the builtin. */
	char iconsdir[300];
	snprintf(iconsdir, sizeof(iconsdir), "%s/icons", bundle);
	mkdir(iconsdir, 0755);
	char folder32[320], folder16[320];
	snprintf(folder32, sizeof(folder32), "%s/folder-32.png", iconsdir);
	snprintf(folder16, sizeof(folder16), "%s/folder-16.png", iconsdir);
	write_flat_png(folder32, 32, 0xFFFF0000u);
	write_flat_png(folder16, 16, 0xFF00FF00u);
	assert(pl_icon(PL_ICON_FOLDER, 32)[0] == 0xFFFF0000u);
	assert(pl_icon(PL_ICON_FOLDER, 16)[0] == 0xFF00FF00u);
	assert(pl_icon(PL_ICON_DOCUMENT, 32)[0] != 0xFFFF0000u);

	/* Custom folders keep their emblems over the theme's folder: every
	 * pixel is either the theme's folder or the built-in emblem, and at
	 * least one emblem pixel survives. Non-emblem kinds are untouched. */
	static const enum pl_icon_kind emblem_kinds[] = {
		PL_ICON_SYSTEM_FOLDER, PL_ICON_APPLICATIONS_FOLDER, PL_ICON_HOME_FOLDER,
		PL_ICON_CONTROL_PANELS, PL_ICON_SF_APPEARANCE, PL_ICON_SF_EXTENSIONS,
		PL_ICON_SF_SOUNDS, PL_ICON_SF_STARTUP_ITEMS, PL_ICON_SF_APPLICATION_SUPPORT,
		PL_ICON_SF_DEVICE_DRIVERS, PL_ICON_SF_NETWORK, PL_ICON_SF_SYSTEM_LOGS,
		PL_ICON_SF_SOFTWARE_COMPONENTS, PL_ICON_SF_FONTS, PL_ICON_SF_FAVORITES,
		PL_ICON_SF_TOY_BOX_ITEMS, PL_ICON_SF_FOLDER_TRICKS, PL_ICON_SF_CONTEXTUAL_MENU_ITEMS,
		PL_ICON_SF_PREFERENCES, PL_ICON_SF_UTILITIES,
	};
	static const int emblem_count = sizeof(emblem_kinds) / sizeof(emblem_kinds[0]);
	char plainbundle[256];
	snprintf(plainbundle, sizeof(plainbundle), "%s/NoFolder", themeroot);
	mkdir(plainbundle, 0755);
	char plainconf[320];
	snprintf(plainconf, sizeof(plainconf), "%s/theme.conf", plainbundle);
	FILE *pf = fopen(plainconf, "w");
	assert(pf); fputs("name=Plain\n", pf); fclose(pf);
	uint32_t *themed[20][2], *base[2], *plain[20][2];
	for (int k = 0; k < emblem_count; k++) {
		for (int s = 0; s < 2; s++) {
			int px = s == 0 ? 32 : 16;
			themed[k][s] = malloc(sizeof(uint32_t) * (size_t)px * px);
			plain[k][s] = malloc(sizeof(uint32_t) * (size_t)px * px);
			assert(themed[k][s] && plain[k][s]);
			memcpy(themed[k][s], pl_icon(emblem_kinds[k], px), sizeof(uint32_t) * (size_t)px * px);
		}
	}
	for (int s = 0; s < 2; s++) {
		int px = s == 0 ? 32 : 16;
		base[s] = malloc(sizeof(uint32_t) * (size_t)px * px);
		assert(base[s]);
		memcpy(base[s], pl_icon(PL_ICON_FOLDER, px), sizeof(uint32_t) * (size_t)px * px);
	}
	uint32_t doc_themed[32 * 32];
	memcpy(doc_themed, pl_icon(PL_ICON_DOCUMENT, 32), sizeof(doc_themed));
	FILE *df = fopen(confpath, "w");
	assert(df); fputs("theme-bundle=NoFolder\n", df); fclose(df);
	for (int k = 0; k < emblem_count; k++) {
		for (int s = 0; s < 2; s++) {
			int px = s == 0 ? 32 : 16;
			memcpy(plain[k][s], pl_icon(emblem_kinds[k], px), sizeof(uint32_t) * (size_t)px * px);
		}
	}
	uint32_t doc_plain[32 * 32];
	memcpy(doc_plain, pl_icon(PL_ICON_DOCUMENT, 32), sizeof(doc_plain));
	assert(memcmp(doc_themed, doc_plain, sizeof(doc_themed)) == 0);
	for (int k = 0; k < emblem_count; k++) {
		for (int s = 0; s < 2; s++) {
			int px = s == 0 ? 32 : 16;
			int kept = 0, bad = 0;
			for (int i = 0; i < px * px; i++) {
				if (themed[k][s][i] != base[s][i]) {
					if (themed[k][s][i] == plain[k][s][i]) {
						kept++;
					} else {
						bad++;
					}
				}
			}
			assert(kept > 0 && bad == 0);
			free(themed[k][s]);
			free(plain[k][s]);
		}
	}
	free(base[0]);
	free(base[1]);
	df = fopen(confpath, "w");
	assert(df); fputs("theme-bundle=Test\n", df); fclose(df);
	unlink(plainconf);
	rmdir(plainbundle);

	/* A theme's own System Folder art (-3983) is used as-is, like the
	 * other mapped desktop icons. */
	char sysbundle[256];
	snprintf(sysbundle, sizeof(sysbundle), "%s/SysFolder", themeroot);
	mkdir(sysbundle, 0755);
	char sysconf[320];
	snprintf(sysconf, sizeof(sysconf), "%s/theme.conf", sysbundle);
	FILE *sf = fopen(sysconf, "w");
	assert(sf); fputs("name=Sys\n", sf); fclose(sf);
	char sysicons[300];
	snprintf(sysicons, sizeof(sysicons), "%s/icons", sysbundle);
	mkdir(sysicons, 0755);
	char sys32[320], sys16[320];
	snprintf(sys32, sizeof(sys32), "%s/system-folder-32.png", sysicons);
	snprintf(sys16, sizeof(sys16), "%s/system-folder-16.png", sysicons);
	write_flat_png(sys32, 32, 0xFF0000FFu);
	write_flat_png(sys16, 16, 0xFF00FF00u);
	df = fopen(confpath, "w");
	assert(df); fputs("theme-bundle=SysFolder\n", df); fclose(df);
	assert(pl_icon(PL_ICON_SYSTEM_FOLDER, 32)[0] == 0xFF0000FFu);
	assert(pl_icon(PL_ICON_SYSTEM_FOLDER, 16)[0] == 0xFF00FF00u);
	df = fopen(confpath, "w");
	assert(df); fputs("theme-bundle=Test\n", df); fclose(df);
	unlink(sys32);
	unlink(sys16);
	unlink(sysconf);
	rmdir(sysicons);
	rmdir(sysbundle);

	/* zac_cursor_theme: the bundle's Xcursor set when it ships one. */
	char cname[64], cdir[256];
	assert(!zac_cursor_theme(cname, sizeof(cname), cdir, sizeof(cdir)));
	char cursorsbase[300];
	snprintf(cursorsbase, sizeof(cursorsbase), "%s/cursors", bundle);
	mkdir(cursorsbase, 0755);
	char cursorsdir[320];
	snprintf(cursorsdir, sizeof(cursorsdir), "%s/Macintosh", cursorsbase);
	mkdir(cursorsdir, 0755);
	assert(!zac_cursor_theme(cname, sizeof(cname), cdir, sizeof(cdir)));
	char cindex[340];
	snprintf(cindex, sizeof(cindex), "%s/index.theme", cursorsdir);
	FILE *cf2 = fopen(cindex, "w");
	assert(cf2); fputs("[Icon Theme]\nName=Macintosh\n", cf2); fclose(cf2);
	assert(zac_cursor_theme(cname, sizeof(cname), cdir, sizeof(cdir)));
	assert(!strcmp(cname, "Macintosh"));
	assert(strstr(cdir, "cursors") != NULL);

	/* zac_theme_apply: the theme's appearance settings reach desktop.conf. */
	assert(zac_theme_apply() == 2);
	{
		char buf[256];
		FILE *d = fopen(confpath, "r");
		assert(d);
		buf[0] = '\0';
		while (fgets(buf, sizeof(buf), d)) {
			if (strstr(buf, "accent=ocean")) break;
		}
		fclose(d);
		assert(strstr(buf, "accent=ocean") != NULL);
	}

	unlink(iconpath);
	unlink(folder32);
	unlink(folder16);
	unlink(cindex);
	rmdir(cursorsdir);
	rmdir(cursorsbase);
	rmdir(iconsdir);
	unlink(themeconf);
	unlink(confpath);
	rmdir(bundle);
	rmdir(themeroot);
	rmdir(cfg);

	rmdir(dir);
	rmdir(dir2);
	printf("theme: ok\n");
	return 0;
}