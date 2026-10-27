#include "controlpanels.h"

#include <gio/gdesktopappinfo.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static bool is_panel(GDesktopAppInfo *info) {
	const char *categories = g_desktop_app_info_get_categories(info);
	if (!categories || g_desktop_app_info_get_nodisplay(info)) {
		return false;
	}
	char **each = g_strsplit(categories, ";", -1);
	const bool found = g_strv_contains((const char *const *)each, "X-ZacOS9-ControlPanel");
	g_strfreev(each);
	return found;
}

static int by_name(const void *a, const void *b) {
	return g_ascii_strcasecmp(((const struct pl_control_panel *)a)->name,
		((const struct pl_control_panel *)b)->name);
}

struct pl_control_panel *pl_control_panels(size_t *count) {
	GArray *panels = g_array_new(FALSE, TRUE, sizeof(struct pl_control_panel));
	/* XDG data directory precedence; Hidden entries already dropped. */
	GList *all = g_app_info_get_all();
	for (GList *l = all; l; l = l->next) {
		if (!G_IS_DESKTOP_APP_INFO(l->data) || !is_panel(G_DESKTOP_APP_INFO(l->data))) {
			continue;
		}
		GAppInfo *info = l->data;
		const char *line = g_app_info_get_commandline(info);
		const char *id = g_app_info_get_id(info);
		if (!line || !*line || !id) {
			continue;
		}
		const size_t program = strcspn(line, " ");
		struct pl_control_panel panel = {
			.id = g_strdup(id),
			.name = g_strdup(g_app_info_get_name(info)),
			.program = g_strndup(line, program),
			.args = g_strdup(line + program),
			.icon = pl_icon_for_program(id),
		};
		g_array_append_val(panels, panel);
	}
	g_list_free_full(all, g_object_unref);
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
