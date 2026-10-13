#include <assert.h>

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
	assert(strcmp(got, "Open|Move To Trash|-|Get Info|Label|Duplicate|Make Alias|") == 0);
	context_labels("state selection=1 window=0 trash=0 view=0 label=0 erase=1 network=0 unmount=1",
		"disk", got, sizeof(got));
	assert(strcmp(got, "Open|Get Info|Label|-|Eject|Erase Disk…|") == 0);
	context_labels("state selection=1 window=0 trash=0 view=0 label=0", "disk", got, sizeof(got));
	assert(strcmp(got, "Open|Get Info|Label|") == 0); /* the startup disk */
	context_labels("state selection=1 window=0 trash=1", "trash", got, sizeof(got));
	assert(strcmp(got, "Open|Empty Trash…|") == 0);
	context_labels("state selection=1 window=0 trash=0", "trash", got, sizeof(got));
	assert(strcmp(got, "Open|") == 0);
	context_labels("state selection=0 window=1 trash=0 view=0", "window", got, sizeof(got));
	assert(strcmp(got, "New Folder|-|View|Clean Up|Arrange|-|Get Info|Close Window|") == 0);
	context_labels("state selection=0 window=1 trash=0 view=1", "window", got, sizeof(got));
	assert(strcmp(got, "New Folder|-|View|-|Get Info|Close Window|") == 0); /* list view */
	context_labels("state selection=0 window=0 trash=0 view=0", "desktop", got, sizeof(got));
	assert(strcmp(got, "New Folder|-|Clean Up|Arrange|-|Change Desktop Background…|") == 0);
	assert(!parse_line("context desktop") && strcmp(context_kind, "desktop") == 0);

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
