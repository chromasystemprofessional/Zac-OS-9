/*
 * The control panels from their desktop entries (controlpanels.c): only
 * those marked X-ZacOS9-ControlPanel, by name, a NoDisplay one left out,
 * the user's own copy of an entry winning, Exec= split into the program
 * and its argument, and each with its icon.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "controlpanels.h"

static int fails;
static char root[] = "/tmp/zacos9-panels-XXXXXX";

static void check(bool ok, const char *what) {
	printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

static void entry(const char *dir, const char *id, const char *body) {
	char path[512];
	snprintf(path, sizeof(path), "%s/%s/applications", root, dir);
	mkdir(path, 0700);
	snprintf(path, sizeof(path), "%s/%s/applications/%s", root, dir, id);
	FILE *f = fopen(path, "w");
	fprintf(f, "[Desktop Entry]\nType=Application\n%s", body);
	fclose(f);
}

int main(void) {
	if (!mkdtemp(root)) {
		return 1;
	}
	char dir[512];
	snprintf(dir, sizeof(dir), "%s/system", root);
	mkdir(dir, 0700);
	setenv("XDG_DATA_DIRS", dir, 1);
	snprintf(dir, sizeof(dir), "%s/home", root);
	mkdir(dir, 0700);
	setenv("XDG_DATA_HOME", dir, 1);

	entry("system", "zacos9-mouse.desktop",
		"Name=Mouse\nExec=zacos9-controlpanel mouse\nCategories=Settings;X-ZacOS9-ControlPanel;\n");
	entry("system", "zacos9-appearance.desktop",
		"Name=Appearance\nExec=zacos9-appearance\nCategories=Settings;X-ZacOS9-ControlPanel;\n");
	entry("system", "zacos9-tcpip.desktop",
		"Name=TCP/IP\nExec=zacos9-tcpip\nCategories=Settings;X-ZacOS9-ControlPanel;\n");
	entry("system", "zacos9-calculator.desktop",
		"Name=Calculator\nExec=zacos9-calculator\nCategories=Utility;\n");
	entry("system", "zacos9-tablet.desktop",
		"Name=Tablet\nExec=zacos9-controlpanel tablet\nNoDisplay=true\nCategories=Settings;X-ZacOS9-ControlPanel;\n");
	entry("system", "zacos9-text.desktop",
		"Name=Text\nExec=zacos9-controlpanel text\nCategories=Settings;X-ZacOS9-ControlPanel;\n");
	/* The user's own copy, renamed. */
	entry("home", "zacos9-text.desktop",
		"Name=Writing\nExec=zacos9-controlpanel text\nCategories=Settings;X-ZacOS9-ControlPanel;\n");

	size_t n = 0;
	struct pl_control_panel *p = pl_control_panels(&n);
	check(n == 4, "four panels: not the calculator, nor the NoDisplay one");
	check(n == 4 && strcmp(p[0].name, "Appearance") == 0 && strcmp(p[1].name, "Mouse") == 0 &&
		strcmp(p[2].name, "TCP/IP") == 0 && strcmp(p[3].name, "Writing") == 0,
		"by name, ignoring case, the user's copy winning");
	check(n == 4 && strcmp(p[1].program, "zacos9-controlpanel") == 0 && strcmp(p[1].args, " mouse") == 0,
		"Exec= split into the program and its argument");
	check(n == 4 && strcmp(p[0].program, "zacos9-appearance") == 0 && strcmp(p[0].args, "") == 0,
		"a panel that's a program of its own has no argument");
	check(n == 4 && p[1].icon == PL_ICON_CP_MOUSE && p[2].icon == PL_ICON_EXT_ETHERNET &&
		p[3].icon == PL_ICON_CP_TEXT, "each with its icon");
	pl_control_panels_free(p, n);

	char cleanup[600];
	snprintf(cleanup, sizeof(cleanup), "rm -rf '%s'", root);
	if (system(cleanup) != 0) {
		fails++;
	}
	return fails ? 1 : 0;
}
