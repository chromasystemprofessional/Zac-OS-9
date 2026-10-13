/*
 * Settings and shared data: desktop.conf values are read back, and the
 * sounds are found from a build tree.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "settings.h"

static int fails;

static void check(bool ok, const char *what) {
	printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

int main(void) {
	char dir[] = "/tmp/zacos9-settings-XXXXXX";
	if (!mkdtemp(dir)) {
		return 1;
	}
	char sub[256], conf[300];
	snprintf(sub, sizeof(sub), "%s/zacos9", dir);
	mkdir(sub, 0700);
	snprintf(conf, sizeof(conf), "%s/desktop.conf", sub);
	FILE *f = fopen(conf, "w");
	fputs("[General]\npattern = ocean-ripple\nalert-sound=glass\n", f);
	fclose(f);
	setenv("XDG_CONFIG_HOME", dir, 1);

	char v[64];
	check(pl_setting("pattern", v, sizeof(v)) && strcmp(v, "ocean-ripple") == 0,
		"pattern = ocean-ripple (spaces around =)");
	check(pl_setting("alert-sound", v, sizeof(v)) && strcmp(v, "glass") == 0, "alert-sound=glass");
	check(!pl_setting("missing", v, sizeof(v)), "an unset key reads as unset");

	/* Labels: the defaults, then the user's (Sniffer Preferences). */
	check(strcmp(pl_label_name(1), "Essential") == 0 && pl_label_color(2) == 0xFFDD0806 &&
		pl_label_color(0) == 0, "labels default to Mac OS's");
	pl_setting_set("label-names", "Urgent||Later");
	pl_setting_set("label-colors", "000000,nope,4600A5");
	check(strcmp(pl_label_name(1), "Urgent") == 0 && strcmp(pl_label_name(2), "Hot") == 0 &&
		strcmp(pl_label_name(3), "Later") == 0 && strcmp(pl_label_name(4), "Cool") == 0,
		"renamed labels; an empty name keeps the default");
	check(pl_label_color(1) == 0xFF000000 && pl_label_color(2) == 0xFFDD0806 &&
		pl_label_color(3) == 0xFF4600A5 && pl_label_color(7) == 0xFF562C05,
		"recoloured labels; a bad colour keeps the default");

	/* General Controls. */
	check(pl_menu_blinks() == 1 && pl_caret_blink_ms() == 1066,
		"menus blink once and the insertion point at medium by default");
	pl_setting_set("menu-blink", "0");
	pl_setting_set("caret-blink", "534");
	check(pl_menu_blinks() == 0 && pl_caret_blink_ms() == 534, "Menu Blinking Off; a fast insertion point");
	pl_setting_set("menu-blink", "9");
	pl_setting_set("caret-blink", "5");
	check(pl_menu_blinks() == 3 && pl_caret_blink_ms() == 1066, "out-of-range values are kept in bounds");

	char wav[1200];
	snprintf(wav, sizeof(wav), "%s/sounds/platinum.wav", pl_data_dir());
	check(access(wav, R_OK) == 0, "the default alert sound is found");

	unlink(conf);
	rmdir(sub);
	rmdir(dir);
	printf("%d failure%s\n", fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
