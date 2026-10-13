/*
 * The keyboard menu (Mac OS 9's, Missing Manual ch. 12): with more than one
 * layout ticked in the Keyboard panel, the menu bar shows the one in use,
 * between the clock and the Application menu, and choosing another switches
 * to it. zacos9-wm keeps the layouts and tells us (platinum-shell-v1
 * keyboard_layouts); ⌘-Space switches there too.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "menubar.h"

#define KB_MAX 4

static struct {
	char code[64], name[128];
} layouts[KB_MAX];
static int n_layouts, current;

void kbmenu_set(const char *list, uint32_t in_use) {
	n_layouts = 0;
	const char *p = list;
	while (p && *p && n_layouts < KB_MAX) {
		const size_t len = strcspn(p, "\n");
		const char *tab = memchr(p, '\t', len);
		if (tab) {
			snprintf(layouts[n_layouts].code, sizeof(layouts[0].code), "%.*s",
				(int)(tab - p), p);
			snprintf(layouts[n_layouts].name, sizeof(layouts[0].name), "%.*s",
				(int)(p + len - tab - 1), tab + 1);
			n_layouts++;
		}
		p += len + (p[len] == '\n');
	}
	current = (int)in_use < n_layouts ? (int)in_use : 0;
}

void kbmenu_abbrev(const char *code, const char *name, char *out, size_t size) {
	/* "us" US, "us(dvorak)" US-DV; with no code, the name's first letters. */
	char text[16];
	size_t n = 0;
	const char *from = *code ? code : name;
	while (*from && *from != '(' && *from != ' ' && n < 3) {
		text[n++] = (char)toupper((unsigned char)*from++);
	}
	const char *variant = *code ? strchr(code, '(') : NULL;
	if (variant && variant[1] && variant[1] != ')') {
		text[n++] = '-';
		for (int i = 1; i <= 2 && variant[i] && variant[i] != ')'; i++) {
			text[n++] = (char)toupper((unsigned char)variant[i]);
		}
	}
	text[n] = '\0';
	snprintf(out, size, "%s", n ? text : "?");
}

void kbmenu_add(struct mb_menu *menus, int *n) {
	if (n_layouts < 2) {
		return;
	}
	char title[16];
	kbmenu_abbrev(layouts[current].code, layouts[current].name, title, sizeof(title));
	struct mb_menu *menu = menus_new_menu(menus, n, title);
	for (int i = 0; menu && i < n_layouts; i++) {
		char index[8];
		snprintf(index, sizeof(index), "%d", i);
		struct mb_item *it = menus_add_item(menu, *layouts[i].name ? layouts[i].name :
			layouts[i].code, 0, true, ACT_KEYBOARD_LAYOUT, index);
		if (it) {
			it->checked = i == current;
		}
	}
}
