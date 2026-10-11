#define _DEFAULT_SOURCE
#include "electron.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool exists(const char *dir, const char *name) {
	char path[PATH_MAX + 64];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	return access(path, F_OK) == 0;
}

static bool app_dir(const char *dir) {
	return exists(dir, "resources/app.asar") || exists(dir, "resources/app/package.json");
}

static bool find_in_path(const char *program, char *out, size_t size) {
	const char *path = getenv("PATH");
	if (!path) {
		return false;
	}
	char *copy = strdup(path);
	bool found = false;
	for (char *dir = strtok(copy, ":"); dir && !found; dir = strtok(NULL, ":")) {
		snprintf(out, size, "%s/%s", dir, program);
		found = access(out, X_OK) == 0;
	}
	free(copy);
	return found;
}

/* The folder holding an Electron app's resources/ (into root), if it is one. */
static bool electron_root(const char *program, char root[PATH_MAX]) {
	char found[PATH_MAX];
	char *real = root;
	if (!program || !*program) {
		return false;
	}
	if (!strchr(program, '/')) {
		if (!find_in_path(program, found, sizeof(found))) {
			return false;
		}
		program = found;
	}
	if (!realpath(program, real)) {
		return false;
	}
	char *slash = strrchr(real, '/');
	if (!slash) {
		return false;
	}
	*slash = '\0';
	if (app_dir(real)) {
		return true;
	}
	slash = strrchr(real, '/');
	if (!slash || slash == real) {
		return false;
	}
	*slash = '\0';
	return app_dir(real);
}

bool pl_electron_app(const char *program) {
	char root[PATH_MAX];
	return electron_root(program, root);
}

/* "KEY": "VALUE" in a small JSON file, without a JSON parser: enough for
 * product.json's top-level strings. */
static bool json_string(const char *text, const char *key, char *out, size_t size) {
	char quoted[128];
	snprintf(quoted, sizeof(quoted), "\"%s\"", key);
	const char *p = strstr(text, quoted);
	if (!p || !(p = strchr(p + strlen(quoted), ':')) || !(p = strchr(p, '"'))) {
		return false;
	}
	const char *end = strchr(++p, '"');
	if (!end || (size_t)(end - p) >= size || end == p) {
		return false;
	}
	memcpy(out, p, end - p);
	out[end - p] = '\0';
	return true;
}

static char *read_file(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		return NULL;
	}
	char *text = malloc(1 << 20);
	size_t n = text ? fread(text, 1, (1 << 20) - 1, f) : 0;
	fclose(f);
	if (text) {
		text[n] = '\0';
	}
	return text;
}

bool pl_code_settings_path(const char *program, char *out, size_t size) {
	char root[PATH_MAX], path[PATH_MAX + 64], name[128];
	if (!electron_root(program, root)) {
		return false;
	}
	snprintf(path, sizeof(path), "%s/resources/app/product.json", root);
	char *product = read_file(path);
	/* VS Code and its relatives (VSCodium, Code - OSS, Cursor) keep their
	 * settings under ~/.config/<nameShort>; other Electron apps have no
	 * product.json, or no nameShort, and are left alone. */
	bool code = product && strstr(product, "\"dataFolderName\"") &&
		json_string(product, "nameShort", name, sizeof(name)) && !strchr(name, '/');
	free(product);
	if (!code) {
		return false;
	}
	const char *config = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (config && *config) {
		snprintf(out, size, "%s/%s/User/settings.json", config, name);
	} else if (home) {
		snprintf(out, size, "%s/.config/%s/User/settings.json", home, name);
	} else {
		return false;
	}
	return true;
}

static void make_parents(char *path) {
	for (char *p = strchr(path + 1, '/'); p; p = strchr(p + 1, '/')) {
		*p = '\0';
		mkdir(path, 0755);
		*p = '/';
	}
}

bool pl_code_use_native_title_bar(const char *settings) {
	char *text = read_file(settings);
	if (text && strstr(text, "\"window.titleBarStyle\"")) {
		free(text); /* chosen already, whichever way: theirs to keep */
		return false;
	}
	static const char setting[] = "\"window.titleBarStyle\": \"native\"";
	const char *brace = text ? strchr(text, '{') : NULL;
	bool blank = !text || text[strspn(text, " \t\r\n")] == '\0';
	if (!brace && !blank) {
		free(text); /* not a settings file we understand: leave it */
		return false;
	}
	char tmp[PATH_MAX + 8];
	snprintf(tmp, sizeof(tmp), "%s.zacos9", settings);
	make_parents(tmp);
	FILE *f = fopen(tmp, "w");
	if (!f) {
		free(text);
		return false;
	}
	if (brace) {
		/* VS Code reads settings as JSON with comments and trailing commas,
		 * so a line after the opening brace is always valid. */
		fwrite(text, 1, brace + 1 - text, f);
		fprintf(f, "\n    %s,", setting);
		fputs(brace + 1, f);
	} else {
		fprintf(f, "{\n    %s\n}\n", setting);
	}
	free(text);
	bool ok = fclose(f) == 0 && rename(tmp, settings) == 0;
	if (!ok) {
		unlink(tmp);
	}
	return ok;
}

int pl_screen_scale(void) {
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	const char *display = getenv("WAYLAND_DISPLAY");
	if (!runtime || !display) {
		return 1;
	}
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s/zacos9-outputs-%s", runtime, display);
	FILE *f = fopen(path, "r");
	if (!f) {
		return 1;
	}
	/* "output NAME WxH scale N nested N x N y N main N", one per screen;
	 * every screen has the same scale, but the main one is authoritative. */
	int scale = 1;
	bool have_main = false;
	char line[512];
	while (fgets(line, sizeof(line), f)) {
		char name[128];
		int w, h, s, nested, x, y, main;
		if (sscanf(line, "output %127s %dx%d scale %d nested %d x %d y %d main %d",
				name, &w, &h, &s, &nested, &x, &y, &main) == 8 && s >= 1 && !have_main) {
			scale = s;
			have_main = main == 1;
		}
	}
	fclose(f);
	return scale;
}

char *pl_electron_command(const char *command) {
	if (!command) {
		return NULL;
	}
	const char *p = command;
	while (*p == ' ' || *p == '\t') {
		p++;
	}
	if (strncmp(p, "exec ", 5) == 0) {
		p += 5;
		while (*p == ' ') {
			p++;
		}
	}
	/* The program: a word, or a quoted string. */
	char program[PATH_MAX];
	size_t n = 0;
	const char *end = p;
	if (*p == '"' || *p == '\'') {
		const char quote = *p;
		for (end = p + 1; *end && *end != quote; end++) {
			if (*end == '\\' && quote == '"' && end[1]) {
				end++;
			}
			if (n < sizeof(program) - 1) {
				program[n++] = *end;
			}
		}
		if (*end != quote) {
			return NULL;
		}
		end++;
	} else {
		for (; *end && *end != ' ' && *end != '\t'; end++) {
			if (n < sizeof(program) - 1) {
				program[n++] = *end;
			}
		}
	}
	program[n] = '\0';
	if (!pl_electron_app(program)) {
		return NULL;
	}
	/* VS Code gives the menu bar its menus only with its native title bar. */
	char settings[PATH_MAX];
	if (pl_code_settings_path(program, settings, sizeof(settings))) {
		pl_code_use_native_title_bar(settings);
	}
	char args[96];
	snprintf(args, sizeof(args), " --ozone-platform=x11 --force-device-scale-factor=%d",
		pl_screen_scale());
	size_t head = (size_t)(end - command);
	char *out = malloc(strlen(command) + strlen(args) + 1);
	memcpy(out, command, head);
	strcpy(out + head, args);
	strcat(out, end);
	return out;
}
