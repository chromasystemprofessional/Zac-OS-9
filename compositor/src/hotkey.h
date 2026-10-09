#ifndef ZACOS9_HOTKEY_H
#define ZACOS9_HOTKEY_H

#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

/*
 * Hot keys as the Collar panel writes them (desktop.conf collar-hotkey):
 * modifier tokens and an xkb keysym name joined by '+', e.g.
 * "command+F8" or "command+option+k". Tokens: command (the ⌘/Super key),
 * option (Alt), control, shift.
 *
 * Clients see ⌘ as Ctrl, so the panel can't tell them apart when it
 * records a hot key: "command" also matches a Ctrl key held instead of ⌘.
 * Other modifiers must match exactly. `mods` are WLR_MODIFIER_* bits.
 */
#define HOTKEY_DEFAULT "command+F8"
bool hotkey_matches(const char *spec, uint32_t mods, xkb_keysym_t sym);

#endif
