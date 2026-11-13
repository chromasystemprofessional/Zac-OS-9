#pragma once

/*
 * ZacOS themes: a directory containing a theme.conf (key=value) plus
 * icons/, patterns/ and sounds/.
 *
 * A user theme is a bundle the Theme Editor writes. Its theme.conf may name
 * another bundle with "inherits=<id>": its own files win, and the inherited
 * bundle supplies the artwork (icons, chrome, cursors, fonts) it does not
 * carry. One level only; an inherited bundle's own "inherits" is ignored.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

/* Resolve a theme-provided asset to an absolute path when the active theme
 * bundle ("theme-bundle" in desktop.conf) supplies it. `relative` is the
 * path inside the bundle (e.g. "boot/happy-zac-1x.png" or "menu-icon.png").
 * Returns false when no active theme has that file. */
bool zac_theme_asset(const char *relative, char *out, size_t size);

/* The Xcursor theme the active bundle ships (cursors/Macintosh/ with an
 * index.theme): its name for XCURSOR_THEME and its directory for
 * XCURSOR_PATH. False when the bundle has none: keep ZacOS9's. */
bool zac_cursor_theme(char *name, size_t nsize, char *dir, size_t dsize);

/* Apply the active theme bundle's appearance settings (accent, highlight,
 * pattern, alert-sound, background, wallpaper-mode) from its theme.conf to
 * the desktop settings. Returns how many were applied. */
int zac_theme_apply(void);

/* Read a colour from the active theme's theme.conf ("key=RRGGBB"). Returns
 * false (leaving *out untouched) when the key is unset or malformed. */
bool zac_theme_color(const char *key, uint32_t *out);

/* Read a raw string value from the active theme's theme.conf. Returns
 * false (leaving out untouched) when the key is unset or too long. */
bool zac_theme_string(const char *key, char *out, size_t size);

/* Pick ink that reads on `background`: white on a dark fill, black on a
 * light one. */
uint32_t zac_ink_for(uint32_t background);

/* Read a colour `key`; when it is unset, pick ink that reads on
 * `background`. `out` is always set. True when the key itself was used. */
bool zac_theme_ink(const char *key, uint32_t background, uint32_t *out);

/* Blend two opaque colours: 0 keeps `from`, 100 gives `to`. */
uint32_t zac_blend(uint32_t from, uint32_t to, int percent);

#ifdef __cplusplus
}
#endif