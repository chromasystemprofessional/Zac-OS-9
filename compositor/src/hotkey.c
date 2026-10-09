#include "hotkey.h"

#include <string.h>
#include <strings.h>
#include <wlr/types/wlr_keyboard.h>

bool hotkey_matches(const char *spec, uint32_t mods, xkb_keysym_t sym) {
	if (!spec || !*spec) {
		return false;
	}
	char buf[128];
	if (strlen(spec) >= sizeof(buf)) {
		return false;
	}
	strcpy(buf, spec);
	bool command = false, option = false, control = false, shift = false;
	xkb_keysym_t key = XKB_KEY_NoSymbol;
	char *save = NULL;
	for (char *tok = strtok_r(buf, "+", &save); tok; tok = strtok_r(NULL, "+", &save)) {
		if (key != XKB_KEY_NoSymbol) {
			return false; /* the key must come last */
		}
		if (!strcasecmp(tok, "command")) {
			command = true;
		} else if (!strcasecmp(tok, "option")) {
			option = true;
		} else if (!strcasecmp(tok, "control")) {
			control = true;
		} else if (!strcasecmp(tok, "shift")) {
			shift = true;
		} else {
			key = xkb_keysym_from_name(tok, XKB_KEYSYM_CASE_INSENSITIVE);
			if (key == XKB_KEY_NoSymbol) {
				return false;
			}
		}
	}
	if (key == XKB_KEY_NoSymbol || xkb_keysym_to_lower(key) != xkb_keysym_to_lower(sym)) {
		return false;
	}
	const bool logo = mods & WLR_MODIFIER_LOGO, ctrl = mods & WLR_MODIFIER_CTRL;
	if (option != !!(mods & WLR_MODIFIER_ALT) || shift != !!(mods & WLR_MODIFIER_SHIFT)) {
		return false;
	}
	if (command && control) {
		return logo && ctrl;
	}
	if (command) {
		return logo != ctrl;
	}
	if (control) {
		return ctrl && !logo;
	}
	return !logo && !ctrl;
}
