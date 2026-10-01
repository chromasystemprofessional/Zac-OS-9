#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "menubar.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

static struct wl_list toplevels; /* struct toplevel.link */
static unsigned activation_counter;

struct wl_list *toplevels_list(void) {
	return &toplevels;
}

static void replace(char **dst, const char *src) {
	free(*dst);
	*dst = strdup(src ? src : "");
}

static void handle_title(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
		const char *title) {
	struct toplevel *t = data;
	replace(&t->p_title, title);
}

static void handle_app_id(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
		const char *app_id) {
	struct toplevel *t = data;
	replace(&t->p_app_id, app_id);
}

static void handle_output_enter(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
		struct wl_output *output) {
}

static void handle_output_leave(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
		struct wl_output *output) {
}

static void handle_state(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
		struct wl_array *states) {
	struct toplevel *t = data;
	t->p_activated = t->p_minimized = false;
	uint32_t *s;
	wl_array_for_each(s, states) {
		if (*s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) {
			t->p_activated = true;
		} else if (*s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED) {
			t->p_minimized = true;
		}
	}
}

static void handle_done(void *data, struct zwlr_foreign_toplevel_handle_v1 *h) {
	struct toplevel *t = data;
	if (t->p_title) {
		replace(&t->title, t->p_title);
	}
	if (t->p_app_id) {
		replace(&t->app_id, t->p_app_id);
	}
	if (t->p_activated && !t->activated) {
		t->last_active = ++activation_counter;
	}
	t->activated = t->p_activated;
	t->minimized = t->p_minimized;
	menubar_apps_changed();
}

static void handle_closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *h) {
	struct toplevel *t = data;
	wl_list_remove(&t->link);
	zwlr_foreign_toplevel_handle_v1_destroy(h);
	free(t->title);
	free(t->app_id);
	free(t->p_title);
	free(t->p_app_id);
	free(t);
	menubar_apps_changed();
}

static void handle_parent(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
		struct zwlr_foreign_toplevel_handle_v1 *parent) {
}

static const struct zwlr_foreign_toplevel_handle_v1_listener handle_listener = {
	.title = handle_title,
	.app_id = handle_app_id,
	.output_enter = handle_output_enter,
	.output_leave = handle_output_leave,
	.state = handle_state,
	.done = handle_done,
	.closed = handle_closed,
	.parent = handle_parent,
};

static void manager_toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1 *mgr,
		struct zwlr_foreign_toplevel_handle_v1 *handle) {
	struct toplevel *t = calloc(1, sizeof(*t));
	t->handle = handle;
	t->last_active = ++activation_counter;
	wl_list_insert(&toplevels, &t->link);
	zwlr_foreign_toplevel_handle_v1_add_listener(handle, &handle_listener, t);
}

static void manager_finished(void *data, struct zwlr_foreign_toplevel_manager_v1 *mgr) {
}

static const struct zwlr_foreign_toplevel_manager_v1_listener manager_listener = {
	.toplevel = manager_toplevel,
	.finished = manager_finished,
};

void toplevels_init(struct zwlr_foreign_toplevel_manager_v1 *mgr) {
	wl_list_init(&toplevels);
	zwlr_foreign_toplevel_manager_v1_add_listener(mgr, &manager_listener, NULL);
}

/* ---- app names from .desktop files -------------------------------------- */

static char *desktop_name(const char *path) {
	FILE *f = fopen(path, "r");
	if (!f) {
		return NULL;
	}
	char line[512], *name = NULL;
	bool in_entry = false;
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (line[0] == '[') {
			in_entry = strcmp(line, "[Desktop Entry]") == 0;
		} else if (in_entry && strncmp(line, "Name=", 5) == 0) {
			name = strdup(line + 5);
			break;
		}
	}
	fclose(f);
	return name;
}

static char *lookup_desktop_name(const char *app_id) {
	const char *home = getenv("HOME");
	const char *data_dirs = getenv("XDG_DATA_DIRS");
	char dirs[2048];
	snprintf(dirs, sizeof(dirs), "%s/.local/share:%s", home ? home : "",
		data_dirs && *data_dirs ? data_dirs : "/usr/local/share:/usr/share");

	char lower[256];
	size_t i;
	for (i = 0; app_id[i] && i < sizeof(lower) - 1; i++) {
		lower[i] = (char)tolower((unsigned char)app_id[i]);
	}
	lower[i] = '\0';
	const char *candidates[] = { app_id, lower };

	for (char *dir = strtok(dirs, ":"); dir; dir = strtok(NULL, ":")) {
		for (int c = 0; c < 2; c++) {
			char path[2600];
			snprintf(path, sizeof(path), "%s/applications/%s.desktop", dir, candidates[c]);
			char *name = desktop_name(path);
			if (name) {
				return name;
			}
		}
	}
	return NULL;
}

const char *app_display_name(const char *app_id) {
	static struct {
		char *app_id, *name;
	} cache[64];
	static int n_cache;
	for (int i = 0; i < n_cache; i++) {
		if (strcmp(cache[i].app_id, app_id) == 0) {
			return cache[i].name;
		}
	}
	char *name = strcmp(app_id, FINDER_APP_ID) == 0 ? strdup("Finder")
		: lookup_desktop_name(app_id);
	if (!name) {
		name = strdup(*app_id ? app_id : "Untitled");
		name[0] = (char)toupper((unsigned char)name[0]);
	}
	if (n_cache < 64) {
		cache[n_cache].app_id = strdup(app_id);
		cache[n_cache].name = name;
		n_cache++;
	}
	return name;
}

/* ---- apps --------------------------------------------------------------- */

static const char *app_of(const struct toplevel *t) {
	return t->app_id ? t->app_id : "";
}

int apps_collect(struct app *apps, int max) {
	/* Visit windows from most to least recently active. */
	int n_top = wl_list_length(&toplevels);
	struct toplevel **sorted = calloc(n_top > 0 ? n_top : 1, sizeof(*sorted));
	int k = 0;
	struct toplevel *t;
	wl_list_for_each(t, &toplevels, link) {
		sorted[k++] = t;
	}
	for (int i = 1; i < k; i++) {
		for (int j = i; j > 0 && sorted[j]->last_active > sorted[j - 1]->last_active; j--) {
			struct toplevel *tmp = sorted[j];
			sorted[j] = sorted[j - 1];
			sorted[j - 1] = tmp;
		}
	}

	int n = 0;
	for (int i = 0; i < k; i++) {
		const char *id = app_of(sorted[i]);
		int found = -1;
		for (int a = 0; a < n; a++) {
			if (strcmp(apps[a].app_id, id) == 0) {
				found = a;
			}
		}
		if (found < 0) {
			if (n == max) {
				continue;
			}
			found = n++;
			apps[found] = (struct app){
				.app_id = (char *)id,
				.name = (char *)app_display_name(id),
				.hidden = true,
			};
		}
		apps[found].active |= sorted[i]->activated;
		apps[found].hidden &= sorted[i]->minimized;
	}
	free(sorted);
	return n;
}

/* Mac OS brings all of an app's windows forward together: activate them
 * oldest first so the most recently used ends up in front. */
void app_activate(const char *app_id, struct wl_seat *seat) {
	struct toplevel *order[256];
	int n = 0;
	struct toplevel *t;
	wl_list_for_each(t, &toplevels, link) {
		if (strcmp(app_of(t), app_id) == 0 && n < 256) {
			order[n++] = t;
		}
	}
	for (int i = 1; i < n; i++) {
		for (int j = i; j > 0 && order[j]->last_active < order[j - 1]->last_active; j--) {
			struct toplevel *tmp = order[j];
			order[j] = order[j - 1];
			order[j - 1] = tmp;
		}
	}
	for (int i = 0; i < n; i++) {
		zwlr_foreign_toplevel_handle_v1_activate(order[i]->handle, seat);
	}
}

void app_set_hidden(const char *app_id, bool hidden) {
	struct toplevel *t;
	wl_list_for_each(t, &toplevels, link) {
		if (strcmp(app_of(t), app_id) == 0) {
			if (hidden) {
				zwlr_foreign_toplevel_handle_v1_set_minimized(t->handle);
			} else {
				zwlr_foreign_toplevel_handle_v1_unset_minimized(t->handle);
			}
		}
	}
}

void app_close_front_window(const char *app_id) {
	struct toplevel *t, *front = NULL;
	wl_list_for_each(t, &toplevels, link) {
		if (strcmp(app_of(t), app_id) == 0 && !t->minimized &&
				(!front || t->last_active > front->last_active)) {
			front = t;
		}
	}
	if (front) {
		zwlr_foreign_toplevel_handle_v1_close(front->handle);
	}
}

void app_quit(const char *app_id) {
	struct toplevel *t;
	wl_list_for_each(t, &toplevels, link) {
		if (strcmp(app_of(t), app_id) == 0) {
			zwlr_foreign_toplevel_handle_v1_close(t->handle);
		}
	}
}
