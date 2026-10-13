/* The Zac menu's recent items (recent.c): what is remembered, in what
 * order, and how many the menu shows. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "menubar.h"

/* ---- stand-ins for the rest of the menu bar ---- */
struct mb_item *menus_add_item(struct mb_menu *m, const char *label, char key, bool enabled,
		enum action action, const char *arg) {
	struct mb_item *it = &m->items[m->n++];
	*it = (struct mb_item){ .label = g_strdup(label), .key = key, .enabled = enabled,
		.action = action, .arg = arg ? g_strdup(arg) : NULL };
	return it;
}
char *app_desktop_file(const char *app_id) {
	/* Only these two have desktop files. */
	return strcmp(app_id, "alpha") == 0 || strcmp(app_id, "bravo") == 0
		? g_strdup_printf("/apps/%s.desktop", app_id) : NULL;
}
bool launch_read_desktop(const char *path, char **name, char **exec) {
	*name = strdup(strstr(path, "alpha") ? "Alpha" : "Bravo");
	*exec = strdup(path);
	return true;
}
void sibling_program(const char *program, char *out, size_t size) {
	snprintf(out, size, "exec %s", program);
}

static const struct mb_menu *sub(const struct mb_menu *m, const char *title) {
	for (int i = 0; i < m->n; i++) {
		if (m->items[i].label && strcmp(m->items[i].label, title) == 0) {
			return m->items[i].submenu;
		}
	}
	return NULL;
}

static void labels(const struct mb_menu *m, char *out, size_t size) {
	out[0] = '\0';
	for (int i = 0; m && i < m->n; i++) {
		g_strlcat(out, m->items[i].label, size);
		g_strlcat(out, "|", size);
	}
}

static void write_file(const char *path, const char *text) {
	char *dir = g_path_get_dirname(path);
	g_mkdir_with_parents(dir, 0700);
	g_free(dir);
	assert(g_file_set_contents(path, text, -1, NULL));
}

int main(void) {
	char *root = g_dir_make_tmp("zacos9-recent-XXXXXX", NULL);
	char *state = g_build_filename(root, "state", NULL), *data = g_build_filename(root, "data", NULL),
		*config = g_build_filename(root, "config", NULL);
	g_setenv("XDG_STATE_HOME", state, TRUE);
	g_setenv("XDG_DATA_HOME", data, TRUE);
	g_setenv("XDG_CONFIG_HOME", config, TRUE);
	g_setenv("HOME", root, TRUE);

	/* Applications: newest first, once each; none without a desktop file,
	 * and never the Sniffer. */
	recent_app_seen("alpha");
	recent_app_seen("nofile");
	recent_app_seen(FINDER_APP_ID);
	recent_app_seen("bravo");
	recent_app_seen("alpha");

	/* Documents from recently-used.xbel, newest first; a missing file not. */
	char *older = g_build_filename(root, "older.txt", NULL), *newer = g_build_filename(root, "newer.txt", NULL);
	write_file(older, "x");
	write_file(newer, "x");
	g_mkdir_with_parents(data, 0700);
	char *xbel = g_build_filename(data, "recently-used.xbel", NULL);
	GBookmarkFile *b = g_bookmark_file_new();
	char *u1 = g_filename_to_uri(older, NULL, NULL), *u2 = g_filename_to_uri(newer, NULL, NULL);
	/* As programs write them: each with the application that used it. */
	const char *uris[] = { u1, u2, "file:///nonexistent/gone.txt" };
	for (size_t i = 0; i < G_N_ELEMENTS(uris); i++) {
		g_bookmark_file_add_application(b, uris[i], "test", "true %u");
	}
	g_bookmark_file_set_modified(b, u1, 1000);
	g_bookmark_file_set_modified(b, u2, 2000);
	g_bookmark_file_set_modified(b, "file:///nonexistent/gone.txt", 3000);
	assert(g_bookmark_file_to_file(b, xbel, NULL));
	g_bookmark_file_free(b);

	/* Servers, as the Neighbourhood writes them. */
	char *servers = g_build_filename(state, "zacos9", "recent-servers", NULL);
	write_file(servers, "afp\t10.0.0.5:548\tStudio Mac\nsmb\tnas.local\tNAS\nbogus line\n");

	struct mb_menu menu = { 0 };
	recent_fill_menu(&menu);
	char got[256];
	labels(sub(&menu, "Recent Applications"), got, sizeof(got));
	assert(strcmp(got, "Alpha|Bravo|") == 0);
	labels(sub(&menu, "Recent Documents"), got, sizeof(got));
	assert(strcmp(got, "newer.txt|older.txt|") == 0);
	assert(strncmp(sub(&menu, "Recent Documents")->items[0].arg, "open-path /", 11) == 0);
	labels(sub(&menu, "Recent Servers"), got, sizeof(got));
	assert(strcmp(got, "Studio Mac|NAS|") == 0);
	assert(strcmp(sub(&menu, "Recent Servers")->items[0].arg,
		"exec zacos9-netbrowser --connect afp '10.0.0.5:548'") == 0);

	/* Zac Menu Options: 1 document, no servers. */
	char *conf = g_build_filename(config, "zacos9", "desktop.conf", NULL);
	write_file(conf, "[General]\nrecent-documents=1\nrecent-servers=0\n");
	struct mb_menu fewer = { 0 };
	recent_fill_menu(&fewer);
	labels(sub(&fewer, "Recent Documents"), got, sizeof(got));
	assert(strcmp(got, "newer.txt|") == 0);
	assert(!sub(&fewer, "Recent Servers") && sub(&fewer, "Recent Applications"));

	char *quoted = g_shell_quote(root);
	char *cleanup = g_strconcat("rm -rf ", quoted, NULL);
	g_spawn_command_line_sync(cleanup, NULL, NULL, NULL, NULL);
	puts("recent items: ok");
	return 0;
}
