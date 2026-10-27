/*
 * Finding the desktop's other programs (locate.c): beside each other when
 * installed, and in a build tree whether its programs share build/shell/
 * or each has a folder of it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "locate.h"

static int fails;
static char root[] = "/tmp/zacos9-locate-XXXXXX";

static void check(bool ok, const char *what) {
	printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

/* root/relative, with its folders; a program (executable) unless `mode` says otherwise. */
static const char *make(const char *relative, mode_t mode) {
	static char path[512];
	snprintf(path, sizeof(path), "%s/%s", root, relative);
	for (char *slash = strchr(path + strlen(root) + 1, '/'); slash; slash = strchr(slash + 1, '/')) {
		*slash = '\0';
		mkdir(path, 0700);
		*slash = '/';
	}
	if (mode == 0) {
		mkdir(path, 0700);
	} else {
		FILE *f = fopen(path, "w");
		fputs("#!/bin/sh\n", f);
		fclose(f);
		chmod(path, mode);
	}
	return path;
}

static bool finds(const char *from, const char *name, const char *expected) {
	char dir[512], out[512], want[512];
	snprintf(dir, sizeof(dir), "%s/%s", root, from);
	if (!expected) {
		return !pl_sibling_program_from(dir, name, out, sizeof(out));
	}
	snprintf(want, sizeof(want), "%s/%s", root, expected);
	return pl_sibling_program_from(dir, name, out, sizeof(out)) && strcmp(out, want) == 0;
}

int main(void) {
	if (!mkdtemp(root)) {
		return 1;
	}
	/* Installed: beside each other. */
	make("usr/bin/zacos9-menubar", 0755);
	make("usr/bin/zacos9-store", 0755);
	check(finds("usr/bin", "zacos9-store", "usr/bin/zacos9-store"), "installed: the one beside it");
	check(finds("usr/bin", "zacos9-nothing", NULL), "installed: one not there isn't found ($PATH then)");
	char out[512], dir[512];
	snprintf(dir, sizeof(dir), "%s/usr/bin", root);
	check(!pl_build_tree_from(dir, out, sizeof(out)), "installed: not a build tree");

	/* A build tree, each of the shell's programs in its own folder. */
	make("build/meson-private", 0);
	make("build/compositor/zacos9-wm", 0755);
	make("build/shell/menubar/zacos9-menubar", 0755);
	make("build/shell/store/zacos9-store", 0755);
	snprintf(dir, sizeof(dir), "%s/build/shell/menubar", root);
	char want[512];
	snprintf(want, sizeof(want), "%s/build", root);
	check(pl_build_tree_from(dir, out, sizeof(out)) && strcmp(out, want) == 0,
		"build tree: found two folders up");
	check(finds("build/shell/menubar", "zacos9-store", "build/shell/store/zacos9-store"),
		"build tree: a program in another folder of shell/");
	check(finds("build/compositor", "zacos9-menubar", "build/shell/menubar/zacos9-menubar"),
		"build tree: the compositor finds the menu bar");

	/* A build tree with the shell's programs side by side in build/shell/. */
	make("build/shell/zacos9-finder", 0755);
	check(finds("build/compositor", "zacos9-finder", "build/shell/zacos9-finder"),
		"build tree: a program in shell/ itself");

	/* Only programs: not a folder of the name, nor a file that can't run. */
	make("build/shell/a/zacos9-folder", 0);
	make("build/shell/b/zacos9-folder", 0755);
	make("build/shell/a/zacos9-text", 0644);
	check(finds("build/compositor", "zacos9-folder", "build/shell/b/zacos9-folder"),
		"a folder named like a program is passed over");
	check(finds("build/compositor", "zacos9-text", NULL), "a file that can't run isn't a program");
	make("build/shell/c/zacos9-twice", 0755);
	make("build/shell/d/zacos9-twice", 0755);
	check(finds("build/compositor", "zacos9-twice", "build/shell/c/zacos9-twice"),
		"two of a name: the first folder's, every time");

	/* Too far below a build tree to be one of its programs. */
	make("build/a/b/c/d/zacos9-deep", 0755);
	snprintf(dir, sizeof(dir), "%s/build/a/b/c/d", root);
	check(!pl_build_tree_from(dir, out, sizeof(out)), "four folders down isn't counted");

	/* This test itself runs from the build tree, so the source tree is found. */
	check(pl_build_tree(out, sizeof(out)), "this test's own build tree");
	struct stat st;
	check(pl_source_path("lib/locate.c", out, sizeof(out)) && stat(out, &st) == 0,
		"the source tree, from the build tree");

	char cleanup[600];
	snprintf(cleanup, sizeof(cleanup), "rm -rf '%s'", root);
	if (system(cleanup) != 0) {
		fails++;
	}
	return fails ? 1 : 0;
}
