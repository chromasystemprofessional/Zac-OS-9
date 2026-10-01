#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "menubar.h"

#define TEXT_UNLIMITED 100000

static struct mb_item *add(struct mb_menu *m, const char *label, char key,
		bool enabled, enum action action, const char *arg) {
	if (m->n >= MAX_ITEMS) {
		return NULL;
	}
	struct mb_item *it = &m->items[m->n++];
	*it = (struct mb_item){
		.label = label ? strdup(label) : NULL,
		.key = key,
		.enabled = enabled,
		.action = action,
		.arg = arg ? strdup(arg) : NULL,
	};
	return it;
}

static void sep(struct mb_menu *m) {
	add(m, NULL, 0, false, ACT_NONE, NULL);
}

static struct mb_menu *new_menu(struct mb_menu *menus, int *n, const char *title) {
	struct mb_menu *m = &menus[(*n)++];
	memset(m, 0, sizeof(*m));
	m->title = title ? strdup(title) : NULL;
	return m;
}

/* Edit commands are keystrokes sent to the front app once the menu has
 * closed. We can't know what the app can undo or paste, so with an app in
 * front they are all enabled. */
static void add_edit_menu(struct mb_menu *menus, int *n, bool enabled) {
	struct mb_menu *m = new_menu(menus, n, "Edit");
	add(m, "Undo", 'Z', enabled, ACT_EDIT_COMMAND, NULL);
	sep(m);
	add(m, "Cut", 'X', enabled, ACT_EDIT_COMMAND, NULL);
	add(m, "Copy", 'C', enabled, ACT_EDIT_COMMAND, NULL);
	add(m, "Paste", 'V', enabled, ACT_EDIT_COMMAND, NULL);
	add(m, "Clear", 0, enabled, ACT_EDIT_CLEAR, NULL);
	add(m, "Select All", 'A', enabled, ACT_EDIT_COMMAND, NULL);
}

/* With the Finder (or nothing) in front, the Finder's menus. Commands go
 * to platinum-finder; its reported state enables the items. */
static void add_finder_menus(struct mb_menu *menus, int *n) {
	finder_connect();
	const struct finder_state *fs = finder_state();
	const bool up = fs->connected, sel = up && fs->selection > 0;

	struct mb_menu *m = new_menu(menus, n, "File");
	add(m, "New Folder", 'N', up, ACT_FINDER, "new-folder");
	add(m, "Open", 'O', sel, ACT_FINDER, "open");
	add(m, "Move To Trash", KEY_DELETE_GLYPH, sel, ACT_FINDER, "move-to-trash");
	add(m, "Close Window", 'W', up && fs->window, ACT_FINDER, "close-window");
	sep(m);
	add(m, "Get Info", 'I', sel || (up && fs->window), ACT_FINDER, "get-info");
	add(m, "Duplicate", 'D', sel, ACT_FINDER, "duplicate");
	add(m, "Make Alias", 'M', sel, ACT_FINDER, "make-alias");
	add(m, "Put Away", 'Y', sel, ACT_FINDER, "put-away");
	sep(m);
	/* TODO: Find. */
	add(m, "Show Original", 'R', sel, ACT_FINDER, "show-original");
	add_edit_menu(menus, n, false);
	m = new_menu(menus, n, "View");
	const bool win = up && fs->window;
	struct mb_item *icons = add(m, "as Icons", 0, win, ACT_FINDER, "view-icons");
	icons->checked = win && !fs->view;
	/* TODO: button view. */
	add(m, "as Buttons", 0, false, ACT_NONE, NULL);
	struct mb_item *list = add(m, "as List", 0, win, ACT_FINDER, "view-list");
	list->checked = win && fs->view;
	m = new_menu(menus, n, "Special");
	add(m, "Empty Trash…", 0, up && fs->trash, ACT_FINDER, "empty-trash");
	sep(m);
	add(m, "Sleep", 0, false, ACT_NONE, NULL);
	add(m, "Restart", 0, false, ACT_NONE, NULL);
	add(m, "Shut Down", 0, false, ACT_NONE, NULL);
}

static void add_app_menus(struct mb_menu *menus, int *n, const char *app_id) {
	struct mb_menu *m = new_menu(menus, n, "File");
	add(m, "Close Window", 'W', true, ACT_CLOSE_WINDOW, app_id);
	sep(m);
	add(m, "Quit", 'Q', true, ACT_QUIT_APP, app_id);
	add_edit_menu(menus, n, keys_available());
}

static bool clock_shows_date;

void menus_toggle_clock(void) {
	clock_shows_date = !clock_shows_date;
}

static void clock_text(char *buf, size_t size) {
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	if (clock_shows_date) {
		strftime(buf, size, "%a, %b %-d, %Y", &tm);
		return;
	}
	/* Mac OS 8 default: 12-hour clock, "9:41 AM". */
	int hour = tm.tm_hour % 12 ? tm.tm_hour % 12 : 12;
	snprintf(buf, size, "%d:%02d %s", hour, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
}

static void render_menu_text(struct mb_menu *m) {
	if (m->title) {
		m->title_text = text_render(m->title, TEXT_UNLIMITED);
	}
	for (int i = 0; i < m->n; i++) {
		struct mb_item *it = &m->items[i];
		if (it->label) {
			it->label_text = text_render(it->label, TEXT_UNLIMITED);
		}
		if (it->key == KEY_DELETE_GLYPH) {
			it->key_text = text_render("⌫", TEXT_UNLIMITED);
		} else if (it->key) {
			char k[2] = { it->key, '\0' };
			it->key_text = text_render(k, TEXT_UNLIMITED);
		}
	}
}

void menus_rebuild(struct mb_bar *bar, int screen_w) {
	menus_free(bar);

	struct app apps[MAX_ITEMS];
	int n_apps = apps_collect(apps, MAX_ITEMS - 4);
	const struct app *front = n_apps > 0 && apps[0].active ? &apps[0] : NULL;
	const bool finder_front = !front || strcmp(front->app_id, FINDER_APP_ID) == 0;

	/* Left: the Platinum logo menu, then the front app's menus. */
	struct mb_menu *logo = new_menu(bar->left, &bar->n_left, NULL);
	logo->icon = logo_pixels();
	launch_fill_logo_menu(logo);
	if (finder_front) {
		add_finder_menus(bar->left, &bar->n_left);
	} else {
		add_app_menus(bar->left, &bar->n_left, front->app_id);
	}

	/* Right: clock, then the Application menu. */
	char clock[32];
	clock_text(clock, sizeof(clock));
	new_menu(bar->right, &bar->n_right, clock);

	struct mb_menu *am = new_menu(bar->right, &bar->n_right, front ? front->name : "Finder");
	if (front) {
		char label[256];
		snprintf(label, sizeof(label), "Hide %s", front->name);
		add(am, label, 0, true, ACT_HIDE_APP, front->app_id);
	} else {
		add(am, "Hide Finder", 0, false, ACT_NONE, NULL);
	}
	add(am, "Hide Others", 0, n_apps > 1 || (n_apps == 1 && !front), ACT_HIDE_OTHERS,
		front ? front->app_id : "");
	add(am, "Show All", 0, n_apps > 0, ACT_SHOW_ALL, NULL);
	sep(am);
	if (n_apps == 0) {
		struct mb_item *it = add(am, "Finder", 0, true, ACT_NONE, NULL);
		it->checked = true;
	}
	for (int i = 0; i < n_apps; i++) {
		struct mb_item *it = add(am, apps[i].name, 0, true, ACT_ACTIVATE_APP, apps[i].app_id);
		if (it) {
			it->checked = apps[i].active;
		}
	}

	for (int i = 0; i < bar->n_left; i++) {
		render_menu_text(&bar->left[i]);
		bar->left_titles[i] = (struct mbar_title){
			.text = bar->left[i].title_text,
			.icon = bar->left[i].icon,
		};
	}
	for (int i = 0; i < bar->n_right; i++) {
		render_menu_text(&bar->right[i]);
		bar->right_titles[i] = (struct mbar_title){ .text = bar->right[i].title_text };
	}
	mbar_layout_left(bar->left_titles, bar->n_left);
	mbar_layout_right(bar->right_titles, bar->n_right, screen_w);
}

static void free_menu(struct mb_menu *m) {
	free(m->title);
	text_destroy(m->title_text);
	for (int i = 0; i < m->n; i++) {
		free(m->items[i].label);
		free(m->items[i].arg);
		text_destroy(m->items[i].label_text);
		text_destroy(m->items[i].key_text);
	}
	memset(m, 0, sizeof(*m));
}

void menus_free(struct mb_bar *bar) {
	for (int i = 0; i < bar->n_left; i++) {
		free_menu(&bar->left[i]);
	}
	for (int i = 0; i < bar->n_right; i++) {
		free_menu(&bar->right[i]);
	}
	bar->n_left = bar->n_right = 0;
}

void menus_perform(struct mb_item *item) {
	struct app apps[MAX_ITEMS];
	int n_apps;
	switch (item->action) {
	case ACT_LAUNCH:
		launch(item->arg);
		break;
	case ACT_CLOSE_WINDOW:
		app_close_front_window(item->arg);
		break;
	case ACT_QUIT_APP:
		app_quit(item->arg);
		break;
	case ACT_HIDE_APP:
		app_set_hidden(item->arg, true);
		break;
	case ACT_HIDE_OTHERS:
		n_apps = apps_collect(apps, MAX_ITEMS);
		for (int i = 0; i < n_apps; i++) {
			if (strcmp(apps[i].app_id, item->arg) != 0) {
				app_set_hidden(apps[i].app_id, true);
			}
		}
		break;
	case ACT_SHOW_ALL:
		n_apps = apps_collect(apps, MAX_ITEMS);
		for (int i = 0; i < n_apps; i++) {
			app_set_hidden(apps[i].app_id, false);
		}
		break;
	case ACT_ACTIVATE_APP:
		app_activate(item->arg, menubar_seat());
		break;
	case ACT_EDIT_COMMAND:
		keys_send_command(item->key);
		break;
	case ACT_EDIT_CLEAR:
		keys_send_clear();
		break;
	case ACT_FINDER:
		finder_send(item->arg);
		break;
	case ACT_ABOUT:
		finder_send("about");
		break;
	case ACT_NONE:
		break;
	}
}
