#pragma once

/*
 * ZacOS themes: a directory containing a theme.conf (key=value) plus
 * icons/, patterns/ and sounds/. Step 1 of the theming feature only defines
 * the bundle format and a reader; no theme ships yet.
 */

#include <stddef.h>

typedef struct {
	char *name;            /* theme name */
	char *directory;       /* absolute path to the bundle */
} ZacTheme;

/* Load a theme from `directory`. Returns 0 on success. */
int zac_theme_load(ZacTheme *theme, const char *directory);

/* Free a loaded theme. */
void zac_theme_free(ZacTheme *theme);

/* List theme bundle directories under `root` (one level). Caller frees the
 * returned array and each string. Returns count (0 if none). */
int zac_theme_list(const char *root, char ***out_dirs);