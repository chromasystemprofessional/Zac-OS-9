#ifndef ZACOS9_FKEYS_H
#define ZACOS9_FKEYS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

/*
 * Function keys that open things (Mac OS 9's Keyboard > Function Keys,
 * Missing Manual p. 105): desktop.conf "fkey-1" .. "fkey-12" name an
 * application's desktop file, a document or a folder. Only keys with
 * something assigned are taken; the rest stay the programs'. With
 * "fkeys-command=true" (the Mac's "Use F1 through F12 as function keys"),
 * the keys stay the programs' and ⌘ with one opens its item.
 */

/* 1..12 for F1..F12, else 0. */
int fkey_number(xkb_keysym_t sym);
/* What this key press opens (`mods` as wlroots reports them; `logo` is
 * the ⌘ bit), into `out`; false if it opens nothing. */
bool fkey_target(uint32_t mods, uint32_t logo, xkb_keysym_t sym, char *out, size_t size);
/* The shell command that opens `target`: an application's desktop file
 * through gio launch, anything else through xdg-open, quoted for sh. */
void fkey_command(const char *target, char *out, size_t size);
/* An application's id ("firefox-esr" for .../firefox-esr.desktop), to bring
 * it forward if it is running; false for a document or folder. */
bool fkey_app_id(const char *target, char *out, size_t size);

#endif
