#include <assert.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

/* Exercise the private state parser and menu builder without a Wayland server. */
#include "finderlink.c"
#include "menus.c"

static int logouts;
void logout_begin(void) {
	logouts++;
}
static char context_kind[32];
void menubar_context_menu(const char *kind) {
	snprintf(context_kind, sizeof(context_kind), "%s", kind);
}
static char path_names[256];
void menubar_path_menu(const char *names) {
	snprintf(path_names, sizeof(path_names), "%s", names);
}
void sibling_program(const char *program, char *out, size_t size) {
	snprintf(out, size, "%s", program);
}

/* The labels of a contextual menu's items, separators as "-". */
static void context_labels(const char *state, const char *kind, char *out, size_t size) {
	parse_line(state);
	struct mb_menu m;
	menus_context(&m, kind);
	out[0] = '\0';
	for (int i = 0; i < m.n; i++) {
		strncat(out, m.items[i].label ? m.items[i].label : "-", size - strlen(out) - 1);
		strncat(out, "|", size - strlen(out) - 1);
	}
	menus_free_menu(&m);
}

/* `eject`: Special > Eject is enabled (any selected disk, local or network). */
static void check(const char *line, bool enabled, bool eject) {
	parse_line(line);
	struct mb_bar bar = {0};
	add_finder_menus(bar.left, &bar.n_left);
	bool found = false;
	bool foundEject = false, foundLogOut = false;
	for (int m = 0; m < bar.n_left; m++) {
		if (strcmp(bar.left[m].title, "Special") != 0) {
			continue;
		}
		for (int i = 0; i < bar.left[m].n; i++) {
			const struct mb_item *item = &bar.left[m].items[i];
			if (item->arg && strcmp(item->arg, "erase-disk") == 0) {
				assert(strcmp(item->label, "Erase Disk…") == 0);
				assert(item->action == ACT_FINDER && item->enabled == enabled);
				found = true;
			} else if (item->arg && strcmp(item->arg, "eject") == 0) {
				assert(strcmp(item->label, "Eject") == 0 && item->key == 'E');
				assert(item->action == ACT_FINDER && item->enabled == eject);
				foundEject = true;
			} else if (item->arg && strcmp(item->arg, "log-out") == 0) {
				assert(strcmp(item->label, "Log Out…") == 0 && item->action == ACT_FINDER);
				foundLogOut = true;
			}
		}
	}
	assert(found && foundEject && foundLogOut);
	menus_free(&bar);
}

int main(void) {
	setenv("XDG_RUNTIME_DIR", "/nonexistent-zacos9-menu-test", 1);
	check("state selection=1 window=0 trash=0 view=0 label=0 erase=1", true, false);
	check("state selection=2 window=0 trash=0 view=0 label=0 erase=1", false, false);
	check("state selection=0 window=0 trash=0 view=0 label=0 erase=1", false, false);
	check("state selection=1 window=0 trash=0 view=0 label=0 erase=0", false, false);
	check("state selection=1 window=0 trash=0 view=0 label=0", false, false);
	check("state selection=1 window=0 trash=0", false, false);
	check("state selection=1 window=0 trash=0 view=0 label=0 erase=0 network=1 unmount=0",
		false, true);
	check("state selection=1 window=0 trash=0 view=0 label=0 erase=1 network=0 unmount=1",
		true, true);
	/* Several disks selected: Eject takes them all; Erase only one. */
	check("state selection=2 window=0 trash=0 view=0 label=0 erase=1 network=1 unmount=1",
		false, true);
	check("state selection=0 window=0 trash=0 view=0 label=0 erase=1 network=0 unmount=0",
		false, false);
	check("state selection=1 window=0 trash=0", false, false);
	/* Contextual menus: only what can be done now, by what was clicked. */
	char got[512];
	context_labels("state selection=1 window=1 trash=0 view=0 label=0", "icon", got, sizeof(got));
	assert(strcmp(got, "Open|Move To Trash|-|Get Info|Label|Duplicate|Make Alias|Add To Favorites|") == 0);
	context_labels("state selection=1 window=0 trash=0 view=0 label=0 erase=1 network=0 unmount=1",
		"disk", got, sizeof(got));
	assert(strcmp(got, "Open|Get Info|Label|Add To Favorites|-|Eject|Erase Disk…|") == 0);
	context_labels("state selection=1 window=0 trash=0 view=0 label=0", "disk", got, sizeof(got));
	assert(strcmp(got, "Open|Get Info|Label|") == 0); /* the startup disk */
	context_labels("state selection=1 window=0 trash=1", "trash", got, sizeof(got));
	assert(strcmp(got, "Open|Empty Trash…|") == 0);
	/* Without Warn before emptying, no ellipsis: it doesn't ask. */
	char conf[] = "/tmp/zacos9-menu-test-XXXXXX";
	assert(mkdtemp(conf));
	char dir[sizeof(conf) + 16], file[sizeof(conf) + 32];
	snprintf(dir, sizeof(dir), "%s/zacos9", conf);
	snprintf(file, sizeof(file), "%s/desktop.conf", dir);
	mkdir(dir, 0700);
	FILE *f = fopen(file, "w");
	fputs("trash-warn=false\n", f);
	fclose(f);
	setenv("XDG_CONFIG_HOME", conf, 1);
	context_labels("state selection=1 window=0 trash=1", "trash", got, sizeof(got));
	assert(strcmp(got, "Open|Empty Trash|") == 0);
	unsetenv("XDG_CONFIG_HOME");
	unlink(file);
	rmdir(dir);
	rmdir(conf);
	context_labels("state selection=1 window=0 trash=0", "trash", got, sizeof(got));
	assert(strcmp(got, "Open|") == 0);
	context_labels("state selection=0 window=1 trash=0 view=0", "window", got, sizeof(got));
	assert(strcmp(got, "New Folder|-|View|Clean Up|Arrange|View Options…|-|Get Info|Close Window|") == 0);
	context_labels("state selection=0 window=1 trash=0 view=1", "window", got, sizeof(got));
	assert(strcmp(got, "New Folder|-|View|Sort List|View Options…|-|Get Info|Close Window|") == 0);
	context_labels("state selection=0 window=0 trash=0 view=0", "desktop", got, sizeof(got));
	assert(strcmp(got, "New Folder|-|Clean Up|Arrange|View Options…|-|Change Desktop Background…|") == 0);
	assert(!parse_line("context desktop") && strcmp(context_kind, "desktop") == 0);

	/* View > Sort List: the list's columns shown (Name, Date Modified =1,
	 * Size =2, Label =5), the sorted one checked; Edit > Preferences…. */
	parse_line("state selection=0 window=1 trash=0 view=1 label=-1 erase=0 network=0 unmount=0 "
		"sort=5 columns=39 custom=1");
	struct mb_bar views = {0};
	add_finder_menus(views.left, &views.n_left);
	const struct mb_item *sortList = NULL, *viewOptions = NULL, *prefs = NULL;
	for (int m = 0; m < views.n_left; m++) {
		for (int i = 0; i < views.left[m].n; i++) {
			const struct mb_item *it = &views.left[m].items[i];
			if (it->label && strcmp(it->label, "Sort List") == 0) {
				sortList = it;
			} else if (it->arg && strcmp(it->arg, "view-options") == 0) {
				viewOptions = it;
			} else if (it->arg && strcmp(it->arg, "preferences") == 0) {
				prefs = it;
			} else if (it->arg && strcmp(it->arg, "reset-columns") == 0) {
				assert(it->enabled && strcmp(it->label, "Reset Column Positions") == 0);
			}
		}
	}
	assert(sortList && sortList->enabled && sortList->submenu->n == 4);
	assert(strcmp(sortList->submenu->items[0].label, "by Name") == 0 &&
		strcmp(sortList->submenu->items[1].label, "by Date Modified") == 0 &&
		strcmp(sortList->submenu->items[2].label, "by Size") == 0 &&
		strcmp(sortList->submenu->items[3].arg, "sort label") == 0 &&
		sortList->submenu->items[3].checked && !sortList->submenu->items[0].checked);
	assert(viewOptions && viewOptions->enabled && strcmp(viewOptions->label, "View Options…") == 0);
	assert(prefs && prefs->enabled && strcmp(prefs->label, "Preferences…") == 0);
	menus_free(&views);

	/* The folder-path menu: the names, each choosing its place in the list. */
	assert(!parse_line("pathmenu Extensions\tSystem Folder\tZacintosh HD") &&
		strcmp(path_names, "Extensions\tSystem Folder\tZacintosh HD") == 0);
	struct mb_menu path;
	menus_path(&path, path_names);
	assert(path.n == 3 && strcmp(path.items[1].label, "System Folder") == 0 &&
		path.items[1].action == ACT_FINDER && strcmp(path.items[1].arg, "path-menu 1") == 0 &&
		strcmp(path.items[2].arg, "path-menu 2") == 0);
	menus_free_menu(&path);

	/* The Sniffer's "logout" line starts the logout, and isn't state. */
	assert(!parse_line("logout") && logouts == 1);
	disconnect();
	struct mb_bar bar = {0};
	add_finder_menus(bar.left, &bar.n_left);
	assert(!state.connected && !state.erase && !state.network && !state.unmount);
	menus_free(&bar);
	puts("ok: Special menu gating (Erase Disk, Eject, Log Out) and Finder state");
	return 0;
}
