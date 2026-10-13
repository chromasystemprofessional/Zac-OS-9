/* The keyboard menu: shown with two layouts or more, titled by the one in
 * use, each layout an item, the one in use checked. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "menubar.h"

/* menus.c's building blocks, as plain as can be. */
struct mb_menu *menus_new_menu(struct mb_menu *menus, int *n, const char *title) {
	struct mb_menu *m = &menus[(*n)++];
	memset(m, 0, sizeof(*m));
	m->title = strdup(title);
	return m;
}

struct mb_item *menus_add_item(struct mb_menu *m, const char *label, char key,
		bool enabled, enum action action, const char *arg) {
	struct mb_item *it = &m->items[m->n++];
	memset(it, 0, sizeof(*it));
	it->label = strdup(label);
	it->enabled = enabled;
	it->action = action;
	it->arg = arg ? strdup(arg) : NULL;
	return it;
}

static void abbrev(const char *code, const char *name, const char *want) {
	char out[16];
	kbmenu_abbrev(code, name, out, sizeof(out));
	if (strcmp(out, want) != 0) {
		fprintf(stderr, "%s / %s: %s, not %s\n", code, name, out, want);
		exit(1);
	}
}

int main(void) {
	abbrev("us", "English (US)", "US");
	abbrev("us(dvorak)", "English (Dvorak)", "US-DV");
	abbrev("de(nodeadkeys)", "German (no dead keys)", "DE-NO");
	abbrev("", "Swedish", "SWE");

	struct mb_menu menus[3];
	int n = 0;
	kbmenu_set("us\tEnglish (US)\n", 0);
	kbmenu_add(menus, &n);
	assert(n == 0); /* one layout: no menu */

	kbmenu_set("us\tEnglish (US)\nus(dvorak)\tEnglish (Dvorak)\ngb\tEnglish (UK)\n", 1);
	kbmenu_add(menus, &n);
	assert(n == 1 && strcmp(menus[0].title, "US-DV") == 0);
	assert(menus[0].n == 3);
	assert(strcmp(menus[0].items[2].label, "English (UK)") == 0);
	assert(!menus[0].items[0].checked && menus[0].items[1].checked);
	assert(menus[0].items[2].action == ACT_KEYBOARD_LAYOUT &&
		strcmp(menus[0].items[2].arg, "2") == 0);

	kbmenu_set("us\tEnglish (US)\ngb\tEnglish (UK)\n", 7); /* out of range */
	n = 0;
	kbmenu_add(menus, &n);
	assert(n == 1 && strcmp(menus[0].title, "US") == 0);
	puts("keyboard menu: ok");
	return 0;
}
