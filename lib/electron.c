#define _DEFAULT_SOURCE
#include "electron.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

bool pl_electron_app(const char *program) {
	char found[PATH_MAX], real[PATH_MAX];
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
