#include "theme.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#include "settings.h"

int zac_theme_load(ZacTheme *theme, const char *directory) {
	if (!theme || !directory) return -1;
	memset(theme, 0, sizeof(*theme));
	theme->directory = strdup(directory);
	/* Read name from theme.conf if present. */
	char path[4096];
	snprintf(path, sizeof(path), "%s/theme.conf", directory);
	FILE *f = fopen(path, "r");
	if (f) {
		char line[1024];
		while (fgets(line, sizeof(line), f)) {
			if (strncmp(line, "name=", 5) == 0) {
				char *nl = strchr(line, '\n');
				if (nl) *nl = '\0';
				free(theme->name);
				theme->name = strdup(line + 5);
			}
		}
		fclose(f);
	}
	if (!theme->name) {
		theme->name = strdup("Unnamed");
	}
	return 0;
}

void zac_theme_free(ZacTheme *theme) {
	if (!theme) return;
	free(theme->name);
	free(theme->directory);
	memset(theme, 0, sizeof(*theme));
}

int zac_theme_list(const char *root, char ***out_dirs) {
	if (!root || !out_dirs) return 0;
	*out_dirs = NULL;
	DIR *d = opendir(root);
	if (!d) return 0;
	/* First pass: count. */
	int count = 0;
	struct dirent *ent;
	while ((ent = readdir(d)) != NULL) {
		if (ent->d_name[0] == '.') continue;
		char path[4096];
		snprintf(path, sizeof(path), "%s/%s/theme.conf", root, ent->d_name);
		struct stat st;
		if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) count++;
	}
	if (count == 0) { closedir(d); return 0; }
	char **dirs = calloc(count, sizeof(char *));
	int i = 0;
	rewinddir(d);
	while ((ent = readdir(d)) != NULL && i < count) {
		if (ent->d_name[0] == '.') continue;
		char path[4096];
		snprintf(path, sizeof(path), "%s/%s/theme.conf", root, ent->d_name);
		struct stat st;
		if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
			char full[4096];
			snprintf(full, sizeof(full), "%s/%s", root, ent->d_name);
			dirs[i] = strdup(full);
			i++;
		}
	}
	closedir(d);
	*out_dirs = dirs;
	return i;
}

/* A bundle id from theme.conf or desktop.conf: one path component, no
 * separators or "..". */
static bool valid_bundle_id(const char *id) {
	if (!id || !*id || id[0] == '.' || strlen(id) >= 200) {
		return false;
	}
	for (const char *p = id; *p; p++) {
		if (*p == '/' || *p == '\\' || *p < 0x20) {
			return false;
		}
	}
	return strcmp(id, ".") != 0 && strcmp(id, "..") != 0;
}

/* A bundle by id: the user's own first, then the installed data themes. */
static bool theme_dir_for_id(const char *id, char *out, size_t size) {
	if (!valid_bundle_id(id)) {
		return false;
	}
	const char *home = getenv("HOME");
	const char *cfg = getenv("XDG_CONFIG_HOME");
	char base[4096];
	const char *dirs[3];
	int n = 0;
	if (cfg && *cfg) {
		snprintf(base, sizeof(base), "%s/zacos9/themes/%s", cfg, id);
		dirs[n++] = base;
	} else if (home && *home) {
		snprintf(base, sizeof(base), "%s/.config/zacos9/themes/%s", home, id);
		dirs[n++] = base;
	}
	char sysbase[4096];
	snprintf(sysbase, sizeof(sysbase), "%s/themes/%s", pl_data_dir(), id);
	dirs[n++] = sysbase;
	for (int i = 0; i < n; i++) {
		struct stat st;
		if (stat(dirs[i], &st) == 0 && S_ISDIR(st.st_mode)) {
			snprintf(out, size, "%s", dirs[i]);
			return true;
		}
	}
	return false;
}

/* The active theme bundle's directory (from "theme-bundle"), or false. */
static bool active_theme_dir(char *out, size_t size) {
	char id[256];
	if (!pl_setting("theme-bundle", id, sizeof(id)) || !id[0]) {
		return false;
	}
	return theme_dir_for_id(id, out, size);
}

/* The bundle the active theme inherits its artwork from ("inherits"),
 * or false. One level only. */
static bool inherited_theme_dir(char *out, size_t size) {
	char id[256], own[4096], inherited[4096];
	if (!zac_theme_string("inherits", id, sizeof(id)) || !id[0]) {
		return false;
	}
	if (!theme_dir_for_id(id, inherited, sizeof(inherited))) {
		return false;
	}
	if (active_theme_dir(own, sizeof(own)) && strcmp(own, inherited) == 0) {
		return false;
	}
	snprintf(out, size, "%s", inherited);
	return true;
}

/* A bundle-relative path that cannot escape its bundle. */
static bool safe_relative(const char *relative) {
	if (!relative || !*relative || relative[0] == '/') {
		return false;
	}
	const char *p = relative;
	while (*p) {
		const char *slash = strchr(p, '/');
		size_t n = slash ? (size_t)(slash - p) : strlen(p);
		if (n == 0 || (n == 1 && p[0] == '.') ||
				(n == 2 && p[0] == '.' && p[1] == '.')) {
			return false;
		}
		if (!slash) {
			break;
		}
		p = slash + 1;
	}
	return true;
}

bool zac_theme_asset(const char *relative, char *out, size_t size) {
	if (!relative || !out || size == 0 || !safe_relative(relative)) return false;
	char dir[4096], id[256];
	if (pl_setting("theme-bundle", id, sizeof(id)) && theme_dir_for_id(id, dir, sizeof(dir))) {
		char path[8192];
		snprintf(path, sizeof(path), "%s/%s", dir, relative);
		if (access(path, R_OK) == 0) {
			snprintf(out, size, "%s", path);
			return true;
		}
	}
	char inherited[4096];
	if (inherited_theme_dir(inherited, sizeof(inherited))) {
		char path[8192];
		snprintf(path, sizeof(path), "%s/%s", inherited, relative);
		if (access(path, R_OK) == 0) {
			snprintf(out, size, "%s", path);
			return true;
		}
	}
	return false;
}

/* The Xcursor theme the active bundle ships (cursors/Macintosh/ with an
 * index.theme): its name for XCURSOR_THEME and its directory for
 * XCURSOR_PATH. False when the bundle has none: keep ZacOS9's. */
bool zac_cursor_theme(char *name, size_t nsize, char *dir, size_t dsize) {
	if (!name || nsize == 0 || !dir || dsize == 0) {
		return false;
	}
	char bundle[4096];
	if (!active_theme_dir(bundle, sizeof(bundle))) {
		return false;
	}
	char index[8192];
	snprintf(index, sizeof(index), "%s/cursors/Macintosh/index.theme", bundle);
	struct stat st;
	if (stat(index, &st) == 0 && S_ISREG(st.st_mode)) {
		snprintf(name, nsize, "Macintosh");
		snprintf(dir, dsize, "%s/cursors", bundle);
		return true;
	}
	/* A linked copy may take its pointers from the bundle it inherits. */
	char inherited[4096];
	if (inherited_theme_dir(inherited, sizeof(inherited))) {
		snprintf(index, sizeof(index), "%s/cursors/Macintosh/index.theme", inherited);
		if (stat(index, &st) == 0 && S_ISREG(st.st_mode)) {
			snprintf(name, nsize, "Macintosh");
			snprintf(dir, dsize, "%s/cursors", inherited);
			return true;
		}
	}
	return false;
}

/* The active theme's theme.conf lines, cached until the file changes.
 * The path and size join the timestamp: a reimported bundle can land
 * inside one timestamp tick, and another bundle's file can share it. */
static struct {
	struct timespec mtime;
	off_t size;
	char path[8192];
	bool loaded;
	int n;
	char lines[128][512];
} confcache;

static void conf_load(void) {
	char dir[4096];
	if (!active_theme_dir(dir, sizeof(dir))) {
		confcache.loaded = true;
		confcache.n = 0;
		confcache.mtime = (struct timespec){ 0 };
		confcache.size = -1;
		confcache.path[0] = '\0';
		return;
	}
	char path[8192];
	snprintf(path, sizeof(path), "%s/theme.conf", dir);
	struct stat st;
	if (stat(path, &st) != 0) {
		confcache.loaded = true;
		confcache.n = 0;
		confcache.mtime = (struct timespec){ 0 };
		confcache.size = -1;
		confcache.path[0] = '\0';
		return;
	}
	if (confcache.loaded && confcache.mtime.tv_sec == st.st_mtim.tv_sec &&
			confcache.mtime.tv_nsec == st.st_mtim.tv_nsec &&
			confcache.size == st.st_size && strcmp(confcache.path, path) == 0) {
		return;
	}
	confcache.loaded = true;
	confcache.mtime = st.st_mtim;
	confcache.size = st.st_size;
	snprintf(confcache.path, sizeof(confcache.path), "%s", path);
	confcache.n = 0;
	FILE *f = fopen(path, "r");
	if (!f) {
		return;
	}
	while (confcache.n < 128 && fgets(confcache.lines[confcache.n], sizeof(confcache.lines[0]), f)) {
		confcache.lines[confcache.n][strcspn(confcache.lines[confcache.n], "\r\n")] = '\0';
		confcache.n++;
	}
	fclose(f);
}

/* Read a raw string value from the active theme's theme.conf. Returns
 * false (leaving out untouched) when the key is unset or too long. */
bool zac_theme_string(const char *key, char *out, size_t size) {
	if (!key || !out || size == 0) return false;
	conf_load();
	for (int i = 0; i < confcache.n; i++) {
		const char *eq = strchr(confcache.lines[i], '=');
		if (!eq || (size_t)(eq - confcache.lines[i]) != strlen(key)) {
			continue;
		}
		if (strncmp(confcache.lines[i], key, strlen(key)) != 0) {
			continue;
		}
		const char *v = eq + 1;
		if (strlen(v) == 0 || strlen(v) >= size) {
			return false;
		}
		memcpy(out, v, strlen(v) + 1);
		return true;
	}
	return false;
}

bool zac_theme_color(const char *key, uint32_t *out) {
	if (!key || !out) return false;
	conf_load();
	for (int i = 0; i < confcache.n; i++) {
		const char *eq = strchr(confcache.lines[i], '=');
		if (!eq || (size_t)(eq - confcache.lines[i]) != strlen(key)) {
			continue;
		}
		if (strncmp(confcache.lines[i], key, strlen(key)) != 0) {
			continue;
		}
		const char *v = eq + 1;
		if (strlen(v) != 6) {
			return false;
		}
		unsigned long rgb = 0;
		for (const char *p = v; *p; p++) {
			int d;
			if (*p >= '0' && *p <= '9') d = *p - '0';
			else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
			else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
			else return false;
			rgb = rgb * 16 + d;
		}
		*out = 0xFF000000u | (uint32_t)rgb;
		return true;
	}
	return false;
}

uint32_t zac_ink_for(uint32_t background) {
	const int r = (int)((background >> 16) & 0xFF);
	const int g = (int)((background >> 8) & 0xFF);
	const int b = (int)(background & 0xFF);
	/* Rec. 601 luma, integer only: what reads as light on a screen. */
	const int luma = (r * 299 + g * 587 + b * 114) / 1000;
	return luma < 128 ? 0xFFFFFFFFu : 0xFF000000u;
}

bool zac_theme_ink(const char *key, uint32_t background, uint32_t *out) {
	if (!out) return false;
	if (key && zac_theme_color(key, out)) {
		return true;
	}
	*out = zac_ink_for(background);
	return false;
}

uint32_t zac_blend(uint32_t from, uint32_t to, int percent) {
	if (percent <= 0) return from;
	if (percent >= 100) return to;
	const int f[4] = {
		(int)((from >> 16) & 0xFF), (int)((from >> 8) & 0xFF),
		(int)(from & 0xFF), (int)((from >> 24) & 0xFF),
	};
	const int t[4] = {
		(int)((to >> 16) & 0xFF), (int)((to >> 8) & 0xFF),
		(int)(to & 0xFF), (int)((to >> 24) & 0xFF),
	};
	int v[4];
	for (int i = 0; i < 4; i++) {
		v[i] = (f[i] * (100 - percent) + t[i] * percent + 50) / 100;
	}
	return ((uint32_t)v[3] << 24) | ((uint32_t)v[0] << 16) |
		((uint32_t)v[1] << 8) | (uint32_t)v[2];
}

int zac_theme_apply(void) {
	char dir[4096];
	if (!active_theme_dir(dir, sizeof(dir))) {
		return 0;
	}
	char path[8192];
	snprintf(path, sizeof(path), "%s/theme.conf", dir);
	FILE *f = fopen(path, "r");
	if (!f) {
		return 0;
	}
	int applied = 0;
	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		char *nl = strchr(line, '\n');
		if (nl) *nl = '\0';
		char *eq = strchr(line, '=');
		if (!eq) {
			continue;
		}
		*eq = '\0';
		const char *key = line;
		const char *value = eq + 1;
		static const char *const keys[] = { "accent", "highlight", "pattern",
			"alert-sound", "background", "wallpaper-mode" };
		for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
			if (strcmp(key, keys[i]) == 0) {
				pl_setting_set(key, value);
				applied++;
				break;
			}
		}
	}
	fclose(f);
	return applied;
}