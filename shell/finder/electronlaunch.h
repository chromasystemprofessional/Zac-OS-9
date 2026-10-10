#pragma once

#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <cstdlib>

#include "electron.h"

/* What to run for a desktop entry: Electron apps start under X11, where
 * they give the menu bar their menus (lib/electron.h), so for those a copy
 * of the entry with the arguments in each Exec; otherwise the entry itself.
 * A new reference. Shared by the Finder and Login Items. */
inline GAppInfo *electronLaunchInfo(GAppInfo *info) {
	const char *file = G_IS_DESKTOP_APP_INFO(info)
		? g_desktop_app_info_get_filename(G_DESKTOP_APP_INFO(info)) : nullptr;
	GAppInfo *electron = nullptr;
	GKeyFile *keys = g_key_file_new();
	if (file && g_key_file_load_from_file(keys, file, G_KEY_FILE_KEEP_TRANSLATIONS, nullptr)) {
		bool changed = false;
		gchar **groups = g_key_file_get_groups(keys, nullptr);
		for (gchar **group = groups; *group; group++) {
			gchar *exec = g_key_file_get_string(keys, *group, "Exec", nullptr);
			char *command = exec ? pl_electron_command(exec) : nullptr;
			if (command) {
				g_key_file_set_string(keys, *group, "Exec", command);
				changed = true;
			}
			free(command);
			g_free(exec);
		}
		g_strfreev(groups);
		if (changed) {
			electron = G_APP_INFO(g_desktop_app_info_new_from_keyfile(keys));
		}
	}
	g_key_file_free(keys);
	return electron ? electron : G_APP_INFO(g_object_ref(info));
}
