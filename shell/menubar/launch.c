#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <glib.h>

#include "menubar.h"

#define ITEMS_DIR "/.config/zacos9/ZacOS 9 Menu Items"

static void detach_session(void *data) {
	if (setsid() < 0) {
		const char message[] = "Could not detach application launch session\n";
		ssize_t written = write(STDERR_FILENO, message, sizeof(message) - 1);
		(void)written;
		_exit(127);
	}
}

/* Run a shell command fully detached from the menu bar. */
void launch(const char *command) {
	uint32_t cookie = launch_feedback_begin();
	char *argv[] = { "/bin/sh", "-c", (char *)command, NULL };
	GPid pid;
	GError *error = NULL;
	if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_DEFAULT, detach_session, NULL, &pid, &error)) {
		launch_feedback_cancel(cookie);
		fprintf(stderr, "Could not launch %s: %s\n", command, error->message);
		g_error_free(error);
		return;
	}
	launch_feedback_update(cookie, pid);
	g_spawn_close_pid(pid);
}

/* A shell command running `program` from next to this binary (build tree
 * or install), else from $PATH. */
static void sibling_program(const char *program, char *out, size_t size) {
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
	char *name, *exec;
};

static bool read_desktop(const char *path, struct entry *e) {
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

static int by_name(const void *a, const void *b) {
	return strcasecmp(((const struct entry *)a)->name, ((const struct entry *)b)->name);
}

static int read_items_dir(struct entry *entries, int max) {
	const char *home = getenv("HOME");
	if (!home) {
		return 0;
	}
	char dir_path[1024];
	snprintf(dir_path, sizeof(dir_path), "%s%s", home, ITEMS_DIR);
	DIR *dir = opendir(dir_path);
	if (!dir) {
		return 0;
	}
	int n = 0;
	struct dirent *de;
	while ((de = readdir(dir)) && n < max) {
		size_t len = strlen(de->d_name);
		if (len < 9 || strcmp(de->d_name + len - 8, ".desktop") != 0) {
			continue;
		}
		char path[2048];
		snprintf(path, sizeof(path), "%s/%s", dir_path, de->d_name);
		if (read_desktop(path, &entries[n])) {
			n++;
		}
	}
	closedir(dir);
	return n;
}

static void add_item(struct mb_menu *menu, const char *label, bool enabled,
		enum action action, const char *arg) {
	if (menu->n >= MAX_ITEMS) {
		return;
	}
	struct mb_item *it = &menu->items[menu->n++];
	*it = (struct mb_item){
		.label = label ? strdup(label) : NULL,
		.enabled = enabled,
		.action = action,
		.arg = arg ? strdup(arg) : NULL,
	};
}

void launch_fill_logo_menu(struct mb_menu *menu) {
	/* Shown by the Finder. */
	add_item(menu, "About This Computer…", true, ACT_ABOUT, NULL);
	add_item(menu, NULL, false, ACT_NONE, NULL);
	/* Classic Mac OS in an emulator; the Finder explains what's missing. */
	add_item(menu, "Classic", true, ACT_FINDER, "classic");
	/* The Software window: an app catalog over apt. Mac OS 9 never had
	 * one of these; it sits beside Classic, not inside Control Panels,
	 * since it is its own application rather than a settings pane. */
	{
		char command[PATH_MAX + 64];
		sibling_program("zacos9-store", command, sizeof(command));
		add_item(menu, "Software", true, ACT_LAUNCH, command);
	}
	/* Software Update: new ZacOS 9 releases and Debian's own updates. */
	{
		char command[PATH_MAX + 64];
		sibling_program("zacos9-update", command, sizeof(command));
		add_item(menu, "Software Update", true, ACT_LAUNCH, command);
	}
	/* Screen Snapshot: drag out part of the screen; it goes on the Desktop
	 * as "Picture N.png" and on the clipboard. */
	{
		char command[PATH_MAX + 64];
		sibling_program("zacos9-snapshot", command, sizeof(command));
		add_item(menu, "Screen Snapshot", true, ACT_LAUNCH, command);
	}
	/* Windows programs, installed through Wine. */
	{
		char command[PATH_MAX + 64];
		sibling_program("zacos9-wininstall", command, sizeof(command));
		add_item(menu, "Windows Installer", true, ACT_LAUNCH, command);
	}
	/* The Network Browser: connecting to other computers, as Mac OS 9's
	 * own Apple Menu Item of the same name did. */
	{
		char command[PATH_MAX + 64];
		sibling_program("zacos9-netbrowser", command, sizeof(command));
		add_item(menu, "Network Browser", true, ACT_LAUNCH, command);
	}
	/* Control Panels, as a hierarchical menu. */
	add_item(menu, "Control Panels", true, ACT_NONE, NULL);
	struct mb_menu *panels = calloc(1, sizeof(*panels));
	menu->items[menu->n - 1].submenu = panels;
	/* Alphabetical, as in the Mac's Control Panels folder. */
	const struct { const char *name, *program, *arg; } panel_list[] = {
		{ "Appearance", "zacos9-appearance", "" },
		{ "Bluetooth", "zacos9-bluetooth", "" },
		{ "Date & Time", "zacos9-datetime", "" },
		{ "File Sharing", "zacos9-filesharing", "" },
		{ "Keyboard", "zacos9-controlpanel", " keyboard" },
		{ "Monitors", "zacos9-controlpanel", " monitors" },
		{ "Mouse", "zacos9-controlpanel", " mouse" },
		{ "Sound", "zacos9-controlpanel", " sound" },
		{ "TCP/IP", "zacos9-tcpip", "" },
	};
	for (size_t i = 0; i < sizeof(panel_list) / sizeof(panel_list[0]); i++) {
		char command[PATH_MAX + 64];
		sibling_program(panel_list[i].program, command, sizeof(command));
		strncat(command, panel_list[i].arg, sizeof(command) - strlen(command) - 1);
		add_item(panels, panel_list[i].name, true, ACT_LAUNCH, command);
	}

	struct entry entries[MAX_ITEMS];
	int n = read_items_dir(entries, MAX_ITEMS - 4);
	if (n == 0) {
		/* No ZacOS 9 Menu Items folder yet: offer the terminals we have. */
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
	}
	/* The Apple menu listed its items alphabetically. */
	qsort(entries, n, sizeof(entries[0]), by_name);
	for (int i = 0; i < n; i++) {
		add_item(menu, entries[i].name, true, ACT_LAUNCH, entries[i].exec);
		free(entries[i].name);
		free(entries[i].exec);
	}
}
