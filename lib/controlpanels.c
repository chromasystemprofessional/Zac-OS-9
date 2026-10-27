#include "controlpanels.h"

#include <glib.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static bool has_category(GKeyFile *file) {
	gsize n = 0;
	char **categories = g_key_file_get_string_list(file, G_KEY_FILE_DESKTOP_GROUP,
		G_KEY_FILE_DESKTOP_KEY_CATEGORIES, &n, NULL);
	const bool found = categories &&
		g_strv_contains((const char *const *)categories, "X-ZacOS9-ControlPanel");
	g_strfreev(categories);
	return found;
}

/* The panel in `path`, unless it's hidden or not a panel: false then. */
static bool read_panel(const char *path, const char *id, struct pl_control_panel *panel) {
	GKeyFile *file = g_key_file_new();
	bool ok = g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, NULL) &&
		!g_key_file_get_boolean(file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_HIDDEN, NULL) &&
		!g_key_file_get_boolean(file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_NO_DISPLAY, NULL) &&
		has_category(file);
	char *name = ok ? g_key_file_get_locale_string(file, G_KEY_FILE_DESKTOP_GROUP,
		G_KEY_FILE_DESKTOP_KEY_NAME, NULL, NULL) : NULL;
	char *exec = ok ? g_key_file_get_string(file, G_KEY_FILE_DESKTOP_GROUP,
		G_KEY_FILE_DESKTOP_KEY_EXEC, NULL) : NULL;
	g_key_file_free(file);
	ok = name && *name && exec && *exec;
	if (ok) {
		const size_t program = strcspn(exec, " ");
		*panel = (struct pl_control_panel){
			.id = g_strdup(id),
			.name = name,
			.program = g_strndup(exec, program),
			.args = g_strdup(exec + program),
			.icon = pl_icon_for_program(id),
		};
		name = NULL;
	}
	g_free(name);
	g_free(exec);
	return ok;
}

static int by_name(const void *a, const void *b) {
	return g_ascii_strcasecmp(((const struct pl_control_panel *)a)->name,
		((const struct pl_control_panel *)b)->name);
}

struct pl_control_panel *pl_control_panels(size_t *count) {
	GArray *panels = g_array_new(FALSE, TRUE, sizeof(struct pl_control_panel));
	/* The user's data folder, then the system's, in order: the first entry
	 * of an id is the one that counts, even one that hides the panel. Read
	 * here rather than through GIO, which leaves out an entry whose program
	 * isn't installed: a build tree's panels, run from there. */
	GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	const char *const *system = g_get_system_data_dirs();
	for (int i = -1; i < 0 || system[i]; i++) {
		char *dir = g_build_filename(i < 0 ? g_get_user_data_dir() : system[i], "applications", NULL);
		GDir *listing = g_dir_open(dir, 0, NULL);
		for (const char *id; listing && (id = g_dir_read_name(listing));) {
			if (!g_str_has_suffix(id, ".desktop") || g_hash_table_contains(seen, id)) {
				continue;
			}
			g_hash_table_add(seen, g_strdup(id));
			char *path = g_build_filename(dir, id, NULL);
			struct pl_control_panel panel;
			if (read_panel(path, id, &panel)) {
				g_array_append_val(panels, panel);
			}
			g_free(path);
		}
		if (listing) {
			g_dir_close(listing);
		}
		g_free(dir);
	}
	g_hash_table_destroy(seen);
	qsort(panels->data, panels->len, sizeof(struct pl_control_panel), by_name);
	*count = panels->len;
	return (struct pl_control_panel *)g_array_free(panels, FALSE);
}

void pl_control_panels_free(struct pl_control_panel *panels, size_t count) {
	for (size_t i = 0; i < count; i++) {
		g_free(panels[i].id);
		g_free(panels[i].name);
		g_free(panels[i].program);
		g_free(panels[i].args);
	}
	g_free(panels);
}
