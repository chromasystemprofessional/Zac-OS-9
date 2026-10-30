#pragma once

/*
 * ZacOS themes: a directory containing a theme.conf (key=value) plus
 * icons/, patterns/ and sounds/. Step 1 of the theming feature only defines
 * the bundle format and a reader; no theme ships yet.
 */

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/* Resolve a theme-provided asset to an absolute path when the active
 * appearance theme ("appearance-theme" in desktop.conf) supplies it.
 * `relative` is the path inside the bundle (e.g. "boot/happy-zac-1x.png" or
 * "menu-icon.png"). Returns false when no active theme has that file. */
bool zac_theme_asset(const char *relative, char *out, size_t size);

/* Apply the active theme bundle's appearance settings (accent, highlight,
 * pattern, alert-sound, background, wallpaper-mode) from its theme.conf to
 * the desktop settings. Returns how many were applied. */
int zac_theme_apply(void);

#ifdef __cplusplus
}
#endif