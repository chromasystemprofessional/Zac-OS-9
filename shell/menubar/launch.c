#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glib.h>

#include "appfilter.h"
#include "icons.h"
#include "electron.h"
#include "menubar.h"

static void detach_session(void *data) {
	if (setsid() < 0) {
		const char message[] = "Could not detach application launch session\n";
		ssize_t written = write(STDERR_FILENO, message, sizeof(message) - 1);
		(void)written;
		_exit(127);
	}
}

/* Run a shell command fully detached from the menu bar. Electron apps
 * start under X11, where they give us their menus (lib/electron.h). */
void launch(const char *command) {
	char *electron = pl_electron_command(command);
	if (electron) {
		command = electron;
	}
	uint32_t cookie = launch_feedback_begin();
	char *argv[] = { "/bin/sh", "-c", (char *)command, NULL };
	GPid pid;
	GError *error = NULL;
	if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_DEFAULT, detach_session, NULL, &pid, &error)) {
		launch_feedback_cancel(cookie);
		fprintf(stderr, "Could not launch %s: %s\n", command, error->message);
		g_error_free(error);
		free(electron);
		return;
	}
	launch_feedback_update(cookie, pid);
	g_spawn_close_pid(pid);
	free(electron);
}

/* A shell command running `program` from next to this binary (build tree
 * or install), else from $PATH. */
void sibling_program(const char *program, char *out, size_t size) {
	char exe[PATH_MAX];
	ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (len > 0) {
		exe[len] = '\0';
		char *slash = strrchr(exe, '/');
		if (slash) {
			*slash = '\0';
			char path[PATH_MAX + 64];
			snprintf(path, sizeof(path), "%s/%s", exe, program);
			if (access(path, X_OK) == 0) {
				snprintf(out, size, "exec '%s'", path);
				return;
			}
		}
	}
	snprintf(out, size, "exec %s", program);
}

static bool in_path(const char *program) {
	const char *path = getenv("PATH");
	if (!path) {
		return false;
	}
	char *copy = strdup(path);
	bool found = false;
	for (char *dir = strtok(copy, ":"); dir && !found; dir = strtok(NULL, ":")) {
		char full[1024];
		snprintf(full, sizeof(full), "%s/%s", dir, program);
		found = access(full, X_OK) == 0;
	}
	free(copy);
	return found;
}

/* Exec= minus the %f/%u/... field codes we don't fill in. */
static char *strip_field_codes(const char *exec) {
	char *out = malloc(strlen(exec) + 1), *o = out;
	for (const char *p = exec; *p; p++) {
		if (p[0] == '%' && p[1]) {
			if (p[1] == '%') {
				*o++ = '%';
			}
			p++;
			continue;
		}
		*o++ = *p;
	}
	*o = '\0';
	return out;
}

struct entry {
	char *name, *exec, *path;
	bool directory;
	enum action action;
};

static bool read_desktop(const char *path, struct entry *e) {
	if (!pl_app_allowed_file(path)) {
		return false; /* a Limited account's other programs aren't listed */
	}
	FILE *f = fopen(path, "r");
	if (!f) {
		return false;
	}
	char line[1024];
	bool in_entry = false;
	e->name = e->exec = NULL;
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (line[0] == '[') {
			in_entry = strcmp(line, "[Desktop Entry]") == 0;
		} else if (in_entry && !e->name && strncmp(line, "Name=", 5) == 0) {
			e->name = strdup(line + 5);
		} else if (in_entry && !e->exec && strncmp(line, "Exec=", 5) == 0) {
			e->exec = strip_field_codes(line + 5);
		}
	}
	fclose(f);
	if (!e->name || !e->exec) {
		free(e->name);
		free(e->exec);
		return false;
	}
	return true;
}

bool launch_read_desktop(const char *path, char **name, char **exec) {
	struct entry e;
	if (!read_desktop(path, &e)) {
		return false;
	}
	*name = e.name;
	*exec = e.exec;
	return true;
}

static int by_name(const void *a, const void *b) {
	return strcasecmp(((const struct entry *)a)->name, ((const struct entry *)b)->name);
}

static void free_entries(struct entry *entries, int n) {
	for (int i = 0; i < n; i++) {
		free(entries[i].name);
		free(entries[i].exec);
		free(entries[i].path);
	}
}

static int read_items_dir(const char *dir_path, struct entry *entries, int max) {
	DIR *dir = opendir(dir_path);
	if (!dir) {
		return 0;
	}
	int n = 0;
	struct dirent *de;
	while ((de = readdir(dir)) && n < max) {
		if (de->d_name[0] == '.') {
			continue;
		}
		char path[PATH_MAX];
		const int length = snprintf(path, sizeof(path), "%s/%s", dir_path, de->d_name);
		if (length < 0 || (size_t)length >= sizeof(path)) {
			continue;
		}
		struct stat info;
		if (lstat(path, &info) != 0) {
			continue;
		}
		if (S_ISDIR(info.st_mode)) {
			entries[n] = (struct entry){
				.name = strdup(de->d_name), .path = strdup(path), .directory = true,
				.action = ACT_NONE,
			};
			n++;
			continue;
		}
		if (S_ISLNK(info.st_mode) && !strchr(path, '\n') && !strchr(path, '\r')) {
			struct stat target;
			if (stat(path, &target) == 0 && (S_ISREG(target.st_mode) || S_ISDIR(target.st_mode))) {
				entries[n++] = (struct entry){
					.name = strdup(de->d_name),
					.exec = g_strdup_printf("open-path %s", path),
					.path = strdup(path),
					.action = ACT_FINDER,
				};
			}
			continue;
		}
		const size_t len = strlen(de->d_name);
		if (!S_ISREG(info.st_mode) || len < 9 ||
				strcmp(de->d_name + len - 8, ".desktop") != 0) {
			continue;
		}
		struct entry entry = { .path = strdup(path), .action = ACT_LAUNCH };
		if (read_desktop(path, &entry)) {
			entries[n++] = entry;
		} else {
			free(entry.path);
		}
	}
	closedir(dir);
	qsort(entries, n, sizeof(entries[0]), by_name);
	return n;
}

static struct mb_item *add_item(struct mb_menu *menu, const char *label, bool enabled,
		enum action action, const char *arg);

/* An item's icon: one of ours, or a desktop entry's own. */
static void with_kind(struct mb_item *it, enum pl_icon_kind kind) {
	if (it) {
		it->icon_kind = (int)kind + 1;
	}
}
static void with_desktop_icon(struct mb_item *it, const char *desktop, enum pl_icon_kind otherwise) {
	if (!it) {
		return;
	}
	it->icon_px = desktop && menu_icon_for_desktop ? menu_icon_for_desktop(desktop) : NULL;
	if (!it->icon_px) {
		with_kind(it, otherwise);
	}
}

static int populate_items_menu(struct mb_menu *menu, const char *path, int depth) {
	if (depth > 4 || menu->n >= MAX_ITEMS) {
		return 0;
	}
	struct entry entries[MAX_ITEMS];
	const int n = read_items_dir(path, entries, MAX_ITEMS);
	int added = 0;
	for (int i = 0; i < n && menu->n < MAX_ITEMS; i++) {
		if (entries[i].directory) {
			struct mb_menu *sub = calloc(1, sizeof(*sub));
			if (!sub) {
				continue;
			}
			const int children = populate_items_menu(sub, entries[i].path, depth + 1);
			if (children > 0) {
				with_kind(add_item(menu, entries[i].name, true, ACT_NONE, NULL), PL_ICON_FOLDER);
				menu->items[menu->n - 1].submenu = sub;
				added++;
			} else {
				free(sub);
			}
		} else {
			struct mb_item *it = add_item(menu, entries[i].name, true, entries[i].action, entries[i].exec);
			/* A program: its own icon; an alias: a folder's or a document's. */
			struct stat st;
			if (entries[i].action == ACT_LAUNCH) {
				with_desktop_icon(it, entries[i].path, PL_ICON_APPLICATION);
			} else {
				with_kind(it, entries[i].exec && stat(entries[i].exec + (strncmp(entries[i].exec, "open-path ", 10) == 0 ? 10 : 0), &st) == 0
					&& S_ISDIR(st.st_mode) ? PL_ICON_FOLDER : PL_ICON_DOCUMENT);
			}
			added++;
		}
	}
	free_entries(entries, n);
	return added;
}

static struct mb_item *add_item(struct mb_menu *menu, const char *label, bool enabled,
		enum action action, const char *arg) {
	if (menu->n >= MAX_ITEMS) {
		return NULL;
	}
	struct mb_item *it = &menu->items[menu->n++];
	*it = (struct mb_item){
		.label = label ? strdup(label) : NULL,
		.enabled = enabled,
		.action = action,
		.arg = arg ? strdup(arg) : NULL,
	};
	return it;
}

/* $XDG_CONFIG_HOME/zacos9/NAME (the Sniffer's QStandardPaths agrees). */
static void config_path(const char *name, char *out, size_t size) {
	const char *config = getenv("XDG_CONFIG_HOME");
	if (config && *config) {
		snprintf(out, size, "%s/zacos9/%s", config, name);
	} else {
		snprintf(out, size, "%s/.config/zacos9/%s", getenv("HOME") ? getenv("HOME") : "", name);
	}
}

static int by_label(const void *a, const void *b) {
	return strcasecmp(*(char *const *)a, *(char *const *)b);
}

/* Favorites: the aliases in the Favorites folder, by name; the Sniffer
 * opens the one chosen. Dimmed when there are none. */
static void add_favorites(struct mb_menu *menu) {
	char dir_path[PATH_MAX];
	config_path("Favorites", dir_path, sizeof(dir_path));
	char *names[MAX_ITEMS];
	int n = 0;
	DIR *dir = opendir(dir_path);
	if (dir) {
		struct dirent *de;
		while ((de = readdir(dir)) && n < MAX_ITEMS) {
			if (de->d_name[0] != '.') {
				names[n++] = strdup(de->d_name);
			}
		}
		closedir(dir);
	}
	qsort(names, n, sizeof(names[0]), by_label);
	with_kind(add_item(menu, "Favorites", n > 0, ACT_NONE, NULL), PL_ICON_FOLDER);
	struct mb_menu *favorites = calloc(1, sizeof(*favorites));
	menu->items[menu->n - 1].submenu = favorites;
	for (int i = 0; i < n; i++) {
		char arg[PATH_MAX + 300];
		snprintf(arg, sizeof(arg), "open-path %s/%s", dir_path, names[i]);
		struct stat st;
		char target[PATH_MAX + 300];
		snprintf(target, sizeof(target), "%s/%s", dir_path, names[i]);
		with_kind(add_item(favorites, names[i], true, ACT_FINDER, arg),
			stat(target, &st) == 0 && S_ISDIR(st.st_mode) ? PL_ICON_FOLDER : PL_ICON_DOCUMENT);
		free(names[i]);
	}
}

/* User scripts in ZacOS 9 Menu Items/Automated Tasks appear as a submenu.
 * Only executable regular files are offered; each path is shell-quoted. */
static void add_automated_tasks(struct mb_menu *menu) {
	char root[PATH_MAX], dir_path[PATH_MAX];
	config_path("ZacOS 9 Menu Items", root, sizeof(root));
	if (snprintf(dir_path, sizeof(dir_path), "%s/Automated Tasks", root) >=
			(int)sizeof(dir_path)) {
		return;
	}
	char *names[MAX_ITEMS];
	int n = 0;
	DIR *dir = opendir(dir_path);
	if (dir) {
		struct dirent *de;
		while ((de = readdir(dir)) && n < MAX_ITEMS) {
			if (de->d_name[0] == '.') {
				continue;
			}
			char path[PATH_MAX];
			if (snprintf(path, sizeof(path), "%s/%s", dir_path, de->d_name) <
					(int)sizeof(path) && access(path, X_OK) == 0 &&
					g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
				names[n++] = strdup(de->d_name);
			}
		}
		closedir(dir);
	}
	qsort(names, n, sizeof(names[0]), by_label);
	if (menu->n >= MAX_ITEMS) {
		for (int i = 0; i < n; i++) {
			free(names[i]);
		}
		return;
	}
	with_kind(add_item(menu, "Automated Tasks", n > 0, ACT_NONE, NULL), PL_ICON_FOLDER);
	struct mb_item *item = &menu->items[menu->n - 1];
	item->submenu = calloc(1, sizeof(*item->submenu));
	for (int i = 0; i < n; i++) {
		char path[PATH_MAX], *quoted;
		if (snprintf(path, sizeof(path), "%s/%s", dir_path, names[i]) >=
				(int)sizeof(path)) {
			free(names[i]);
			continue;
		}
		quoted = g_shell_quote(path);
		char command[PATH_MAX * 2];
		snprintf(command, sizeof(command), "exec %s", quoted);
		with_kind(add_item(item->submenu, names[i], true, ACT_LAUNCH, command), PL_ICON_APPLICATION);
		g_free(quoted);
		free(names[i]);
	}
}

void launch_fill_logo_menu(struct mb_menu *menu, const struct app *apps, int n_apps) {
	/* About and software tools are the top group in the Apple menu. */
	with_kind(add_item(menu, "About This Computer…", true, ACT_ABOUT, NULL), PL_ICON_EXT_CHIP);
	/* The app catalog over apt. */
	{
		char command[PATH_MAX + 64];
		sibling_program("zacos9-store", command, sizeof(command));
		with_kind(add_item(menu, "New Tricks", true, ACT_LAUNCH, command), PL_ICON_APPLICATION);
	}
	/* New ZacOS 9 releases and Debian's own updates. */
	{
		char command[PATH_MAX + 64];
		sibling_program("zacos9-update", command, sizeof(command));
		with_kind(add_item(menu, "Fetch Updates", true, ACT_LAUNCH, command), PL_ICON_EXT_OPENTRANSPORT);
	}
	/* Force Quit: the open applications, front-most first; the Finder
	 * asks first, then has zacos9-wm end the one chosen. */
	{
		struct mb_menu *quit = calloc(1, sizeof(*quit));
		for (int i = 0; i < n_apps; i++) {
			if (!*apps[i].app_id || strcmp(apps[i].app_id, FINDER_APP_ID) == 0) {
				continue;
			}
			char arg[512];
			snprintf(arg, sizeof(arg), "force-quit %s %s", apps[i].app_id, apps[i].name);
			struct mb_item *it = add_item(quit, apps[i].name, true, ACT_FINDER, arg);
			if (it && menu_icon_for_app) {
				it->icon_px = menu_icon_for_app(apps[i].app_id);
			}
			if (it && !it->icon_px) {
				with_kind(it, PL_ICON_APPLICATION);
			}
		}
		with_kind(add_item(menu, "Force Quit", quit->n > 0, ACT_NONE, NULL), PL_ICON_CAUTION);
		menu->items[menu->n - 1].submenu = quit;
	}
	add_item(menu, NULL, false, ACT_NONE, NULL);
	/* Classic Mac OS in an emulator; the Finder explains what's missing. */
	with_kind(add_item(menu, "Old Dog", true, ACT_FINDER, "classic"), PL_ICON_CLASSIC);
	/* Screen Snapshot: drag out part of the screen; it goes on the Desktop
	 * as "Picture N.png" and on the clipboard. */
	{
		char command[PATH_MAX + 64];
		sibling_program("zacos9-snapshot", command, sizeof(command));
		with_kind(add_item(menu, "Screen Snapshot", true, ACT_LAUNCH, command), PL_ICON_EXT_CAMERA);
	}
	/* Windows programs, installed through Wine. */
	{
		char command[PATH_MAX + 64];
		sibling_program("zacos9-wininstall", command, sizeof(command));
		with_kind(add_item(menu, "Windows Installer", true, ACT_LAUNCH, command), PL_ICON_WINDOWS);
	}
	/* The Network Browser: connecting to other computers, as Mac OS 9's
	 * own Apple Menu Item of the same name did. */
	{
		char command[PATH_MAX + 64];
		sibling_program("zacos9-netbrowser", command, sizeof(command));
		with_kind(add_item(menu, "Neighbourhood", true, ACT_LAUNCH, command), PL_ICON_EXT_APPLETALK);
	}
	/* Printer Picker: Mac OS 9's Chooser, which was here too. */
	{
		char command[PATH_MAX + 64];
		sibling_program("zacos9-printerpicker", command, sizeof(command));
		with_kind(add_item(menu, "Printer Picker", true, ACT_LAUNCH, command), PL_ICON_EXT_PRINTMONITOR);
	}
	/* Control Panels, as a hierarchical menu. */
	with_kind(add_item(menu, "Control Panels", true, ACT_NONE, NULL), PL_ICON_CONTROL_PANELS);
	struct mb_menu *panels = calloc(1, sizeof(*panels));
	menu->items[menu->n - 1].submenu = panels;
	/* Alphabetical, as in the Mac's Control Panels folder. */
	/* Each with an icon from ZacOS's own set (lib/icons.h). */
	const struct { const char *name, *program, *arg; enum pl_icon_kind icon; } panel_list[] = {
		{ "Accessibility", "zacos9-controlpanel", " accessibility", PL_ICON_CP_ACCESSIBILITY },
		{ "Appearance", "zacos9-appearance", "", PL_ICON_CP_APPEARANCE },
		{ "Bluetooth", "zacos9-bluetooth", "", PL_ICON_EXT_BLUETOOTH },
		{ "Collar", "zacos9-collarpanel", "", PL_ICON_CP_COLLAR },
		{ "Date & Time", "zacos9-datetime", "", PL_ICON_CP_DATETIME },
		{ "File Sharing", "zacos9-filesharing", "", PL_ICON_SHARED_FOLDER },
		{ "File Translator", "zacos9-fileexchange", "", PL_ICON_CP_FILETRANSLATOR },
		{ "General Controls", "zacos9-controlpanel", " general", PL_ICON_CP_GENERAL },
		{ "Internet", "zacos9-controlpanel", " internet", PL_ICON_INTERNET_LOCATION },
		{ "Keyboard", "zacos9-controlpanel", " keyboard", PL_ICON_CP_KEYBOARD },
		{ "Keychain Access", "zacos9-keychain", "", PL_ICON_CP_KEYCHAIN },
		{ "Memory", "zacos9-controlpanel", " memory", PL_ICON_CP_MEMORY },
		{ "Monitors", "zacos9-controlpanel", " monitors", PL_ICON_EXT_DISPLAY },
		{ "Mouse", "zacos9-controlpanel", " mouse", PL_ICON_CP_MOUSE },
		{ "Nap Time", "zacos9-controlpanel", " energysaver", PL_ICON_EXT_POWER },
		{ "Numbers", "zacos9-controlpanel", " numbers", PL_ICON_CP_NUMBERS },
		{ "Retriever", "zacos9-retriever", "", PL_ICON_CP_RETRIEVER },
		{ "Screen Saver", "zacos9-controlpanel", " screensaver", PL_ICON_CP_SCREENSAVER },
		{ "Screen Sharing", "zacos9-screensharing", "", PL_ICON_EXT_DISPLAY },
		{ "Sound", "zacos9-controlpanel", " sound", PL_ICON_EXT_AUDIO },
		{ "Speech", "zacos9-controlpanel", " speech", PL_ICON_CP_SPEECH },
		{ "Startup Disk", "zacos9-controlpanel", " startupdisk", PL_ICON_DISK },
		{ "Tablet", "zacos9-controlpanel", " tablet", PL_ICON_EXT_INPUT },
		{ "TCP/IP", "zacos9-tcpip", "", PL_ICON_EXT_ETHERNET },
		{ "Text", "zacos9-controlpanel", " text", PL_ICON_CP_TEXT },
		{ "Trackpad", "zacos9-controlpanel", " trackpad", PL_ICON_CP_TRACKPAD },
		{ "Users", "zacos9-controlpanel", " users", PL_ICON_CP_USERS },
		{ "Walkies", "zacos9-walkies", "", PL_ICON_CP_WALKIES },
		{ "Zac Menu Options", "zacos9-controlpanel", " zacmenu", PL_ICON_CP_ZACMENU },
	};
	bool toy_box_added = false;
	for (size_t i = 0; i < sizeof(panel_list) / sizeof(panel_list[0]); i++) {
		char command[PATH_MAX + 64];
		sibling_program(panel_list[i].program, command, sizeof(command));
		strncat(command, panel_list[i].arg, sizeof(command) - strlen(command) - 1);
		/* The Toy Box (the Sniffer's own window) goes in its place by name. */
		if (!toy_box_added && strcmp(panel_list[i].name, "Toy Box") > 0) {
			with_kind(add_item(panels, "Toy Box", true, ACT_FINDER, "toy-box"), PL_ICON_FOLDER);
			toy_box_added = true;
		}
		with_kind(add_item(panels, panel_list[i].name, true, ACT_LAUNCH, command), panel_list[i].icon);
	}
	add_favorites(menu);
	recent_fill_menu(menu);

	char items_dir[PATH_MAX];
	config_path("ZacOS 9 Menu Items", items_dir, sizeof(items_dir));
	const int path_length = (int)strlen(items_dir);
	const int populated = path_length >= 0 && (size_t)path_length < sizeof(items_dir)
		? populate_items_menu(menu, items_dir, 0) : 0;
	if (populated == 0) {
		/* No ZacOS 9 Menu Items folder yet: offer the terminals we have. */
		struct entry entries[MAX_ITEMS] = { 0 };
		int n = 0;
		const struct { const char *name, *program; } defaults[] = {
			{ "Terminal", "foot" },
			{ "XTerm", "xterm" },
		};
		for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
			if (in_path(defaults[i].program)) {
				entries[n].name = strdup(defaults[i].name);
				entries[n].exec = strdup(defaults[i].program);
				n++;
			}
		}
		/* And the desk accessories, as the Apple menu had them. */
		const struct { const char *name, *program; } accessories[] = {
			{ "Calculator", "zacos9-calculator" },
			{ "Dog Ears", "zacos9-dogears" },
			{ "Bark Recorder", "zacos9-barkrecorder" },
			{ "Key Tags", "zacos9-keytags" },
			{ "Notepad", "zacos9-notepad" },
			{ "Pedigree", "zacos9-pedigree" },
			{ "Stash", "zacos9-stash" },
		};
		for (size_t i = 0; i < sizeof(accessories) / sizeof(accessories[0]); i++) {
			char command[PATH_MAX + 64];
			sibling_program(accessories[i].program, command, sizeof(command));
			entries[n].name = strdup(accessories[i].name);
			entries[n].exec = strdup(command);
			n++;
		}
		/* The Apple menu listed its items alphabetically. */
		qsort(entries, n, sizeof(entries[0]), by_name);
		for (int i = 0; i < n; i++) {
		add_item(menu, entries[i].name, true, ACT_LAUNCH, entries[i].exec);
		}
		free_entries(entries, n);
	}
	add_automated_tasks(menu);
}
