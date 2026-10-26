#define _DEFAULT_SOURCE
#include "electron.h"

#include <errno.h>
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

/* The whole of a file, NUL-terminated (its length in *len); NULL with errno
 * set if it can't be read, or is too big to be a settings file. */
static char *read_file(const char *path, size_t *len) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		return NULL;
	}
	enum { LIMIT = 64 << 20 };
	size_t size = 0, cap = 64 << 10;
	char *text = malloc(cap);
	while (text) {
		size += fread(text + size, 1, cap - 1 - size, f);
		if (size < cap - 1) {
			break;
		}
		char *bigger = cap < LIMIT ? realloc(text, cap * 2) : NULL;
		if (!bigger) {
			free(text);
			text = NULL;
			errno = EFBIG;
			break;
		}
		text = bigger;
		cap *= 2;
	}
	if (text && ferror(f)) {
		free(text);
		text = NULL;
		errno = EIO;
	}
	fclose(f);
	if (text) {
		text[size] = '\0';
		if (len) {
			*len = size;
		}
	}
	return text;
}

bool pl_code_settings_path(const char *program, char *out, size_t size) {
	char root[PATH_MAX], path[PATH_MAX + 64], name[128];
	if (!electron_root(program, root)) {
		return false;
	}
	snprintf(path, sizeof(path), "%s/resources/app/product.json", root);
	char *product = read_file(path, NULL);
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

/* Past whitespace and JSONC comments from p; NULL in an unterminated comment. */
static const char *skip_blank(const char *p, const char *end) {
	while (p < end) {
		if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
			p++;
		} else if (end - p >= 2 && p[0] == '/' && p[1] == '/') {
			while (p < end && *p != '\n') {
				p++;
			}
		} else if (end - p >= 2 && p[0] == '/' && p[1] == '*') {
			const char *close = NULL;
			for (const char *q = p + 2; q + 1 < end && !close; q++) {
				close = q[0] == '*' && q[1] == '/' ? q : NULL;
			}
			if (!close) {
				return NULL;
			}
			p = close + 2;
		} else {
			break;
		}
	}
	return p;
}

/* Where a settings file's top-level object opens, read the way VS Code reads
 * it (JSON with comments and trailing commas): NULL unless the file is one
 * object with only whitespace and comments around it. *chosen is set if the
 * object has a "window.titleBarStyle" of its own; *empty if it has nothing. */
static const char *settings_object(const char *text, size_t len, bool *chosen, bool *empty) {
	static const char key[] = "window.titleBarStyle";
	const char *end = text + len;
	const char *p = text;
	if (len >= 3 && memcmp(p, "\xEF\xBB\xBF", 3) == 0) {
		p += 3;
	}
	p = skip_blank(p, end);
	if (!p || p == end || *p != '{') {
		return NULL;
	}
	const char *open = p++;
	const char *first = skip_blank(p, end);
	*empty = first && first < end && *first == '}';
	*chosen = false;
	int depth = 1;
	while (depth > 0) {
		if (!(p = skip_blank(p, end)) || p == end) {
			return NULL;
		}
		if (*p == '"') {
			const char *s = ++p;
			while (p < end && *p != '"') {
				p += *p == '\\' && p + 1 < end ? 2 : 1;
			}
			if (p >= end) {
				return NULL;
			}
			const char *after = skip_blank(p + 1, end);
			*chosen |= depth == 1 && after && after < end && *after == ':' &&
				(size_t)(p - s) == sizeof(key) - 1 && memcmp(s, key, sizeof(key) - 1) == 0;
		} else if (*p == '{' || *p == '[') {
			depth++;
		} else if (*p == '}' || *p == ']') {
			depth--;
		}
		p++;
	}
	p = skip_blank(p, end);
	return p == end ? open : NULL;
}

bool pl_code_use_native_title_bar(const char *settings) {
	static const char setting[] = "\"window.titleBarStyle\": \"native\"";
	/* Write through a symlink (settings kept with dotfiles) to its file. */
	char target[PATH_MAX];
	if (!realpath(settings, target)) {
		if (errno != ENOENT || snprintf(target, sizeof(target), "%s", settings) >= (int)sizeof(target)) {
			return false;
		}
		struct stat st;
		if (lstat(settings, &st) == 0) {
			return false; /* a symlink to nothing: not ours to replace */
		}
	}
	size_t len = 0;
	char *text = read_file(target, &len);
	if (!text && errno != ENOENT) {
		return false; /* there but unreadable: never write over it */
	}
	bool blank = !text || text[strspn(text, " \t\r\n")] == '\0';
	bool chosen = false, empty = false;
	const char *brace = blank ? NULL : settings_object(text, len, &chosen, &empty);
	if (!blank && (!brace || chosen)) {
		/* Chosen already, whichever way (theirs to keep), or not a settings
		 * file this understands: leave it as it is. */
		free(text);
		return false;
	}
	struct stat st;
	const bool existed = text && stat(target, &st) == 0;
	char tmp[PATH_MAX + 8];
	snprintf(tmp, sizeof(tmp), "%s.zacos9", target);
	make_parents(tmp);
	FILE *f = fopen(tmp, "w");
	if (!f) {
		free(text);
		return false;
	}
	if (existed) {
		fchmod(fileno(f), st.st_mode & 07777);
	}
	if (brace) {
		/* A member straight after the object's opening brace; VS Code
		 * accepts the trailing comma, but an empty object gets none. */
		fwrite(text, 1, brace + 1 - text, f);
		fprintf(f, empty ? "\n    %s" : "\n    %s,", setting);
		fwrite(brace + 1, 1, len - (brace + 1 - text), f);
	} else {
		fprintf(f, "{\n    %s\n}\n", setting);
	}
	free(text);
	bool ok = !ferror(f);
	ok = fclose(f) == 0 && ok && rename(tmp, target) == 0;
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
	/* "output NAME WxH scale S nested N x N y N main N", one per screen;
	 * every screen has the same scale, but the main one is authoritative.
	 * At 1.25 and 1.5 X11 programs are drawn at 1x and enlarged
	 * (compositor/src/xwayland.c), so those count as 1. */
	int scale = 1;
	bool have_main = false;
	char line[512];
	while (fgets(line, sizeof(line), f)) {
		char name[128];
		int w, h, nested, x, y, main;
		double s;
		if (sscanf(line, "output %127s %dx%d scale %lf nested %d x %d y %d main %d",
				name, &w, &h, &s, &nested, &x, &y, &main) == 8 && s >= 1 && !have_main) {
			scale = s == (int)s ? (int)s : 1;
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
