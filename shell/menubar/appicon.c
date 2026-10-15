/*
 * The front program's own icon for the Application menu's title, when its
 * ribbed handle has hidden the name (menus.c): the desktop entry's Icon=,
 * found as icon themes are (the theme's, then hicolor, then pixmaps) and
 * loaded with gdk-pixbuf at the title's height.
 */
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include "menubar.h"
#include "menudraw.h"

/* The ZacOS9 theme's icon theme (share/themes/ZacOS9/index.theme), then
 * the fallback every theme inherits. */
static const char *const themes[] = { "Adwaita", "hicolor" };
/* Nearest the title's height first: less scaling, crisper. */
static const int sizes[] = { 16, 22, 24, 32, 48, 64, 96, 128, 256, 512 };

static char *icon_name(const char *app_id) {
	char *desktop = app_desktop_file(app_id);
	if (!desktop) {
		return NULL;
	}
	GKeyFile *file = g_key_file_new();
	char *icon = g_key_file_load_from_file(file, desktop, G_KEY_FILE_NONE, NULL) ?
		g_key_file_get_string(file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_ICON, NULL) :
		NULL;
	g_key_file_free(file);
	free(desktop);
	return icon;
}

/* The data folders icons live under: the user's, then the system's. */
static char **data_dirs(void) {
	const char *const *system = g_get_system_data_dirs();
	GPtrArray *dirs = g_ptr_array_new();
	g_ptr_array_add(dirs, g_strdup(g_get_user_data_dir()));
	for (int i = 0; system[i]; i++) {
		g_ptr_array_add(dirs, g_strdup(system[i]));
	}
	g_ptr_array_add(dirs, NULL);
	return (char **)g_ptr_array_free(dirs, FALSE);
}

static char *find_icon(const char *name) {
	if (g_path_is_absolute(name)) {
		return g_file_test(name, G_FILE_TEST_IS_REGULAR) ? g_strdup(name) : NULL;
	}
	char **dirs = data_dirs();
	char *found = NULL;
	for (size_t t = 0; !found && t < G_N_ELEMENTS(themes); t++) {
		for (int d = 0; !found && dirs[d]; d++) {
			for (size_t s = 0; !found && s < G_N_ELEMENTS(sizes); s++) {
				char size[16];
				g_snprintf(size, sizeof(size), "%dx%d", sizes[s], sizes[s]);
				char *file = g_strconcat(name, ".png", NULL);
				char *path = g_build_filename(dirs[d], "icons", themes[t], size, "apps", file, NULL);
				g_free(file);
				if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
					found = path;
				} else {
					g_free(path);
				}
			}
			if (!found) {
				char *file = g_strconcat(name, ".svg", NULL);
				char *path = g_build_filename(dirs[d], "icons", themes[t], "scalable", "apps", file, NULL);
				g_free(file);
				if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
					found = path;
				} else {
					g_free(path);
				}
			}
		}
	}
	for (int d = 0; !found && dirs[d]; d++) {
		for (const char *const *ext = (const char *const[]){ ".png", ".svg", ".xpm", NULL }; *ext; ext++) {
			char *file = g_strconcat(name, *ext, NULL);
			char *path = g_build_filename(dirs[d], "pixmaps", file, NULL);
			g_free(file);
			if (!found && g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
				found = path;
			} else {
				g_free(path);
			}
		}
	}
	g_strfreev(dirs);
	return found;
}

/* MBAR_ICON_W x MBAR_ICON_H straight-alpha ARGB, the icon centred. */
static uint32_t *load(const char *path) {
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(path, MBAR_ICON_H, MBAR_ICON_H, TRUE, NULL);
	if (!pixbuf) {
		return NULL;
	}
	const int w = gdk_pixbuf_get_width(pixbuf), h = gdk_pixbuf_get_height(pixbuf);
	const int n = gdk_pixbuf_get_n_channels(pixbuf), stride = gdk_pixbuf_get_rowstride(pixbuf);
	const bool alpha = gdk_pixbuf_get_has_alpha(pixbuf);
	const guchar *src = gdk_pixbuf_read_pixels(pixbuf);
	uint32_t *px = calloc(MBAR_ICON_W * MBAR_ICON_H, sizeof(*px));
	const int x0 = (MBAR_ICON_W - w) / 2, y0 = (MBAR_ICON_H - h) / 2;
	for (int y = 0; px && y < h && y0 + y < MBAR_ICON_H; y++) {
		for (int x = 0; x < w && x0 + x < MBAR_ICON_W; x++) {
			const guchar *p = src + y * stride + x * n;
			const uint32_t a = alpha ? p[3] : 255;
			px[(y0 + y) * MBAR_ICON_W + x0 + x] = a << 24 | (uint32_t)p[0] << 16 |
				(uint32_t)p[1] << 8 | p[2];
		}
	}
	g_object_unref(pixbuf);
	return px;
}

const uint32_t *app_icon_title(const char *app_id) {
	static struct {
		char *app_id;
		uint32_t *px; /* NULL: it has none we can load */
	} cache[64];
	static int n_cache;
	for (int i = 0; i < n_cache; i++) {
		if (strcmp(cache[i].app_id, app_id) == 0) {
			return cache[i].px;
		}
	}
	char *name = icon_name(app_id);
	char *path = name && *name ? find_icon(name) : NULL;
	uint32_t *px = path ? load(path) : NULL;
	g_free(path);
	g_free(name);
	if (n_cache < 64) {
		cache[n_cache].app_id = strdup(app_id);
		cache[n_cache].px = px;
		n_cache++;
	}
	return px;
}
