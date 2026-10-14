/* Function keys that open things (src/fkeys.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fkeys.h"
#include "settings.h"

static int fails;

static void check(bool ok, const char *what) {
	printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

int main(void) {
	char dir[] = "/tmp/zacos9-fkeys-XXXXXX";
	if (!mkdtemp(dir)) {
		return 1;
	}
	setenv("XDG_CONFIG_HOME", dir, 1);
	const uint32_t LOGO = 64, SHIFT = 1;
	char out[1024];

	check(fkey_number(XKB_KEY_F1) == 1 && fkey_number(XKB_KEY_F12) == 12 &&
		fkey_number(XKB_KEY_a) == 0, "F1..F12 are keys 1..12");
	check(!fkey_target(0, LOGO, XKB_KEY_F5, out, sizeof(out)), "an unassigned key is the program's");
	pl_setting_set("fkey-5", "/usr/share/applications/firefox-esr.desktop");
	pl_setting_set("fkey-6", "/home/me/Work Notes");
	check(fkey_target(0, LOGO, XKB_KEY_F5, out, sizeof(out)) &&
		strcmp(out, "/usr/share/applications/firefox-esr.desktop") == 0, "an assigned key opens its item");
	check(!fkey_target(SHIFT, LOGO, XKB_KEY_F5, out, sizeof(out)) &&
		!fkey_target(LOGO, LOGO, XKB_KEY_F5, out, sizeof(out)), "with other keys held, it's the program's");
	pl_setting_set("fkeys-command", "true");
	check(!fkey_target(0, LOGO, XKB_KEY_F5, out, sizeof(out)) &&
		fkey_target(LOGO, LOGO, XKB_KEY_F5, out, sizeof(out)),
		"F-keys as function keys: ⌘ with one opens its item");

	fkey_command("/usr/share/applications/firefox-esr.desktop", out, sizeof(out));
	check(strcmp(out, "gio launch '/usr/share/applications/firefox-esr.desktop'") == 0,
		"an application starts through gio launch");
	fkey_command("/home/me/Bob's Notes", out, sizeof(out));
	check(strcmp(out, "xdg-open '/home/me/Bob'\\''s Notes'") == 0, "anything else opens, quoted for sh");
	check(fkey_app_id("/usr/share/applications/firefox-esr.desktop", out, sizeof(out)) &&
		strcmp(out, "firefox-esr") == 0 && !fkey_app_id("/home/me/Work Notes", out, sizeof(out)),
		"an application's id, to bring it forward when it is running");

	char cmd[256];
	snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
	if (system(cmd) != 0) {
		fails++;
	}
	return fails ? 1 : 0;
}
