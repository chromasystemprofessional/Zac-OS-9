#define _DEFAULT_SOURCE
#include "appfilter.h"

#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

#define MCT_IFACE "com.endlessm.ParentalControls.AppFilter"

static struct {
	bool loaded, active, allowlist;
	GPtrArray *entries; /* char * */
} filter;

static void add_entries(char **list) {
	for (char **e = list; *e; e++) {
		if (**e) {
			g_ptr_array_add(filter.entries, g_strdup(*e));
		}
	}
}

/* This person's filter from accountsservice; none if malcontent isn't
 * there or has no filter for them. */
static void load_malcontent(void) {
	GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
	if (!bus) {
		return;
	}
	GVariant *user = g_dbus_connection_call_sync(bus, "org.freedesktop.Accounts",
		"/org/freedesktop/Accounts", "org.freedesktop.Accounts", "FindUserById",
		g_variant_new("(x)", (gint64)getuid()), G_VARIANT_TYPE("(o)"),
		G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL);
	if (user) {
		const char *path;
		g_variant_get(user, "(&o)", &path);
		GVariant *reply = g_dbus_connection_call_sync(bus, "org.freedesktop.Accounts", path,
			"org.freedesktop.DBus.Properties", "Get",
			g_variant_new("(ss)", MCT_IFACE, "AppFilter"), G_VARIANT_TYPE("(v)"),
			G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL);
		if (reply) {
			GVariant *value;
			g_variant_get(reply, "(v)", &value);
			if (g_variant_is_of_type(value, G_VARIANT_TYPE("(bas)"))) {
				gboolean allow;
				char **list;
				g_variant_get(value, "(b^as)", &allow, &list);
				filter.allowlist = allow;
				add_entries(list);
				/* An empty blocklist is malcontent's "no filter". */
				filter.active = allow || filter.entries->len > 0;
				g_strfreev(list);
			}
			g_variant_unref(value);
			g_variant_unref(reply);
		}
		g_variant_unref(user);
	}
	g_object_unref(bus);
}

static void load(void) {
	if (filter.loaded) {
		return;
	}
	filter.loaded = true;
	filter.entries = g_ptr_array_new_with_free_func(g_free);
	const char *fake = g_getenv("ZACOS9_APP_FILTER");
	if (fake && *fake) {
		char **parts = g_strsplit(fake, ":", -1);
		filter.active = true;
		filter.allowlist = strcmp(parts[0], "block") != 0;
		add_entries(parts + 1);
		g_strfreev(parts);
		return;
	}
	load_malcontent();
}

bool pl_app_filter_active(void) {
	load();
	return filter.active;
}

/* The program a desktop entry runs: its Exec's first word, past `env`
 * and its VAR=value settings, found on PATH. */
static char *program_of(GDesktopAppInfo *app) {
	const char *exec = g_app_info_get_commandline(G_APP_INFO(app));
	char **argv = NULL;
	if (!exec || !g_shell_parse_argv(exec, NULL, &argv, NULL)) {
		return NULL;
	}
	int i = 0;
	char *first = argv[0] ? g_path_get_basename(argv[0]) : NULL;
	if (first && strcmp(first, "env") == 0) {
		for (i = 1; argv[i] && (strchr(argv[i], '=') || argv[i][0] == '-'); i++) {
		}
	}
	g_free(first);
	char *path = argv[i] ? g_find_program_in_path(argv[i]) : NULL;
	g_strfreev(argv);
	if (path) {
		char *real = realpath(path, NULL);
		if (real) {
			g_free(path);
			path = g_strdup(real);
			free(real);
		}
	}
	return path;
}

char *pl_app_filter_entry(GDesktopAppInfo *app) {
	char *flatpak = g_desktop_app_info_get_string(app, "X-Flatpak");
	if (flatpak && *flatpak) {
		struct utsname u;
		uname(&u);
		const char *arch = strcmp(u.machine, "aarch64") == 0 ? "aarch64" : "x86_64";
		char *ref = g_strdup_printf("app/%s/%s/stable", flatpak, arch);
		g_free(flatpak);
		return ref;
	}
	g_free(flatpak);
	return program_of(app);
}

/* Whether the list has this entry: Flatpak refs by their app ID alone. */
static bool listed(const char *entry) {
	bool ref = g_str_has_prefix(entry, "app/");
	const char *id = entry + 4;
	size_t id_len = ref ? strcspn(id, "/") : 0;
	for (guint i = 0; i < filter.entries->len; i++) {
		const char *e = g_ptr_array_index(filter.entries, i);
		if (!ref ? strcmp(e, entry) == 0
				: g_str_has_prefix(e, "app/") && strncmp(e + 4, id, id_len) == 0 &&
				(e[4 + id_len] == '/' || e[4 + id_len] == '\0')) {
			return true;
		}
	}
	return false;
}

bool pl_app_allowed(GDesktopAppInfo *app) {
	if (!pl_app_filter_active() || !app) {
		return true;
	}
	char *entry = pl_app_filter_entry(app);
	if (!entry) {
		return !filter.allowlist; /* runs nothing we can name */
	}
	char *base = g_path_get_basename(entry);
	bool ours = g_str_has_prefix(base, "zacos9-") || g_str_has_prefix(base, "zacos9_");
	g_free(base);
	bool allowed = ours || listed(entry) == filter.allowlist;
	g_free(entry);
	return allowed;
}

bool pl_app_allowed_file(const char *desktop_file) {
	if (!pl_app_filter_active()) {
		return true;
	}
	GDesktopAppInfo *app = g_desktop_app_info_new_from_filename(desktop_file);
	if (!app) {
		return true;
	}
	bool allowed = pl_app_allowed(app);
	g_object_unref(app);
	return allowed;
}
