#include "controlpanels.h"

#include <glib.h>
#include <glib/gstdio.h>
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

/* The panel in `path`, unless it's not a panel, or is hidden (and
 * `hidden_ok` is false): false then. */
static bool read_panel(const char *path, const char *id, struct pl_control_panel *panel,
		bool hidden_ok) {
	GKeyFile *file = g_key_file_new();
	bool ok = g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, NULL);
	if (ok && !hidden_ok) {
		ok = !g_key_file_get_boolean(file, G_KEY_FILE_DESKTOP_GROUP,
				G_KEY_FILE_DESKTOP_KEY_HIDDEN, NULL) &&
			!g_key_file_get_boolean(file, G_KEY_FILE_DESKTOP_GROUP,
				G_KEY_FILE_DESKTOP_KEY_NO_DISPLAY, NULL);
	}
	ok = ok && has_category(file);
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

static void free_panel(struct pl_control_panel *panel) {
	g_free(panel->id);
	g_free(panel->name);
	g_free(panel->program);
	g_free(panel->args);
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
			if (read_panel(path, id, &panel, FALSE)) {
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
		free_panel(&panels[i]);
	}
	g_free(panels);
}

/* True when `path` loads and its entry hides itself: the user's way of
 * turning a panel off, whether by the copy written below or by hand. */
static bool hides_panel(const char *path) {
	GKeyFile *file = g_key_file_new();
	const bool ok = g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, NULL) &&
		(g_key_file_get_boolean(file, G_KEY_FILE_DESKTOP_GROUP,
			 G_KEY_FILE_DESKTOP_KEY_NO_DISPLAY, NULL) ||
		 g_key_file_get_boolean(file, G_KEY_FILE_DESKTOP_GROUP,
			 G_KEY_FILE_DESKTOP_KEY_HIDDEN, NULL));
	g_key_file_free(file);
	return ok;
}

/* The system's own entry for `id`, wherever it is installed, or NULL. */
static char *system_entry_path(const char *id) {
	const char *const *system = g_get_system_data_dirs();
	for (int i = 0; system[i]; i++) {
		char *path = g_build_filename(system[i], "applications", id, NULL);
		if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
			return path;
		}
		g_free(path);
	}
	return NULL;
}

struct pl_control_panel *pl_control_panels_disabled(size_t *count) {
	GArray *panels = g_array_new(FALSE, TRUE, sizeof(struct pl_control_panel));
	char *own_dir = g_build_filename(g_get_user_data_dir(), "applications", NULL);
	/* The system's folders only, in its order: a panel the user installed
	 * themselves has nothing to go back to, and one the system ships
	 * hidden was never on. */
	GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	const char *const *system = g_get_system_data_dirs();
	for (int i = 0; system[i]; i++) {
		char *dir = g_build_filename(system[i], "applications", NULL);
		GDir *listing = g_dir_open(dir, 0, NULL);
		for (const char *id; listing && (id = g_dir_read_name(listing));) {
			if (!g_str_has_suffix(id, ".desktop") || g_hash_table_contains(seen, id)) {
				continue;
			}
			g_hash_table_add(seen, g_strdup(id));
			char *path = g_build_filename(dir, id, NULL);
			char *own = g_build_filename(own_dir, id, NULL);
			struct pl_control_panel panel;
			if (!read_panel(path, id, &panel, FALSE) || !hides_panel(own)) {
				g_free(own);
				g_free(path);
				continue;
			}
			/* The user's copy first, as the reader's order: it may have
			 * the name they gave the panel. One that only hides the entry
			 * has no fields of its own, and the system's stand. */
			struct pl_control_panel own_panel;
			if (read_panel(own, id, &own_panel, TRUE)) {
				free_panel(&panel);
				panel = own_panel;
			}
			g_array_append_val(panels, panel);
			g_free(own);
			g_free(path);
		}
		if (listing) {
			g_dir_close(listing);
		}
		g_free(dir);
	}
	g_hash_table_destroy(seen);
	g_free(own_dir);
	qsort(panels->data, panels->len, sizeof(struct pl_control_panel), by_name);
	*count = panels->len;
	return (struct pl_control_panel *)g_array_free(panels, FALSE);
}

bool pl_control_panel_set_enabled(const char *id, bool enabled) {
	if (!id || !*id || strchr(id, '/') || !g_str_has_suffix(id, ".desktop")) {
		return false; /* an entry's own file name, and nothing else */
	}
	char *own_dir = g_build_filename(g_get_user_data_dir(), "applications", NULL);
	char *own = g_build_filename(own_dir, id, NULL);
	bool ok;
	if (enabled) {
		/* The system's entry is the panel again: the user's copy, the one
		 * that hid it, goes. */
		ok = !g_file_test(own, G_FILE_TEST_EXISTS) || g_remove(own) == 0;
	} else {
		ok = false;
		char *path = system_entry_path(id);
		struct pl_control_panel panel;
		if (path && read_panel(path, id, &panel, FALSE)) {
			free_panel(&panel);
			GKeyFile *file = g_key_file_new();
			ok = g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, NULL);
			if (ok) {
				/* NoDisplay, not Hidden: the Mac's (Disabled) folder held
				 * the entry itself, still a control panel, for the user to
				 * drag back. */
				g_key_file_set_boolean(file, G_KEY_FILE_DESKTOP_GROUP,
					G_KEY_FILE_DESKTOP_KEY_NO_DISPLAY, TRUE);
				g_key_file_remove_key(file, G_KEY_FILE_DESKTOP_GROUP,
					G_KEY_FILE_DESKTOP_KEY_HIDDEN, NULL);
				ok = g_mkdir_with_parents(own_dir, 0700) == 0 &&
					g_key_file_save_to_file(file, own, NULL);
			}
			g_key_file_free(file);
		}
		g_free(path);
	}
	g_free(own);
	g_free(own_dir);
	return ok;
}
