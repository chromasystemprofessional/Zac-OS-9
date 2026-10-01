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
	char dir[] = "/tmp/platinum-settings-XXXXXX";
	if (!mkdtemp(dir)) {
		return 1;
	}
	char sub[256], conf[300];
	snprintf(sub, sizeof(sub), "%s/platinum", dir);
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

	char wav[1200];
	snprintf(wav, sizeof(wav), "%s/sounds/platinum.wav", pl_data_dir());
	check(access(wav, R_OK) == 0, "the default alert sound is found");

	unlink(conf);
	rmdir(sub);
	rmdir(dir);
	printf("%d failure%s\n", fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
