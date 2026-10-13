#ifndef ZACOS9_KBLAYOUT_H
#define ZACOS9_KBLAYOUT_H

#include <stdbool.h>
#include <xkbcommon/xkbcommon.h>

/*
 * Keyboard layouts (the Keyboard panel; Mac OS 9's keyboard menu): the
 * layouts ticked in desktop.conf's "keyboard-layouts" ("us,gb,us(dvorak)"),
 * or else the ones Debian's installer chose (/etc/default/keyboard), all in
 * one keymap, one xkb group each. The model and options always come from
 * /etc/default/keyboard ($ZACOS9_KEYBOARD_DEFAULTS for tests).
 */
#define KB_MAX_LAYOUTS 4 /* xkb's limit on groups */

struct kb_layouts {
	char codes[KB_MAX_LAYOUTS][64]; /* "us", "us(dvorak)" */
	int n;                          /* 0: xkbcommon's own default */
	char model[64], options[256];
};

/* Fills `out` from the settings and the system's defaults. */
void kb_layouts_load(struct kb_layouts *out);
/* Parses a comma-separated list of codes into out->codes. */
void kb_layouts_parse(struct kb_layouts *out, const char *list);
bool kb_layouts_equal(const struct kb_layouts *a, const struct kb_layouts *b);
/* The keymap for them, or NULL. */
struct xkb_keymap *kb_layouts_keymap(struct xkb_context *ctx, const struct kb_layouts *layouts);
/* The layout's index, or -1. */
int kb_layouts_find(const struct kb_layouts *layouts, const char *code);

/* The layout in use, kept across sessions ($XDG_STATE_HOME/zacos9/
 * keyboard-layout, its code). */
void kb_layout_remember(const char *code);
bool kb_layout_remembered(char *out, int size);

#endif
