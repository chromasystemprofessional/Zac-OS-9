#include <assert.h>

/* Exercise the private state parser and menu builder without a Wayland server. */
#include "finderlink.c"
#include "menus.c"

static int logouts;
void logout_begin(void) {
	logouts++;
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
