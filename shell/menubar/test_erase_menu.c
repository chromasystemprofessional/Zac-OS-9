#include <assert.h>

/* Exercise the private state parser and menu builder without a Wayland server. */
#include "finderlink.c"
#include "menus.c"

static void check(const char *line, bool enabled, bool network, bool unmount) {
	parse_line(line);
	struct mb_bar bar = {0};
	add_finder_menus(bar.left, &bar.n_left);
	bool found = false;
	bool foundNetwork = false, foundUnmount = false;
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
			} else if (item->arg && strcmp(item->arg, "disconnect-network") == 0) {
				assert(strcmp(item->label, "Disconnect Network Drive") == 0);
				assert(item->action == ACT_FINDER && item->enabled == network);
				foundNetwork = true;
			} else if (item->arg && strcmp(item->arg, "unmount-disk") == 0) {
				assert(strcmp(item->label, "Unmount Disk") == 0);
				assert(item->action == ACT_FINDER && item->enabled == unmount);
				foundUnmount = true;
			}
		}
	}
	assert(found && foundNetwork && foundUnmount);
	menus_free(&bar);
}

int main(void) {
	setenv("XDG_RUNTIME_DIR", "/nonexistent-zacos9-menu-test", 1);
	check("state selection=1 window=0 trash=0 view=0 label=0 erase=1", true, false, false);
	check("state selection=2 window=0 trash=0 view=0 label=0 erase=1", false, false, false);
	check("state selection=0 window=0 trash=0 view=0 label=0 erase=1", false, false, false);
	check("state selection=1 window=0 trash=0 view=0 label=0 erase=0", false, false, false);
	check("state selection=1 window=0 trash=0 view=0 label=0", false, false, false);
	check("state selection=1 window=0 trash=0", false, false, false);
	check("state selection=1 window=0 trash=0 view=0 label=0 erase=0 network=1 unmount=0",
		false, true, false);
	check("state selection=1 window=0 trash=0 view=0 label=0 erase=1 network=0 unmount=1",
		true, false, true);
	check("state selection=2 window=0 trash=0 view=0 label=0 erase=1 network=1 unmount=1",
		false, false, false);
	check("state selection=0 window=0 trash=0 view=0 label=0 erase=1 network=1 unmount=1",
		false, false, false);
	check("state selection=1 window=0 trash=0", false, false, false);
	disconnect();
	struct mb_bar bar = {0};
	add_finder_menus(bar.left, &bar.n_left);
	assert(!state.connected && !state.erase && !state.network && !state.unmount);
	menus_free(&bar);
	puts("ok: disk menu gating and backward-compatible Finder state");
	return 0;
}
