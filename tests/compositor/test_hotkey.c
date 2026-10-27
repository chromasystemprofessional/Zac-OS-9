/* The Collar's hot key, as the compositor matches it. */
#include <stdio.h>
#include <wlr/types/wlr_keyboard.h>

#include "hotkey.h"

static int fails;
static void check(int ok, const char *what) {
	printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
	fails += !ok;
}

int main(void) {
	const uint32_t L = WLR_MODIFIER_LOGO, C = WLR_MODIFIER_CTRL, A = WLR_MODIFIER_ALT,
		S = WLR_MODIFIER_SHIFT;
	check(hotkey_matches("command+F8", L, XKB_KEY_F8), "command+F8 with ⌘");
	check(hotkey_matches("command+F8", C, XKB_KEY_F8), "command also takes Ctrl (clients can't tell)");
	check(!hotkey_matches("command+F8", 0, XKB_KEY_F8), "the modifier is needed");
	check(!hotkey_matches("command+F8", L | S, XKB_KEY_F8), "extra modifiers don't match");
	check(!hotkey_matches("command+F8", L | C, XKB_KEY_F8), "nor ⌘ and Ctrl together");
	check(!hotkey_matches("command+F8", L, XKB_KEY_F9), "another key doesn't match");
	check(hotkey_matches("F8", 0, XKB_KEY_F8) && !hotkey_matches("F8", L, XKB_KEY_F8),
		"a key alone");
	check(hotkey_matches("command+option+k", L | A, XKB_KEY_k), "letters");
	check(hotkey_matches("command+shift+k", L | S, XKB_KEY_K), "shifted letters");
	check(hotkey_matches("control+F2", C, XKB_KEY_F2) && !hotkey_matches("control+F2", L, XKB_KEY_F2),
		"control is Ctrl only");
	check(hotkey_matches("command+control+F2", L | C, XKB_KEY_F2), "command and control");
	check(!hotkey_matches("", L, XKB_KEY_F8) && !hotkey_matches("command", L, XKB_KEY_F8) &&
		!hotkey_matches("command+nosuchkey", L, XKB_KEY_F8) &&
		!hotkey_matches("F8+command", L, XKB_KEY_F8), "nonsense matches nothing");
	return fails ? 1 : 0;
}
