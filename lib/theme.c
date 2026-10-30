#include "theme.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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