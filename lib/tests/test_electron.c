/*
 * Electron apps start under X11 at the screen's scale (lib/electron.c): which
 * programs count as Electron apps, the scale read from zacos9-wm's outputs
 * file, and where the arguments go in a command line.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "electron.h"

static int fails;

static void check(bool ok, const char *what) {
	printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

static void touch(const char *path, bool executable) {
	FILE *f = fopen(path, "w");
	fputs("#!/bin/sh\n", f);
	fclose(f);
	chmod(path, executable ? 0755 : 0644);
}

static bool command_is(const char *command, const char *want) {
	char *got = pl_electron_command(command);
	bool ok = got && strcmp(got, want) == 0;
	if (!ok) {
		printf("      got: %s\n", got ? got : "(null)");
	}
	free(got);
	return ok;
}

int main(void) {
	char root[] = "/tmp/test-electron-XXXXXX";
	if (!mkdtemp(root)) {
		return 1;
	}
	char path[512], app[512], other[512], bin[512];
	/* An Electron app: binary next to resources/app.asar, plus a launcher script in bin/. */
	snprintf(path, sizeof(path), "%s/My App", root);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/My App/resources", root);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/My App/resources/app.asar", root);
	touch(path, false);
	snprintf(app, sizeof(app), "%s/My App/myapp", root);
	touch(app, true);
	snprintf(path, sizeof(path), "%s/My App/bin", root);
	mkdir(path, 0755);
	snprintf(bin, sizeof(bin), "%s/My App/bin/myapp", root);
	touch(bin, true);
	/* Anything else. */
	snprintf(other, sizeof(other), "%s/plain", root);
	touch(other, true);

	check(pl_electron_app(app), "a binary beside resources/app.asar is Electron");
	check(pl_electron_app(bin), "a launcher script in its bin/ is too");
	check(!pl_electron_app(other) && !pl_electron_app("") && !pl_electron_app(NULL) &&
		!pl_electron_app("/no/such/program"), "other programs are not");

	char *saved_path = strdup(getenv("PATH") ? getenv("PATH") : "/usr/bin:/bin");
	setenv("PATH", root, 1);
	snprintf(path, sizeof(path), "%s/linked", root);
	symlink(app, path);
	check(pl_electron_app("linked"), "a name on $PATH is followed through its symlink");

	/* The scale: the main screen's, from the outputs file. */
	setenv("XDG_RUNTIME_DIR", root, 1);
	setenv("WAYLAND_DISPLAY", "wayland-test", 1);
	check(pl_screen_scale() == 1, "no outputs file: scale 1");
	snprintf(path, sizeof(path), "%s/zacos9-outputs-wayland-test", root);
	FILE *f = fopen(path, "w");
	fputs("mirror 0\n"
		"output HDMI-A-1 1920x1080 scale 1 nested 0 x 0 y 0 main 0\n"
		"mode 1920x1080@60000\n"
		"output eDP-1 2880x1800 scale 2 nested 0 x 1920 y 0 main 1\n", f);
	fclose(f);
	check(pl_screen_scale() == 2, "the main screen's scale");

	/* Command lines. */
	char command[1024], want[1024];
	snprintf(command, sizeof(command), "'%s' %%F", app);
	snprintf(want, sizeof(want), "'%s' --ozone-platform=x11 --force-device-scale-factor=2 %%F", app);
	check(command_is(command, want), "arguments go after a quoted program");
	snprintf(command, sizeof(command), "exec \"%s\"", app);
	snprintf(want, sizeof(want), "exec \"%s\" --ozone-platform=x11 --force-device-scale-factor=2", app);
	check(command_is(command, want), "after \"exec\" too");
	check(command_is("linked --new-window %U",
		"linked --ozone-platform=x11 --force-device-scale-factor=2 --new-window %U"),
		"a bare program name");
	snprintf(command, sizeof(command), "%s --flag", other);
	char *none = pl_electron_command(command);
	check(!none && !pl_electron_command("foot") && !pl_electron_command("'unterminated"),
		"other commands are left alone");
	free(none);

	setenv("PATH", saved_path, 1);
	free(saved_path);
	char cleanup[600];
	snprintf(cleanup, sizeof(cleanup), "rm -rf '%s'", root);
	if (system(cleanup) != 0) {
		fails++;
	}
	return fails ? 1 : 0;
}
