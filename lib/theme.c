#include "theme.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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