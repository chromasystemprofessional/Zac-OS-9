#include "locate.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Folders a build tree can be above a program: build/shell/<folder>/ is two. */
#define BUILD_TREE_DEPTH 3

static bool fits(int n, size_t size) {
	return n >= 0 && (size_t)n < size;
}

static bool is_dir(const char *path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool is_program(const char *path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, X_OK) == 0;
}

/* The folder this program's binary is in. */
static bool own_folder(char *out, size_t size) {
	char exe[PATH_MAX];
	const ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (len <= 0) {
		return false;
	}
	exe[len] = '\0';
	char *slash = strrchr(exe, '/');
	if (!slash || slash == exe) {
		return false;
	}
	*slash = '\0';
	return fits(snprintf(out, size, "%s", exe), size);
}

bool pl_build_tree_from(const char *dir, char *out, size_t size) {
	char path[PATH_MAX];
	if (!fits(snprintf(path, sizeof(path), "%s", dir), sizeof(path))) {
		return false;
	}
	for (int up = 0; up <= BUILD_TREE_DEPTH; up++) {
		char marker[PATH_MAX + 16];
		snprintf(marker, sizeof(marker), "%s/meson-private", path);
		if (is_dir(marker)) {
			return fits(snprintf(out, size, "%s", path), size);
		}
		char *slash = strrchr(path, '/');
		if (!slash || slash == path) {
			return false;
		}
		*slash = '\0';
	}
	return false;
}

bool pl_build_tree(char *out, size_t size) {
	char dir[PATH_MAX];
	return own_folder(dir, sizeof(dir)) && pl_build_tree_from(dir, out, size);
}

bool pl_build_path(const char *relative, char *out, size_t size) {
	char tree[PATH_MAX];
	return pl_build_tree(tree, sizeof(tree)) &&
		fits(snprintf(out, size, "%s/%s", tree, relative), size);
}

bool pl_source_path(const char *relative, char *out, size_t size) {
	char tree[PATH_MAX];
	return pl_build_tree(tree, sizeof(tree)) &&
		fits(snprintf(out, size, "%s/%s", ZACOS9_SOURCE_DIR, relative), size);
}

static bool found(const char *path, char *out, size_t size) {
	return is_program(path) && fits(snprintf(out, size, "%s", path), size);
}

bool pl_sibling_program_from(const char *dir, const char *name, char *out, size_t size) {
	char path[PATH_MAX];
	if (fits(snprintf(path, sizeof(path), "%s/%s", dir, name), sizeof(path)) &&
			found(path, out, size)) {
		return true;
	}
	char tree[PATH_MAX];
	if (!pl_build_tree_from(dir, tree, sizeof(tree))) {
		return false;
	}
	if (fits(snprintf(path, sizeof(path), "%s/shell/%s", tree, name), sizeof(path)) &&
			found(path, out, size)) {
		return true;
	}
	/* Each program in its own folder of build/shell/; in name order, so
	 * the same one is found every time. */
	char shell[PATH_MAX];
	if (!fits(snprintf(shell, sizeof(shell), "%s/shell", tree), sizeof(shell))) {
		return false;
	}
	struct dirent **folders;
	const int n = scandir(shell, &folders, NULL, alphasort);
	bool ok = false;
	for (int i = 0; i < n; i++) {
		if (!ok && folders[i]->d_name[0] != '.' &&
				fits(snprintf(path, sizeof(path), "%s/%s/%s", shell, folders[i]->d_name, name),
					sizeof(path))) {
			ok = found(path, out, size);
		}
		free(folders[i]);
	}
	if (n >= 0) {
		free(folders);
	}
	return ok;
}

bool pl_sibling_program(const char *name, char *out, size_t size) {
	char dir[PATH_MAX];
	return own_folder(dir, sizeof(dir)) && pl_sibling_program_from(dir, name, out, size);
}
