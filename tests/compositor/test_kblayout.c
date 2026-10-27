/* Keyboard layouts (src/kblayout.c): the Keyboard panel's choice, else
 * Debian's /etc/default/keyboard; their keymap, one group each; and the
 * layout in use kept across sessions. */
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "kblayout.h"
#include "settings.h"

static int fails;

static void check(bool ok, const char *what) {
	printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

/* The keysym of evdev key `key` in layout `group`, unshifted. */
static xkb_keysym_t sym(struct xkb_keymap *keymap, int key, int group) {
	const xkb_keysym_t *syms;
	return xkb_keymap_key_get_syms_by_level(keymap, key + 8, group, 0, &syms) > 0 ? syms[0] : 0;
}

int main(void) {
	char dir[] = "/tmp/zacos9-kblayout-XXXXXX";
	if (!mkdtemp(dir)) {
		return 1;
	}
	setenv("XDG_CONFIG_HOME", dir, 1);
	setenv("XDG_STATE_HOME", dir, 1);
	char defaults[300];
	snprintf(defaults, sizeof(defaults), "%s/keyboard", dir);
	setenv("ZACOS9_KEYBOARD_DEFAULTS", defaults, 1);
	FILE *f = fopen(defaults, "w");
	fputs("# KEYBOARD CONFIGURATION FILE\nXKBMODEL=\"pc105\"\nXKBLAYOUT=\"us,de\"\n"
		"XKBVARIANT=\",nodeadkeys\"\nXKBOPTIONS=\"ctrl:nocaps\"\nBACKSPACE=\"guess\"\n", f);
	fclose(f);

	struct kb_layouts layouts;
	kb_layouts_load(&layouts);
	check(layouts.n == 2 && strcmp(layouts.codes[0], "us") == 0 &&
		strcmp(layouts.codes[1], "de(nodeadkeys)") == 0,
		"with nothing chosen, Debian's layouts (with their variants)");
	check(strcmp(layouts.model, "pc105") == 0 && strcmp(layouts.options, "ctrl:nocaps") == 0,
		"and its model and options");

	pl_setting_set("keyboard-layouts", "us,us(dvorak),bad;rm -rf,us,gb,fr");
	struct kb_layouts chosen;
	kb_layouts_load(&chosen);
	check(chosen.n == 4 && strcmp(chosen.codes[1], "us(dvorak)") == 0 &&
		strcmp(chosen.codes[2], "gb") == 0 && strcmp(chosen.codes[3], "fr") == 0,
		"the Keyboard panel's choice: odd codes and repeats left out, four at most");
	check(strcmp(chosen.options, "ctrl:nocaps") == 0, "Debian's options still apply");
	check(!kb_layouts_equal(&layouts, &chosen), "a changed choice is a change");
	check(kb_layouts_find(&chosen, "gb") == 2 && kb_layouts_find(&chosen, "de") == -1,
		"layouts are found by code");

	struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = kb_layouts_keymap(ctx, &chosen);
	check(keymap && xkb_keymap_num_layouts(keymap) == 4, "one keymap, a group per layout");
	if (keymap) {
		check(sym(keymap, KEY_Q, 0) == XKB_KEY_q && sym(keymap, KEY_Q, 1) == XKB_KEY_apostrophe &&
			sym(keymap, KEY_Q, 3) == XKB_KEY_a,
			"Q is q in U.S., ' in Dvorak, a in French");
		xkb_keymap_unref(keymap);
	}
	struct kb_layouts unknown;
	memset(&unknown, 0, sizeof(unknown));
	kb_layouts_parse(&unknown, "zz-nosuch");
	keymap = kb_layouts_keymap(ctx, &unknown);
	check(keymap != NULL, "a layout the system lacks still leaves a keyboard");
	xkb_keymap_unref(keymap);
	xkb_context_unref(ctx);

	char code[64];
	check(!kb_layout_remembered(code, sizeof(code)), "no layout remembered at first");
	kb_layout_remember("us(dvorak)");
	check(kb_layout_remembered(code, sizeof(code)) && strcmp(code, "us(dvorak)") == 0,
		"the layout in use is remembered");

	char cmd[400];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
	if (system(cmd) != 0) {
		fails++;
	}
	printf("%d failure%s\n", fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
