/*
 * The control panels from their desktop entries (controlpanels.c): only
 * those marked X-ZacOS9-ControlPanel, by name, a NoDisplay one left out,
 * the user's own copy of an entry winning, Exec= split into the program
 * and its argument, and each with its icon. Then the ones the user turned
 * off, in Control Panels (Disabled), and turning them back on and off
 * again through the user's own copy of their entries.
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

/* Whether `path` can be read at all, and whether its text holds `text`. */
static bool exists(const char *path) {
	FILE *f = fopen(path, "r");
	const bool ok = f != NULL;
	if (f) {
		fclose(f);
	}
	return ok;
}

static bool holds(const char *path, const char *text) {
	FILE *f = fopen(path, "r");
	if (!f) {
		return false;
	}
	char body[4096];
	const size_t n = fread(body, 1, sizeof(body) - 1, f);
	fclose(f);
	body[n] = '\0';
	return strstr(body, text) != NULL;
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
	/* None of the programs installed, as in a build tree or CI: an
	 * entry is listed whether or not its program is on $PATH. */
	setenv("PATH", root, 1);

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
	entry("system", "zacos9-speech.desktop",
		"Name=Speech\nExec=zacos9-controlpanel speech\nCategories=Settings;X-ZacOS9-ControlPanel;\n");
	/* The user's own copies: one hiding a panel, one renaming it. */
	entry("home", "zacos9-speech.desktop", "Name=Speech\nHidden=true\n");
	entry("home", "zacos9-text.desktop",
		"Name=Writing\nExec=zacos9-controlpanel text\nCategories=Settings;X-ZacOS9-ControlPanel;\n");

	size_t n = 0;
	struct pl_control_panel *p = pl_control_panels(&n);
	check(n == 4, "four panels: not the calculator, the NoDisplay one, or one the user hid");
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

	/* Speech is the panel the user hid, so it is the System Folder's
	 * Control Panels (Disabled) folder, as it was on a Mac: with the name,
	 * program and icon it has when it is on. */
	char own[512];
	snprintf(own, sizeof(own), "%s/home/applications/zacos9-speech.desktop", root);
	size_t off_count = 0;
	struct pl_control_panel *off = pl_control_panels_disabled(&off_count);
	check(off_count == 1 && strcmp(off[0].name, "Speech") == 0,
		"the panel the user hid is in (Disabled)");
	check(off_count == 1 && strcmp(off[0].program, "zacos9-controlpanel") == 0 &&
		strcmp(off[0].args, " speech") == 0 && off[0].icon == PL_ICON_CP_SPEECH,
		"with the name, program and icon it has when it is on");
	pl_control_panels_free(off, off_count);

	/* Turned back on, the system's entry is the panel again. */
	check(pl_control_panel_set_enabled("zacos9-speech.desktop", true),
		"a panel can be turned back on");
	check(!exists(own), "which removes the user's hiding copy");
	p = pl_control_panels(&n);
	check(n == 5 && strcmp(p[2].name, "Speech") == 0, "and it is in Control Panels again");
	pl_control_panels_free(p, n);
	off = pl_control_panels_disabled(&off_count);
	check(off_count == 0, "and no longer in (Disabled)");
	pl_control_panels_free(off, off_count);
	check(pl_control_panel_set_enabled("zacos9-speech.desktop", true),
		"and turning it on twice is harmless");

	/* Turned off again, the copy the reader leaves out. */
	check(pl_control_panel_set_enabled("zacos9-speech.desktop", false),
		"a panel can be turned off");
	check(holds(own, "NoDisplay=true") && holds(own, "X-ZacOS9-ControlPanel"),
		"by a copy of its entry, still a control panel, marked NoDisplay=true");
	check(holds(own, "Exec=zacos9-controlpanel speech"), "with the system's program");
	p = pl_control_panels(&n);
	check(n == 4, "which leaves it out of Control Panels");
	pl_control_panels_free(p, n);
	off = pl_control_panels_disabled(&off_count);
	check(off_count == 1 && strcmp(off[0].name, "Speech") == 0,
		"and puts it in (Disabled) again");
	pl_control_panels_free(off, off_count);
	check(!pl_control_panel_set_enabled("zacos9-calculator.desktop", false),
		"something that isn't a panel can't be turned off");
	check(!pl_control_panel_set_enabled("zacos9-absent.desktop", false),
		"nor can a panel that isn't installed");
	check(!pl_control_panel_set_enabled("../zacos9-speech.desktop", false),
		"and nothing outside the entries' own folder");

	char cleanup[600];
	snprintf(cleanup, sizeof(cleanup), "/bin/rm -rf '%s'", root);
	if (system(cleanup) != 0) {
		fails++;
	}
	return fails ? 1 : 0;
}
