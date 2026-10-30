#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "icons.h"
#include "menubar.h"
#include "settings.h"

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

struct mb_item *menus_add_item(struct mb_menu *m, const char *label, char key,
		bool enabled, enum action action, const char *arg) {
	return add(m, label, key, enabled, action, arg);
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

/* Special > Empty Trash: "…" while it asks first (the Trash's Get Info,
 * Warn before emptying), as Mac OS 9 dropped the ellipsis without it. */
static const char *empty_trash_title(void) {
	char v[16];
	const bool warns = !(pl_setting("trash-warn", v, sizeof(v)) && strcmp(v, "false") == 0);
	return warns ? "Empty Trash…" : "Empty Trash";
}

/* File > Label and its contextual twin: the seven labels, the selection's
 * checked. */
static void add_label_submenu(struct mb_menu *m, bool enabled, int current) {
	struct mb_item *label = add(m, "Label", 0, enabled, ACT_NONE, NULL);
	label->submenu = calloc(1, sizeof(*label->submenu));
	for (int i = 0; i < PL_LABEL_COUNT; i++) {
		char command[16];
		snprintf(command, sizeof(command), "label %d", i);
		struct mb_item *it = add(label->submenu, pl_label_name(i), 0, enabled,
			ACT_FINDER, command);
		it->swatch = pl_label_color(i);
		it->checked = enabled && current == i;
		if (i == 0) {
			sep(label->submenu);
		}
	}
}

/* View > Arrange and its contextual twin. */
static void add_arrange_submenu(struct mb_menu *m, bool enabled) {
	struct mb_item *arrange = add(m, "Arrange", 0, enabled, ACT_NONE, NULL);
	arrange->submenu = calloc(1, sizeof(*arrange->submenu));
	add(arrange->submenu, "by Name", 0, enabled, ACT_FINDER, "arrange name");
	add(arrange->submenu, "by Date Modified", 0, enabled, ACT_FINDER, "arrange modified");
	add(arrange->submenu, "by Date Created", 0, enabled, ACT_FINDER, "arrange created");
	add(arrange->submenu, "by Size", 0, enabled, ACT_FINDER, "arrange size");
	add(arrange->submenu, "by Kind", 0, enabled, ACT_FINDER, "arrange kind");
	add(arrange->submenu, "by Label", 0, enabled, ACT_FINDER, "arrange label");
}

/* View > Sort List: the list's columns shown, left to right (the
 * Sniffer's viewoptions.h ids), the one sorted by checked. */
static void add_sort_submenu(struct mb_menu *m, bool enabled, const struct finder_state *fs) {
	static const struct { int id; const char *name, *key; } columns[] = {
		{ 0, "by Name", "name" }, { 1, "by Date Modified", "modified" },
		{ 4, "by Date Created", "created" }, { 2, "by Size", "size" }, { 3, "by Kind", "kind" },
		{ 5, "by Label", "label" }, { 6, "by Comments", "comments" }, { 7, "by Version", "version" },
	};
	struct mb_item *sort = add(m, "Sort List", 0, enabled, ACT_NONE, NULL);
	sort->submenu = calloc(1, sizeof(*sort->submenu));
	for (size_t i = 0; i < sizeof(columns) / sizeof(columns[0]); i++) {
		if (columns[i].id != 0 && !(fs->columns & (1u << columns[i].id))) {
			continue;
		}
		char command[32];
		snprintf(command, sizeof(command), "sort %s", columns[i].key);
		struct mb_item *it = add(sort->submenu, columns[i].name, 0, enabled, ACT_FINDER, command);
		it->checked = enabled && fs->sort == columns[i].id;
	}
}

/* With the Finder (or nothing) in front, the Finder's menus. Commands go
 * to zacos9-finder; its reported state enables the items. */
static void free_menu(struct mb_menu *m);

/* Puppy Mode: the Sniffer's commands it leaves out go (lib/settings.c),
 * with submenus left empty and separators left doubled or at the ends. */
static void puppy_trim(struct mb_menu *m) {
	int out = 0;
	for (int i = 0; i < m->n; i++) {
		struct mb_item *it = &m->items[i];
		bool keep = it->action != ACT_FINDER || pl_puppy_allows(it->arg);
		if (keep && it->submenu) {
			puppy_trim(it->submenu);
			keep = it->submenu->n > 0;
		}
		if (keep && !it->label && (out == 0 || !m->items[out - 1].label)) {
			keep = false;
		}
		if (!keep) {
			struct mb_menu gone = { .n = 1 };
			gone.items[0] = *it;
			free_menu(&gone);
			continue;
		}
		m->items[out++] = *it;
	}
	while (out > 0 && !m->items[out - 1].label) {
		out--;
		free(m->items[out].arg);
	}
	m->n = out;
}

static void add_finder_menus(struct mb_menu *menus, int *n) {
	finder_connect();
	const struct finder_state *fs = finder_state();
	const bool up = fs->connected, sel = up && fs->selection > 0;

	struct mb_menu *m = new_menu(menus, n, "File");
	struct mb_menu *const first = m;
	add(m, "New Folder", 'N', up, ACT_FINDER, "new-folder");
	add(m, "Open", 'O', sel, ACT_FINDER, "open");
	add(m, "Print", 'P', sel, ACT_FINDER, "print");
	add(m, "Encrypt…", 0, sel, ACT_FINDER, "encrypt");
	add(m, "Decrypt…", 0, sel, ACT_FINDER, "decrypt");
	add(m, "Move To Trash", KEY_DELETE_GLYPH, sel, ACT_FINDER, "move-to-trash");
	add(m, "Close Window", 'W', up && fs->window, ACT_FINDER, "close-window");
	sep(m);
	/* Mac OS 9: Get Info > General Information, Sharing… */
	const bool info = sel || (up && fs->window);
	struct mb_item *get_info = add(m, "Get Info", 0, info, ACT_NONE, NULL);
	get_info->submenu = calloc(1, sizeof(*get_info->submenu));
	add(get_info->submenu, "General Information", 'I', info, ACT_FINDER, "get-info");
	add(get_info->submenu, "Sharing…", 0, info, ACT_FINDER, "get-info-sharing");
	add_label_submenu(m, sel, fs->label);
	add(m, "Duplicate", 'D', sel, ACT_FINDER, "duplicate");
	add(m, "Make Alias", 'M', sel, ACT_FINDER, "make-alias");
	add(m, "Add To Favorites", 0, sel, ACT_FINDER, "add-to-favorites");
	add(m, "Put Away", 'Y', sel, ACT_FINDER, "put-away");
	sep(m);
	add(m, "Search Internet…", 'H', up, ACT_FINDER, "search-internet");
	add(m, "Find…", 'F', up, ACT_FINDER, "find");
	add(m, "Find Similar Items", 0, sel, ACT_FINDER, "find-similar");
	add(m, "Index Selection", 0, up, ACT_FINDER, "index-selection");
	add(m, "Show Original", 'R', sel, ACT_FINDER, "show-original");
	sep(m);
	add(m, "Page Setup…", 0, up, ACT_FINDER, "page-setup");
	add(m, up && fs->window ? "Print Window…" : "Print Desktop…", 0, up, ACT_FINDER,
		"print-window");
	/* Sniffer has no undoable edits or clipboard yet; it can select. */
	add_edit_menu(menus, n, false);
	struct mb_menu *edit = &menus[*n - 1];
	struct mb_item *select_all = &edit->items[edit->n - 1];
	select_all->enabled = up;
	select_all->action = ACT_FINDER;
	select_all->arg = strdup("select-all");
	sep(edit);
	add(edit, "Show Clipboard", 0, up, ACT_FINDER, "show-clipboard");
	sep(edit);
	add(edit, "Preferences…", 0, up, ACT_FINDER, "preferences");
	m = new_menu(menus, n, "View");
	const bool win = up && fs->window;
	struct mb_item *icons = add(m, "as Icons", 0, win, ACT_FINDER, "view-icons");
	icons->checked = win && fs->view == 0;
	struct mb_item *buttons = add(m, "as Buttons", 0, win, ACT_FINDER, "view-buttons");
	buttons->checked = win && fs->view == 2;
	struct mb_item *list = add(m, "as List", 0, win, ACT_FINDER, "view-list");
	list->checked = win && fs->view == 1;
	sep(m);
	/* Mac OS 8/9: a window, or a pop-up window's tab at the screen's bottom. */
	struct mb_item *as_window = add(m, "as Window", 0, win && fs->popups, ACT_FINDER, "view-window");
	as_window->checked = win && !fs->popup;
	struct mb_item *as_popup = add(m, "as Pop-up Window", 0, win && fs->popups, ACT_FINDER,
		"view-popup");
	as_popup->checked = win && fs->popup;
	sep(m);
	/* Mac OS 9: for an icon view, or the desktop when no window is in front. */
	const bool iconView = up && (!fs->window || fs->view == 0);
	add(m, "Clean Up", 0, iconView, ACT_FINDER, "clean-up");
	add_arrange_submenu(m, iconView);
	add_sort_submenu(m, win && fs->view == 1, fs);
	add(m, "Reset Column Positions", 0, win && fs->view == 1 && fs->custom, ACT_FINDER,
		"reset-columns");
	sep(m);
	add(m, "View Options…", 0, up, ACT_FINDER, "view-options");
	m = new_menu(menus, n, "Special");
	add(m, empty_trash_title(), 0, up && fs->trash, ACT_FINDER, "empty-trash");
	add(m, "Eject", 'E', up && (fs->network || fs->unmount), ACT_FINDER, "eject");
	add(m, "Erase Disk…", 0, up && fs->erase && fs->selection == 1, ACT_FINDER, "erase-disk");
	/* Restore legacy hidden overrides, not applications queued in Trash. */
	add(m, "Show All Applications", 0, up, ACT_FINDER, "show-hidden-applications");
	sep(m);
	add(m, "Sleep",     0, true, ACT_LAUNCH, "systemctl suspend");
	add(m, "Restart",   0, true, ACT_LAUNCH, "systemctl reboot");
	add(m, "Shut Down", 0, true, ACT_LAUNCH, "systemctl poweroff");
	sep(m);
	struct mb_item *log_out = add(m, "Log Out…", 'Q', up, ACT_FINDER, "log-out");
	if (log_out) {
		log_out->shift = true; /* ⇧⌘Q */
	}
	/* Mac OS 9's Help menu: Help Center, then the Finder's own book. */
	m = new_menu(menus, n, "Help");
	char help[PATH_MAX + 64];
	sibling_program("zacos9-help", help, sizeof(help));
	add(m, "Help Center", 0, true, ACT_LAUNCH, help);
	sep(m);
	char book[PATH_MAX + 80];
	snprintf(book, sizeof(book), "%s zacos9", help);
	add(m, "ZacOS Help", '?', true, ACT_LAUNCH, book);
	/* Where Mac OS 9 had its tutorials (docs/tutorial.md). */
	char tutorial[PATH_MAX + 80], practice[PATH_MAX + 64];
	snprintf(tutorial, sizeof(tutorial), "%s tutorial", help);
	add(m, "Learn ZacOS", 0, true, ACT_LAUNCH, tutorial);
	sibling_program("zacos9-mousepractice", practice, sizeof(practice));
	add(m, "Mouse Practice", 0, true, ACT_LAUNCH, practice);
	sep(m);
	char hints[16];
	struct mb_item *hint_bubbles = add(m, "Show Hint Bubbles", 0, true,
		ACT_TOGGLE_SETTING, "hint-bubbles");
	hint_bubbles->checked = !pl_setting("hint-bubbles", hints, sizeof(hints)) ||
		(strcmp(hints, "0") != 0 && strcmp(hints, "false") != 0);
	if (pl_puppy_mode()) {
		for (struct mb_menu *f = first; f < &menus[*n]; f++) {
			puppy_trim(f);
		}
	}
}

struct mb_menu *menus_new_menu(struct mb_menu *menus, int *n, const char *title) {
	return new_menu(menus, n, title);
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

/* The Application menu shows the front program's name, unless its handle
 * hid it: then the program's icon (appicon.c), or the generic one. */
static bool app_name_shown(void) {
	char v[16];
	return !(pl_setting("app-menu-name", v, sizeof(v)) && strcmp(v, "false") == 0);
}

void menus_show_app_name(bool shown) {
	pl_setting_set("app-menu-name", shown ? NULL : "false");
}

void menus_toggle_app_name(void) {
	menus_show_app_name(!app_name_shown());
}

static const uint32_t *app_menu_icon(void) {
	static uint32_t px[MBAR_ICON_W * MBAR_ICON_H];
	static bool made;
	if (!made) {
		const uint32_t *icon = pl_icon(PL_ICON_APPLICATION, PL_ICON_SMALL);
		/* 16 px in the 20 x 15 slot: centred across; a blank top row goes. */
		int top = 0;
		for (int x = 0; icon && x < PL_ICON_SMALL && top == 0; x++) {
			top = icon[x] >> 24 ? -1 : 0;
		}
		top = top < 0 ? 0 : 1;
		for (int y = 0; icon && y < MBAR_ICON_H && y + top < PL_ICON_SMALL; y++) {
			for (int x = 0; x < PL_ICON_SMALL; x++) {
				px[y * MBAR_ICON_W + x + (MBAR_ICON_W - PL_ICON_SMALL) / 2] =
					icon[(y + top) * PL_ICON_SMALL + x];
			}
		}
		made = true;
	}
	return px;
}

static bool setting_on(const char *key) {
	char v[16];
	return pl_setting(key, v, sizeof(v)) && (strcmp(v, "1") == 0 || strcmp(v, "true") == 0);
}

void menus_hourly_chime(void) {
	if (!setting_on("clock-hour-chime")) {
		return;
	}
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	static int last_year = -1, last_yday = -1, last_hour = -1;
	if (tm.tm_min != 0 || (last_year == tm.tm_year && last_yday == tm.tm_yday &&
			last_hour == tm.tm_hour)) {
		return;
	}
	last_year = tm.tm_year;
	last_yday = tm.tm_yday;
	last_hour = tm.tm_hour;
	pl_sound_play("chirp");
}

static struct {
	bool on;
	unsigned latched, locked;
} sticky;

void menus_set_sticky(bool on, unsigned latched, unsigned locked) {
	sticky.on = on;
	sticky.latched = latched;
	sticky.locked = locked;
}

void menus_sticky_title(bool on, unsigned latched, unsigned locked, char *out, size_t size) {
	/* Shift, Control, Option, Command, in the bits' order. */
	static const char *const glyphs[] = { "\u21E7", "\u2303", "\u2325", "\u2318" };
	out[0] = '\0';
	if (!on) {
		return;
	}
	size_t n = 0;
	for (int i = 0; i < 4; i++) {
		const unsigned bit = 1u << i;
		if (locked & bit) {
			n += (size_t)snprintf(out + n, n < size ? size - n : 0, "[%s]", glyphs[i]);
		} else if (latched & bit) {
			n += (size_t)snprintf(out + n, n < size ? size - n : 0, "%s", glyphs[i]);
		}
	}
	if (n == 0) {
		snprintf(out, size, "\u00B7"); /* on, holding nothing */
	}
}

/* The clock's options come from the Date & Time panel (desktop.conf). */
static bool clock_shown(void) {
	char v[16];
	return !(pl_setting("clock", v, sizeof(v)) && strcmp(v, "off") == 0);
}

static void clock_text(char *buf, size_t size) {
	time_t now = time(NULL);
	struct tm tm;
	tzset(); /* the time zone may have just changed */
	localtime_r(&now, &tm);
	if (clock_shows_date) {
		char format[16];
		const char *date_format = "%b %-d, %Y";
		if (pl_setting("clock-date-format", format, sizeof(format))) {
			if (strcmp(format, "dmy") == 0) {
				date_format = "%-d %b %Y";
			} else if (strcmp(format, "ymd") == 0) {
				date_format = "%Y-%m-%d";
			}
		}
		strftime(buf, size, date_format, &tm);
		return;
	}
	char day[16] = "";
	if (setting_on("clock-weekday")) {
		strftime(day, sizeof(day), "%a ", &tm);
	}
	if (setting_on("clock-24h")) {
		snprintf(buf, size, "%s%d:%02d", day, tm.tm_hour, tm.tm_min);
		return;
	}
	/* Mac OS 8 default: 12-hour clock, "9:41 AM". */
	int hour = tm.tm_hour % 12 ? tm.tm_hour % 12 : 12;
	snprintf(buf, size, "%s%d:%02d %s", day, hour, tm.tm_min, tm.tm_hour < 12 ? "AM" : "PM");
}

static void render_menu_text(struct mb_menu *m) {
	if (m->title) {
		m->title_text = text_render(m->title, TEXT_UNLIMITED);
	}
	for (int i = 0; i < m->n; i++) {
		struct mb_item *it = &m->items[i];
		if (it->submenu) {
			render_menu_text(it->submenu);
		}
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

	/* Left: the logo menu, then the front app's menus. */
	struct mb_menu *logo = new_menu(bar->left, &bar->n_left, NULL);
	_Static_assert(PL_LOGO_W == MBAR_ICON_W && PL_LOGO_H == MBAR_ICON_H,
		"the logo fills the menu bar's icon slot");
	logo->icon = logo_pixels_themed();
	launch_fill_logo_menu(logo, apps, n_apps);
	if (finder_front) {
		add_finder_menus(bar->left, &bar->n_left);
	} else if (!appmenu_add_menus(bar->left, &bar->n_left, MAX_TITLES)) {
		/* A program that gave us its own menus (appmenu.c) shows those. */
		add_app_menus(bar->left, &bar->n_left, front->app_id);
	}

	/* Right: clock, then the Application menu. */
	bar->has_clock = clock_shown();
	if (bar->has_clock) {
		char clock[48];
		clock_text(clock, sizeof(clock));
		new_menu(bar->right, &bar->n_right, clock);
	}

	char held[64];
	menus_sticky_title(sticky.on, sticky.latched, sticky.locked, held, sizeof(held));
	if (*held) {
		new_menu(bar->right, &bar->n_right, held);
	}

	kbmenu_add(bar->right, &bar->n_right);

	const bool named = app_name_shown();
	struct mb_menu *am = new_menu(bar->right, &bar->n_right,
		!named ? NULL : front ? front->name : "Sniffer");
	const uint32_t *own = named ? NULL : app_icon_title(front ? front->app_id : FINDER_APP_ID);
	am->icon = named ? NULL : own ? own : app_menu_icon();
	am->handle = true;
	if (front) {
		char label[256];
		snprintf(label, sizeof(label), "Hide %s", front->name);
		add(am, label, 0, true, ACT_HIDE_APP, front->app_id);
	} else {
		add(am, "Hide Sniffer", 0, false, ACT_NONE, NULL);
	}
	add(am, "Hide Others", 0, n_apps > 1 || (n_apps == 1 && !front), ACT_HIDE_OTHERS,
		front ? front->app_id : "");
	add(am, "Show All", 0, n_apps > 0, ACT_SHOW_ALL, NULL);
	sep(am);
	if (n_apps == 0) {
		struct mb_item *it = add(am, "Sniffer", 0, true, ACT_NONE, NULL);
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
		bar->right_titles[i] = (struct mbar_title){
			.text = bar->right[i].title_text,
			.icon = bar->right[i].icon,
			.handle = bar->right[i].handle,
		};
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
		if (m->items[i].submenu) {
			free_menu(m->items[i].submenu);
			free(m->items[i].submenu);
		}
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

static const char *finder_command_arg(const char *arg, bool option_held) {
	return option_held && arg && strcmp(arg, "empty-trash") == 0
		? "empty-trash-option" : arg;
}

static void toggle_setting(const char *key) {
	char current[16];
	const bool present = pl_setting(key, current, sizeof(current));
	const bool enabled = present
		? strcmp(current, "0") != 0 && strcmp(current, "false") != 0
		: strcmp(key, "hint-bubbles") == 0;
	if (!pl_setting_set(key, enabled ? "0" : "1")) {
		fprintf(stderr, "zacos9-menubar: could not save setting %s\n", key);
	}
}

void menus_perform(struct mb_item *item, bool option_held) {
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
		if (option_held) {
			n_apps = apps_collect(apps, MAX_ITEMS);
			const char *previous = app_switch_option_previous(apps, n_apps, item->arg);
			if (previous) {
				app_set_hidden(previous, true);
			}
		}
		app_activate(item->arg, menubar_seat());
		break;
	case ACT_KEYBOARD_LAYOUT:
		menubar_set_keyboard_layout(atoi(item->arg));
		break;
	case ACT_TOGGLE_SETTING:
		toggle_setting(item->arg);
		break;
	case ACT_EDIT_COMMAND:
		keys_send_command(item->key);
		break;
	case ACT_EDIT_CLEAR:
		keys_send_clear();
		break;
	case ACT_FINDER:
		finder_send(finder_command_arg(item->arg, option_held));
		break;
	case ACT_DBUSMENU:
		appmenu_perform(item->arg);
		break;
	case ACT_ABOUT:
		finder_send("about");
		break;
	case ACT_NONE:
		break;
	}
}

/* Folder Tricks for the folder clicked: Attach ▸ the Folder Tricks
 * folder's programs (and the folder itself), Remove ▸ those attached. */
static void add_trick_items(struct mb_menu *m) {
	const struct finder_extras *x = finder_extras();
	if (!x->can_trick) {
		return;
	}
	sep(m);
	struct mb_item *attach = add(m, "Attach a Folder Trick", 0, true, ACT_NONE, NULL);
	if (!attach) {
		return;
	}
	attach->submenu = calloc(1, sizeof(*attach->submenu));
	char command[256];
	for (int i = 0; i < x->n_available; i++) {
		bool on = false;
		for (int j = 0; j < x->n_attached; j++) {
			on |= strcmp(x->attached[j], x->available[i]) == 0;
		}
		snprintf(command, sizeof(command), "attach-trick %s", x->available[i]);
		struct mb_item *it = add(attach->submenu, x->available[i], 0, !on, ACT_FINDER, command);
		if (it) {
			it->checked = on; /* attached already */
		}
	}
	if (x->n_available) {
		sep(attach->submenu);
	}
	add(attach->submenu, "Open Folder Tricks Folder", 0, true, ACT_FINDER, "open-folder-tricks");
	if (x->n_attached) {
		struct mb_item *remove = add(m, "Remove a Folder Trick", 0, true, ACT_NONE, NULL);
		if (remove) {
			remove->submenu = calloc(1, sizeof(*remove->submenu));
			for (int i = 0; i < x->n_attached; i++) {
				snprintf(command, sizeof(command), "remove-trick %s", x->attached[i]);
				add(remove->submenu, x->attached[i], 0, true, ACT_FINDER, command);
			}
		}
	}
}

/* The Contextual Menu Items folder's commands that apply. */
static void add_context_commands(struct mb_menu *m) {
	const struct finder_extras *x = finder_extras();
	if (x->n_commands) {
		sep(m);
	}
	for (int i = 0; i < x->n_commands; i++) {
		char command[32];
		snprintf(command, sizeof(command), "contextual %d", i);
		add(m, x->commands[i], 0, true, ACT_FINDER, command);
	}
}

/* A contextual menu (Control-click in the Sniffer) on an "icon", "disk",
 * "trash", a "window" background or the "desktop". Like Mac OS 9's, it
 * lists only what can be done right now, and no ⌘-keys. */
void menus_context(struct mb_menu *m, const char *kind) {
	memset(m, 0, sizeof(*m));
	const struct finder_state *fs = finder_state();
	if (!fs->connected) {
		return;
	}
	const bool sel = fs->selection > 0;
	if (strcmp(kind, "icon") == 0 && sel) {
		add(m, "Open", 0, true, ACT_FINDER, "open");
		/* As the File menu: Print after Open, when there's a document. */
		if (fs->documents) {
			add(m, "Print", 0, true, ACT_FINDER, "print");
		}
		add(m, "Move To Trash", 0, true, ACT_FINDER, "move-to-trash");
		sep(m);
		add(m, "Get Info", 0, true, ACT_FINDER, "get-info");
		add_label_submenu(m, true, fs->label);
		add(m, "Duplicate", 0, true, ACT_FINDER, "duplicate");
		add(m, "Make Alias", 0, true, ACT_FINDER, "make-alias");
		add(m, "Add To Favorites", 0, true, ACT_FINDER, "add-to-favorites");
		add_trick_items(m);
		add_context_commands(m);
	} else if (strcmp(kind, "disk") == 0 && sel) {
		add(m, "Open", 0, true, ACT_FINDER, "open");
		add(m, "Get Info", 0, true, ACT_FINDER, "get-info");
		add_label_submenu(m, true, fs->label);
		if (fs->network || fs->unmount) {
			/* A real disk; the startup disk is the Macintosh view. */
			add(m, "Add To Favorites", 0, true, ACT_FINDER, "add-to-favorites");
		}
		if (fs->network || fs->unmount || (fs->erase && fs->selection == 1)) {
			sep(m);
		}
		if (fs->network || fs->unmount) {
			add(m, "Eject", 0, true, ACT_FINDER, "eject");
		}
		if (fs->erase && fs->selection == 1) {
			add(m, "Erase Disk…", 0, true, ACT_FINDER, "erase-disk");
		}
		add_context_commands(m);
	} else if (strcmp(kind, "trash") == 0 && sel) {
		add(m, "Open", 0, true, ACT_FINDER, "open");
		if (fs->trash) {
			add(m, empty_trash_title(), 0, true, ACT_FINDER, "empty-trash");
		}
	} else if (strcmp(kind, "window") == 0 && fs->window) {
		add(m, "New Folder", 0, true, ACT_FINDER, "new-folder");
		sep(m);
		struct mb_item *view = add(m, "View", 0, true, ACT_NONE, NULL);
		view->submenu = calloc(1, sizeof(*view->submenu));
		add(view->submenu, "as Icons", 0, true, ACT_FINDER, "view-icons")->checked = fs->view == 0;
		add(view->submenu, "as Buttons", 0, true, ACT_FINDER, "view-buttons")->checked = fs->view == 2;
		add(view->submenu, "as List", 0, true, ACT_FINDER, "view-list")->checked = fs->view == 1;
		if (fs->view == 0) {
			add(m, "Clean Up", 0, true, ACT_FINDER, "clean-up");
			add_arrange_submenu(m, true);
		} else if (fs->view == 1) {
			add_sort_submenu(m, true, fs);
		}
		add(m, "View Options…", 0, true, ACT_FINDER, "view-options");
		sep(m);
		add(m, "Get Info", 0, true, ACT_FINDER, "get-info");
		add(m, "Close Window", 0, true, ACT_FINDER, "close-window");
		add_trick_items(m);
		add_context_commands(m);
	} else if (strcmp(kind, "desktop") == 0) {
		add(m, "New Folder", 0, true, ACT_FINDER, "new-folder");
		sep(m);
		add(m, "Clean Up", 0, true, ACT_FINDER, "clean-up");
		add_arrange_submenu(m, true);
		add(m, "View Options…", 0, true, ACT_FINDER, "view-options");
		sep(m);
		char command[PATH_MAX + 64];
		sibling_program("zacos9-appearance", command, sizeof(command));
		strncat(command, " desktop", sizeof(command) - strlen(command) - 1);
		add(m, "Change Desktop Background…", 0, true, ACT_LAUNCH, command);
		add_trick_items(m);
		add_context_commands(m);
	}
	if (pl_puppy_mode()) {
		puppy_trim(m);
	}
	render_menu_text(m);
}

void menus_path(struct mb_menu *m, const char *names) {
	memset(m, 0, sizeof(*m));
	char *copy = strdup(names);
	int i = 0;
	for (char *save = NULL, *name = strtok_r(copy, "\t", &save); name;
			name = strtok_r(NULL, "\t", &save), i++) {
		char command[32];
		snprintf(command, sizeof(command), "path-menu %d", i);
		add(m, name, 0, true, ACT_FINDER, command);
	}
	free(copy);
	render_menu_text(m);
}

void menus_free_menu(struct mb_menu *m) {
	free_menu(m);
}
