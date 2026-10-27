#ifndef ZACOS9_LOCATE_H
#define ZACOS9_LOCATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

/*
 * Finding the desktop's other programs from wherever this one runs.
 * Installed, they're beside each other (bindir, and libexecdir/zacos9 for
 * the helpers). Run from a build tree, each is in its part's folder of it
 * (build/compositor/, build/shell/ or a folder of build/shell/,
 * build/network/afp/...), and the root helpers are scripts in the source
 * tree. The shell's Qt programs use these through shell/common/programs.h.
 */

/* The build tree this program runs from (the folder holding meson-private/,
 * this program's own or one of the three above it), into `out`; false when
 * it's installed. */
bool pl_build_tree(char *out, size_t size);

/* `relative` under the build tree this program runs from, or under the
 * source tree that was built, into `out`; false when it's installed. */
bool pl_build_path(const char *relative, char *out, size_t size);
bool pl_source_path(const char *relative, char *out, size_t size);

/* Another of the desktop's programs: beside this one, else, when this one
 * runs from a build tree, in its shell/ folder or one of shell/'s own
 * folders. False when neither has it: run `name` from $PATH. */
bool pl_sibling_program(const char *name, char *out, size_t size);

/* The same as if this program were in the folder `dir` (for tests). */
bool pl_build_tree_from(const char *dir, char *out, size_t size);
bool pl_sibling_program_from(const char *dir, const char *name, char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif
