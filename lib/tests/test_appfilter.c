#define _DEFAULT_SOURCE
/* Limited accounts' program filter (appfilter.c), with a stand-in for
 * malcontent's list. */
#include <assert.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "appfilter.h"

static char dir[] = "/tmp/zacos9-appfilter-XXXXXX";

static GDesktopAppInfo *entry(const char *name, const char *body) {
	char *path = g_build_filename(dir, name, NULL);
	char *text = g_strdup_printf("[Desktop Entry]\nType=Application\nName=%s\n%s\n", name, body);
	g_file_set_contents(path, text, -1, NULL);
	GDesktopAppInfo *app = g_desktop_app_info_new_from_filename(path);
	assert(app);
	g_free(text);
	g_free(path);
	return app;
}

int main(void) {
	assert(g_mkdtemp(dir));
	char *sh = realpath("/bin/sh", NULL), *true_ = realpath("/bin/true", NULL);
	char *value = g_strdup_printf("allow:%s:app/org.example.Paint/x86_64/stable", sh);
	g_setenv("ZACOS9_APP_FILTER", value, TRUE);

	GDesktopAppInfo *shell = entry("shell.desktop", "Exec=sh -c true");
	GDesktopAppInfo *wrapped = entry("wrapped.desktop", "Exec=/usr/bin/env FOO=1 sh");
	GDesktopAppInfo *other = entry("other.desktop", "Exec=true");
	GDesktopAppInfo *paint = entry("paint.desktop",
		"Exec=/usr/bin/flatpak run org.example.Paint\nX-Flatpak=org.example.Paint");
	GDesktopAppInfo *game = entry("game.desktop",
		"Exec=/usr/bin/flatpak run org.example.Game\nX-Flatpak=org.example.Game");

	assert(pl_app_filter_active());
	assert(pl_app_allowed(shell));
	assert(pl_app_allowed(wrapped)); /* past env and its settings */
	assert(!pl_app_allowed(other));
	assert(pl_app_allowed(paint));   /* by Flatpak ID, whatever the arch */
	assert(!pl_app_allowed(game));
	char *e = pl_app_filter_entry(other);
	assert(e && strcmp(e, true_) == 0);
	g_free(e);
	e = pl_app_filter_entry(paint);
	assert(e && g_str_has_prefix(e, "app/org.example.Paint/"));
	g_free(e);
	puts("ok: a limited account's programs");
	return 0;
}
