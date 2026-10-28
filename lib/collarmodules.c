#include "collarmodules.h"

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
		g_strv_contains((const char *const *)categories, "X-ZacOS9-CollarModule");
	g_strfreev(categories);
	return found;
}

/* The module in `path`, unless it's not a module, or is hidden (and
 * `hidden_ok` is false): false then. */
static bool read_module(const char *path, const char *id, struct pl_collar_module *module,
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
		*module = (struct pl_collar_module){
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
	return g_ascii_strcasecmp(((const struct pl_collar_module *)a)->name,
		((const struct pl_collar_module *)b)->name);
}

static void free_module(struct pl_collar_module *module) {
	g_free(module->id);
	g_free(module->name);
	g_free(module->program);
	g_free(module->args);
}

struct pl_collar_module *pl_collar_modules(size_t *count) {
	GArray *modules = g_array_new(FALSE, TRUE, sizeof(struct pl_collar_module));
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
			struct pl_collar_module module;
			if (read_module(path, id, &module, FALSE)) {
				g_array_append_val(modules, module);
			}
			g_free(path);
		}
		if (listing) {
			g_dir_close(listing);
		}
		g_free(dir);
	}
	g_hash_table_destroy(seen);
	qsort(modules->data, modules->len, sizeof(struct pl_collar_module), by_name);
	*count = modules->len;
	return (struct pl_collar_module *)g_array_free(modules, FALSE);
}

void pl_collar_modules_free(struct pl_collar_module *modules, size_t count) {
	for (size_t i = 0; i < count; i++) {
		free_module(&modules[i]);
	}
	g_free(modules);
}

static bool hides_module(const char *path) {
	GKeyFile *file = g_key_file_new();
	const bool ok = g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, NULL) &&
		(g_key_file_get_boolean(file, G_KEY_FILE_DESKTOP_GROUP,
			 G_KEY_FILE_DESKTOP_KEY_NO_DISPLAY, NULL) ||
		 g_key_file_get_boolean(file, G_KEY_FILE_DESKTOP_GROUP,
			 G_KEY_FILE_DESKTOP_KEY_HIDDEN, NULL));
	g_key_file_free(file);
	return ok;
}

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

struct pl_collar_module *pl_collar_modules_disabled(size_t *count) {
	GArray *modules = g_array_new(FALSE, TRUE, sizeof(struct pl_collar_module));
	char *own_dir = g_build_filename(g_get_user_data_dir(), "applications", NULL);
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
			struct pl_collar_module module;
			if (!read_module(path, id, &module, FALSE) || !hides_module(own)) {
				g_free(own);
				g_free(path);
				continue;
			}
			struct pl_collar_module own_module;
			if (read_module(own, id, &own_module, TRUE)) {
				free_module(&module);
				module = own_module;
			}
			g_array_append_val(modules, module);
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
	qsort(modules->data, modules->len, sizeof(struct pl_collar_module), by_name);
	*count = modules->len;
	return (struct pl_collar_module *)g_array_free(modules, FALSE);
}

bool pl_collar_module_set_enabled(const char *id, bool enabled) {
	if (!id || !*id || strchr(id, '/') || !g_str_has_suffix(id, ".desktop")) {
		return false;
	}
	char *own_dir = g_build_filename(g_get_user_data_dir(), "applications", NULL);
	char *own = g_build_filename(own_dir, id, NULL);
	bool ok;
	if (enabled) {
		ok = !g_file_test(own, G_FILE_TEST_EXISTS) || g_remove(own) == 0;
	} else {
		ok = false;
		char *path = system_entry_path(id);
		struct pl_collar_module module;
		if (path && read_module(path, id, &module, FALSE)) {
			free_module(&module);
			GKeyFile *file = g_key_file_new();
			ok = g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, NULL);
			if (ok) {
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
