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

static void write_text(const char *path, const char *text) {
	FILE *f = fopen(path, "w");
	fputs(text, f);
	fclose(f);
}

/* The whole file (to be freed), NULL if unreadable. */
static char *slurp(const char *path, size_t *len) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	rewind(f);
	char *text = malloc(n + 1);
	text[fread(text, 1, n, f)] = '\0';
	fclose(f);
	if (len) {
		*len = (size_t)n;
	}
	return text;
}

/* Settings `before` get the native title bar (or are left alone, if `after`
 * is NULL), giving exactly `after`. */
static bool settings_become(const char *path, const char *before, const char *after) {
	write_text(path, before);
	const bool wrote = pl_code_use_native_title_bar(path);
	char *got = slurp(path, NULL);
	const bool ok = got && wrote == (after != NULL) && strcmp(got, after ? after : before) == 0;
	if (!ok) {
		printf("      got (%s): %s\n", wrote ? "wrote" : "left", got ? got : "(unreadable)");
	}
	free(got);
	return ok;
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
	f = fopen(path, "w");
	fputs("output eDP-1 2560x1600 scale 1.5 nested 0 x 0 y 0 main 1\n", f);
	fclose(f);
	check(pl_screen_scale() == 1, "1.5: X11 programs are drawn at 1x");
	f = fopen(path, "w");
	fputs("output eDP-1 2880x1800 scale 2 nested 0 x 0 y 0 main 1\n", f);
	fclose(f);

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

	/* VS Code: its settings get the native title bar, unless already chosen. */
	char code[512], settings[600], want_path[600];
	snprintf(path, sizeof(path), "%s/Code", root);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/Code/resources", root);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/Code/resources/app", root);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/Code/resources/app/package.json", root);
	touch(path, false);
	snprintf(path, sizeof(path), "%s/Code/resources/app/product.json", root);
	f = fopen(path, "w");
	fputs("{\n  \"nameShort\": \"Code\",\n  \"dataFolderName\": \".vscode\"\n}\n", f);
	fclose(f);
	snprintf(code, sizeof(code), "%s/Code/code", root);
	touch(code, true);
	setenv("XDG_CONFIG_HOME", root, 1);
	snprintf(want_path, sizeof(want_path), "%s/Code/User/settings.json", root);
	check(pl_code_settings_path(code, settings, sizeof(settings)) && strcmp(settings, want_path) == 0,
		"VS Code's settings are found from its product.json");
	check(!pl_code_settings_path(app, settings, sizeof(settings)), "other Electron apps have none");
	char *launched = pl_electron_command(code);
	free(launched);
	f = fopen(want_path, "r");
	char text[512] = "";
	if (f) {
		text[fread(text, 1, sizeof(text) - 1, f)] = '\0';
		fclose(f);
	}
	check(strstr(text, "\"window.titleBarStyle\": \"native\"") != NULL,
		"starting VS Code makes its settings with the native title bar");
	f = fopen(want_path, "w");
	fputs("// mine\n{\n    \"editor.fontSize\": 14\n}\n", f);
	fclose(f);
	check(pl_code_use_native_title_bar(want_path), "added to existing settings");
	f = fopen(want_path, "r");
	text[fread(text, 1, sizeof(text) - 1, f)] = '\0';
	fclose(f);
	check(strcmp(text, "// mine\n{\n    \"window.titleBarStyle\": \"native\",\n    \"editor.fontSize\": 14\n}\n") == 0,
		"after the brace, keeping comments and other settings");
	f = fopen(want_path, "w");
	fputs("{ \"window.titleBarStyle\": \"custom\" }\n", f);
	fclose(f);
	check(!pl_code_use_native_title_bar(want_path), "a title bar style chosen already is kept");


	/* Settings are JSON with comments: a brace in a comment or a string is
	 * not the object's, and a style mentioned only in a comment isn't chosen. */
	check(settings_become(want_path, "// example { here\n{\n  \"editor.fontSize\": 14\n}\n",
		"// example { here\n{\n    \"window.titleBarStyle\": \"native\",\n  \"editor.fontSize\": 14\n}\n"),
		"a brace in a comment before the object isn't its opening brace");
	check(settings_become(want_path,
		"/* { \"window.titleBarStyle\": \"custom\" } */\n{\n  // \"window.titleBarStyle\": \"custom\",\n  \"a\": \"{\\\"\"\n}\n",
		"/* { \"window.titleBarStyle\": \"custom\" } */\n{\n    \"window.titleBarStyle\": \"native\",\n  // \"window.titleBarStyle\": \"custom\",\n  \"a\": \"{\\\"\"\n}\n"),
		"one in a block comment, a commented-out style or a string doesn't count");
	check(settings_become(want_path,
		"// {\n{ /* c */ \"window.titleBarStyle\" /* x */ : \"custom\", }\n", NULL),
		"a chosen style is kept, whatever comments are around it");
	check(settings_become(want_path, "\xEF\xBB\xBF{}", "\xEF\xBB\xBF{\n    \"window.titleBarStyle\": \"native\"}"),
		"an empty object (after a byte order mark) gets no stray comma");
	check(settings_become(want_path, "{ \"a\": 1\n", NULL) &&
		settings_become(want_path, "// just a note\n", NULL) &&
		settings_become(want_path, "{ \"a\": \"unterminated }\n", NULL) &&
		settings_become(want_path, "{ /* unterminated }\n", NULL) &&
		settings_become(want_path, "{ \"a\": 1 }\n{ \"b\": 2 }\n", NULL),
		"anything that isn't one whole object is left alone");

	/* Big settings are rewritten whole, never cut off at a buffer's size. */
	const size_t big = 3 << 20;
	char *large = malloc(big + 64);
	strcpy(large, "{\n  \"big\": \"");
	size_t at = strlen(large);
	memset(large + at, 'x', big);
	strcpy(large + at + big, "\",\n  \"tail\": true\n}\n");
	write_text(want_path, large);
	size_t len = 0;
	check(pl_code_use_native_title_bar(want_path), "added to settings over a megabyte");
	char *grown = slurp(want_path, &len);
	static const char added[] = "\n    \"window.titleBarStyle\": \"native\",";
	check(grown && len == strlen(large) + strlen(added) &&
		strcmp(grown + len - strlen("\"tail\": true\n}\n"), "\"tail\": true\n}\n") == 0,
		"keeping every byte of them");
	free(grown);
	free(large);

	/* Its permissions stay, a symlink stays a symlink, and a file it can't
	 * read is never replaced. */
	write_text(want_path, "{}\n");
	chmod(want_path, 0600);
	struct stat st;
	check(pl_code_use_native_title_bar(want_path) && stat(want_path, &st) == 0 &&
		(st.st_mode & 0777) == 0600, "a private settings file stays private");
	char real[600], link[600];
	snprintf(real, sizeof(real), "%s/dotfiles-settings.json", root);
	snprintf(link, sizeof(link), "%s/Code/User/linked.json", root);
	write_text(real, "{ \"a\": 1 }\n");
	symlink(real, link);
	char *through = NULL;
	check(pl_code_use_native_title_bar(link) && lstat(link, &st) == 0 && S_ISLNK(st.st_mode) &&
		(through = slurp(real, NULL)) && strstr(through, "\"window.titleBarStyle\": \"native\""),
		"a symlinked settings file is changed through the link");
	free(through);
	snprintf(link, sizeof(link), "%s/Code/User/dangling.json", root);
	snprintf(real, sizeof(real), "%s/nowhere.json", root);
	symlink(real, link);
	check(!pl_code_use_native_title_bar(link) && access(real, F_OK) != 0,
		"a symlink to nothing is left alone");
	if (geteuid() != 0) {
		write_text(want_path, "{ \"secret\": 1 }\n");
		chmod(want_path, 0);
		check(!pl_code_use_native_title_bar(want_path), "an unreadable settings file is left alone");
		chmod(want_path, 0600);
		char *kept = slurp(want_path, NULL);
		check(kept && strcmp(kept, "{ \"secret\": 1 }\n") == 0, "and not overwritten");
		free(kept);
	}

	setenv("PATH", saved_path, 1);
	free(saved_path);
	char cleanup[600];
	snprintf(cleanup, sizeof(cleanup), "rm -rf '%s'", root);
	if (system(cleanup) != 0) {
		fails++;
	}
	return fails ? 1 : 0;
}
