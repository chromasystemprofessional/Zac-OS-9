#include <assert.h>

/* Exercise the private state parser and menu builder without a Wayland server. */
#include "finderlink.c"
#include "menus.c"

static void check(const char *line, bool enabled) {
	parse_line(line);
	struct mb_bar bar = {0};
	add_finder_menus(bar.left, &bar.n_left);
	bool found = false;
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
			}
		}
	}
	assert(found);
	menus_free(&bar);
}

int main(void) {
	setenv("XDG_RUNTIME_DIR", "/nonexistent-zacos9-menu-test", 1);
	check("state selection=1 window=0 trash=0 view=0 label=0 erase=1", true);
	check("state selection=2 window=0 trash=0 view=0 label=0 erase=1", false);
	check("state selection=0 window=0 trash=0 view=0 label=0 erase=1", false);
	check("state selection=1 window=0 trash=0 view=0 label=0 erase=0", false);
	check("state selection=1 window=0 trash=0 view=0 label=0", false);
	check("state selection=1 window=0 trash=0", false);
	disconnect();
	struct mb_bar bar = {0};
	add_finder_menus(bar.left, &bar.n_left);
	assert(!state.connected && !state.erase);
	menus_free(&bar);
	puts("ok: Erase Disk menu gating and backward-compatible Finder state");
	return 0;
}
