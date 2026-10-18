/*
 * The Zac menu's Recent Applications, Recent Documents and Recent Servers
 * (Mac OS 9's Apple Menu Options; Missing Manual ch. 5 and 12).
 *
 *   Applications: programs whose windows appeared, newest first, kept in
 *     $XDG_STATE_HOME/zacos9/recent-applications (one app id a line); only
 *     those with a desktop file are listed, and the Sniffer never is.
 *   Documents: the freedesktop recently-used list (recently-used.xbel),
 *     which GTK, Qt and KDE programs write and the Sniffer adds to; the
 *     Sniffer opens the one chosen.
 *   Servers: what the Neighbourhood connected to,
 *     $XDG_STATE_HOME/zacos9/recent-servers ("afp|smb\taddress\tname");
 *     choosing one connects again through it.
 *
 * How many of each: desktop.conf recent-applications, recent-documents,
 * recent-servers (Zac Menu Options; 10 by default, 0 leaves it out).
 */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <glib.h>

#include "icons.h"
#include "menubar.h"
#include "settings.h"

#define RECENT_KEEP 50 /* lines kept on file, whatever the menu shows */

static void state_path(const char *name, char *out, size_t size) {
	const char *state = getenv("XDG_STATE_HOME");
	if (state && *state) {
		snprintf(out, size, "%s/zacos9/%s", state, name);
	} else {
		snprintf(out, size, "%s/.local/state/zacos9/%s", getenv("HOME") ? getenv("HOME") : "", name);
	}
}

static int setting_count(const char *key) {
	char v[16];
	if (!pl_setting(key, v, sizeof(v))) {
		return 10;
	}
	const int n = atoi(v);
	return n < 0 ? 0 : n > MAX_ITEMS ? MAX_ITEMS : n;
}

static bool show_submenus(void) {
	char value[16];
	return !pl_setting("recent-submenus", value, sizeof(value)) ||
		(strcmp(value, "0") != 0 && strcmp(value, "false") != 0);
}

/* The file's lines (at most `max`), newest first; free with g_strfreev. */
static char **read_lines(const char *path) {
	char *text = NULL;
	if (!g_file_get_contents(path, &text, NULL, NULL)) {
		return g_new0(char *, 1);
	}
	char **lines = g_strsplit(text, "\n", -1);
	g_free(text);
	return lines;
}

void recent_app_seen(const char *app_id) {
	if (!app_id || !*app_id || strcmp(app_id, FINDER_APP_ID) == 0) {
		return;
	}
	char *desktop = app_desktop_file(app_id);
	if (!desktop) {
		return; /* nothing to launch it again with */
	}
	free(desktop);
	char path[PATH_MAX];
	state_path("recent-applications", path, sizeof(path));
	char **old = read_lines(path);
	if (old[0] && strcmp(old[0], app_id) == 0) {
		g_strfreev(old);
		return; /* already the newest */
	}
	GString *out = g_string_new(app_id);
	g_string_append_c(out, '\n');
	int kept = 1;
	for (int i = 0; old[i] && kept < RECENT_KEEP; i++) {
		if (*old[i] && strcmp(old[i], app_id) != 0) {
			g_string_append_printf(out, "%s\n", old[i]);
			kept++;
		}
	}
	g_strfreev(old);
	char *dir = g_path_get_dirname(path);
	g_mkdir_with_parents(dir, 0700);
	g_free(dir);
	g_file_set_contents(path, out->str, (gssize)out->len, NULL);
	g_string_free(out, TRUE);
}

/* A submenu item for a list; dimmed when the list is empty. */
static struct mb_menu *submenu(struct mb_menu *menu, const char *title) {
	struct mb_item *it = menus_add_item(menu, title, 0, true, ACT_NONE, NULL);
	if (!it) {
		return NULL;
	}
	it->submenu = calloc(1, sizeof(*it->submenu));
	it->icon_kind = PL_ICON_FOLDER + 1; /* the Zac menu's folders */
	return it->submenu;
}

static void dim_if_empty(struct mb_menu *menu, const struct mb_menu *sub) {
	if (sub && sub->n == 0) {
		menu->items[menu->n - 1].enabled = false;
	}
}

static struct mb_menu *recent_list(struct mb_menu *menu, const char *title, bool nested) {
	return nested ? submenu(menu, title) : menu;
}

/* `desktop`: a program's desktop file, for its own icon; else `kind`. */
static void add_recent_item(struct mb_menu *menu, const char *title, const char *label,
		bool nested, enum action action, const char *arg, const char *desktop, enum pl_icon_kind kind) {
	char *shown = nested ? g_strdup(label) : g_strdup_printf("%s: %s", title, label);
	struct mb_item *it = menus_add_item(menu, shown, 0, true, action, arg);
	g_free(shown);
	if (it) {
		it->icon_px = desktop && menu_icon_for_desktop ? menu_icon_for_desktop(desktop) : NULL;
		it->icon_kind = it->icon_px ? 0 : (int)kind + 1;
	}
}

static void fill_applications(struct mb_menu *menu, int count, bool nested) {
	const char *title = "Recent Applications";
	struct mb_menu *sub = recent_list(menu, title, nested);
	if (!sub) {
		return;
	}
	char path[PATH_MAX];
	state_path("recent-applications", path, sizeof(path));
	char **ids = read_lines(path);
	int added = 0;
	for (int i = 0; ids[i] && added < count && (nested || menu->n < MAX_ITEMS); i++) {
		char *desktop = app_desktop_file(ids[i]);
		char *name, *exec;
		if (desktop && launch_read_desktop(desktop, &name, &exec)) {
			add_recent_item(sub, title, name, nested, ACT_LAUNCH, exec, desktop, PL_ICON_APPLICATION);
			added++;
			free(name);
			free(exec);
		}
		free(desktop);
	}
	g_strfreev(ids);
	if (nested) {
		dim_if_empty(menu, sub);
	}
}

struct document {
	char *path;
	gint64 when;
};

static int newest_first(const void *a, const void *b) {
	const gint64 x = ((const struct document *)a)->when, y = ((const struct document *)b)->when;
	return x < y ? 1 : x > y ? -1 : 0;
}

static void fill_documents(struct mb_menu *menu, int count, bool nested) {
	const char *title = "Recent Documents";
	struct mb_menu *sub = recent_list(menu, title, nested);
	if (!sub) {
		return;
	}
	char *xbel = g_build_filename(g_get_user_data_dir(), "recently-used.xbel", NULL);
	GBookmarkFile *bookmarks = g_bookmark_file_new();
	if (g_bookmark_file_load_from_file(bookmarks, xbel, NULL)) {
		gsize n = 0;
		char **uris = g_bookmark_file_get_uris(bookmarks, &n);
		struct document *docs = g_new0(struct document, n ? n : 1);
		gsize found = 0;
		for (gsize i = 0; i < n; i++) {
			char *file = g_filename_from_uri(uris[i], NULL, NULL);
			if (!file || !g_file_test(file, G_FILE_TEST_IS_REGULAR)) {
				g_free(file); /* gone, a folder, or not on this computer */
				continue;
			}
			GDateTime *modified = g_bookmark_file_get_modified_date_time(bookmarks, uris[i], NULL);
			docs[found].path = file;
			docs[found].when = modified ? g_date_time_to_unix(modified) : 0;
			found++;
		}
		qsort(docs, found, sizeof(docs[0]), newest_first);
		int added = 0;
		for (gsize i = 0; i < found; i++) {
			if (added < count && (nested || menu->n < MAX_ITEMS)) {
				char *name = g_path_get_basename(docs[i].path);
				char *arg = g_strconcat("open-path ", docs[i].path, NULL);
				add_recent_item(sub, title, name, nested, ACT_FINDER, arg, NULL, PL_ICON_DOCUMENT);
				added++;
				g_free(name);
				g_free(arg);
			}
			g_free(docs[i].path);
		}
		g_free(docs);
		g_strfreev(uris);
	}
	g_bookmark_file_free(bookmarks);
	g_free(xbel);
	if (nested) {
		dim_if_empty(menu, sub);
	}
}

static void fill_servers(struct mb_menu *menu, int count, bool nested) {
	const char *title = "Recent Servers";
	struct mb_menu *sub = recent_list(menu, title, nested);
	if (!sub) {
		return;
	}
	char path[PATH_MAX], netbrowser[PATH_MAX + 64];
	state_path("recent-servers", path, sizeof(path));
	sibling_program("zacos9-netbrowser", netbrowser, sizeof(netbrowser));
	char **lines = read_lines(path);
	int added = 0;
	for (int i = 0; lines[i] && added < count && (nested || menu->n < MAX_ITEMS); i++) {
		char **f = g_strsplit(lines[i], "\t", 3);
		if (f[0] && f[1] && f[2] && (strcmp(f[0], "afp") == 0 || strcmp(f[0], "smb") == 0)) {
			char *address = g_shell_quote(f[1]);
			char *command = g_strdup_printf("%s --connect %s %s", netbrowser, f[0], address);
			add_recent_item(sub, title, f[2], nested, ACT_LAUNCH, command, NULL, PL_ICON_NETWORK_DISK);
			added++;
			g_free(command);
			g_free(address);
		}
		g_strfreev(f);
	}
	g_strfreev(lines);
	if (nested) {
		dim_if_empty(menu, sub);
	}
}

void recent_fill_menu(struct mb_menu *menu) {
	const int apps = setting_count("recent-applications");
	const int docs = setting_count("recent-documents");
	const int servers = setting_count("recent-servers");
	const bool nested = show_submenus();
	if (apps > 0) {
		fill_applications(menu, apps, nested);
	}
	if (docs > 0) {
		fill_documents(menu, docs, nested);
	}
	if (servers > 0) {
		fill_servers(menu, servers, nested);
	}
}
