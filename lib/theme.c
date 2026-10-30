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

/* The active theme bundle's directory (from "appearance-theme"), or false. */
static bool active_theme_dir(char *out, size_t size) {
	char id[256];
	if (!pl_setting("appearance-theme", id, sizeof(id)) || !id[0]) {
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

bool zac_theme_asset(const char *relative, char *out, size_t size) {
	if (!relative || !out || size == 0) return false;
	char dir[4096];
	if (!active_theme_dir(dir, sizeof(dir))) {
		return false;
	}
	char path[8192];
	snprintf(path, sizeof(path), "%s/%s", dir, relative);
	if (access(path, R_OK) == 0) {
		snprintf(out, size, "%s", path);
		return true;
	}
	return false;
}

/* The active theme's theme.conf lines, cached until the file changes. */
static struct {
	struct timespec mtime;
	bool loaded;
	int n;
	char lines[64][256];
} confcache;

static void conf_load(void) {
	char dir[4096];
	if (!active_theme_dir(dir, sizeof(dir))) {
		confcache.loaded = true;
		confcache.n = 0;
		confcache.mtime = (struct timespec){ 0 };
		return;
	}
	char path[8192];
	snprintf(path, sizeof(path), "%s/theme.conf", dir);
	struct stat st;
	if (stat(path, &st) != 0) {
		confcache.loaded = true;
		confcache.n = 0;
		confcache.mtime = (struct timespec){ 0 };
		return;
	}
	if (confcache.loaded && confcache.mtime.tv_sec == st.st_mtim.tv_sec &&
			confcache.mtime.tv_nsec == st.st_mtim.tv_nsec) {
		return;
	}
	confcache.loaded = true;
	confcache.mtime = st.st_mtim;
	confcache.n = 0;
	FILE *f = fopen(path, "r");
	if (!f) {
		return;
	}
	while (confcache.n < 64 && fgets(confcache.lines[confcache.n], sizeof(confcache.lines[0]), f)) {
		confcache.lines[confcache.n][strcspn(confcache.lines[confcache.n], "\r\n")] = '\0';
		confcache.n++;
	}
	fclose(f);
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