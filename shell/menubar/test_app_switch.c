/* ⌘-Tab's choice of program: alphabetical by name, wrapping, the Sniffer
 * always among them, and the Sniffer in front when no program is. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "menubar.h"

/* toplevels.c tells the menu bar when windows come and go. */
void menubar_apps_changed(void) {
}

static const char *pick(struct app *list, int n, int step) {
	struct app copy[MAX_ITEMS];
	memcpy(copy, list, sizeof(*list) * (size_t)n);
	return app_switch_pick(copy, n, step);
}

int main(void) {
	/* Front first, as apps_collect lists them: Zebra is active. */
	struct app apps[] = {
		{ .app_id = "zebra", .name = "Zebra", .active = true },
		{ .app_id = "org.mozilla.firefox", .name = "Firefox" },
		{ .app_id = "foot", .name = "Terminal", .hidden = true },
	};
	/* By name: Firefox, Sniffer, Terminal, Zebra. */
	assert(strcmp(pick(apps, 3, 1), "org.mozilla.firefox") == 0); /* wraps */
	assert(strcmp(pick(apps, 3, -1), "foot") == 0); /* hidden ones count */

	apps[0].active = false; /* the desktop was clicked: the Sniffer is in front */
	assert(strcmp(pick(apps, 3, 1), "foot") == 0);
	assert(strcmp(pick(apps, 3, -1), "org.mozilla.firefox") == 0);

	/* Only the Sniffer: ⌘-Tab stays there. */
	assert(strcmp(pick(apps, 0, 1), FINDER_APP_ID) == 0);

	/* Names compare without case; the Sniffer isn't listed twice. */
	struct app named[] = {
		{ .app_id = FINDER_APP_ID, .name = "Sniffer" },
		{ .app_id = "b", .name = "bravo", .active = true },
		{ .app_id = "a", .name = "Alpha" },
	};
	assert(strcmp(pick(named, 3, 1), FINDER_APP_ID) == 0);
	assert(strcmp(pick(named, 3, -1), "a") == 0);

	puts("app switch: ok");
	return 0;
}
