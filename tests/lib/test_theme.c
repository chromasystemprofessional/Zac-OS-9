#include "theme.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

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
	assert(cf); fputs("appearance-theme=Test\n", cf); fclose(cf);
	char themeconf[256];
	snprintf(themeconf, sizeof(themeconf), "%s/zacos9/themes/Test/theme.conf", cfg);
	FILE *tf = fopen(themeconf, "w");
	assert(tf); fputs("name=Test\naccent=ocean\nhighlight=46A0FA\n", tf); fclose(tf);

	char resolved[256];
	assert(zac_theme_asset("menu-icon.png", resolved, sizeof(resolved)) == true);
	assert(strstr(resolved, "/Test/menu-icon.png") != NULL);
	assert(zac_theme_asset("boot/missing.png", resolved, sizeof(resolved)) == false);

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