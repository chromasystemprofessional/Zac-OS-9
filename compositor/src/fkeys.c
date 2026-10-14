#include "fkeys.h"

#include <stdio.h>
#include <string.h>

#include "settings.h"

int fkey_number(xkb_keysym_t sym) {
	return sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12 ? (int)(sym - XKB_KEY_F1) + 1 : 0;
}

bool fkey_target(uint32_t mods, uint32_t logo, xkb_keysym_t sym, char *out, size_t size) {
	const int n = fkey_number(sym);
	if (!n) {
		return false;
	}
	char v[16];
	const bool command = pl_setting("fkeys-command", v, sizeof(v)) && strcmp(v, "true") == 0;
	if (mods != (command ? logo : 0)) {
		return false; /* Shift-F5, Control-Alt-F1 and the rest are the programs' (or the console's) */
	}
	char key[16];
	snprintf(key, sizeof(key), "fkey-%d", n);
	return pl_setting(key, out, size) && out[0] == '/';
}

/* `s` in single quotes, its own single quotes closed and escaped. */
static void quote(const char *s, char *out, size_t size) {
	size_t o = 0;
	if (o + 1 < size) {
		out[o++] = '\'';
	}
	for (; *s && o + 5 < size; s++) {
		if (*s == '\'') {
			memcpy(out + o, "'\\''", 4);
			o += 4;
		} else {
			out[o++] = *s;
		}
	}
	if (o + 1 < size) {
		out[o++] = '\'';
	}
	out[o < size ? o : size - 1] = '\0';
}

static bool is_desktop_file(const char *target) {
	const size_t n = strlen(target);
	return n > 8 && strcmp(target + n - 8, ".desktop") == 0;
}

void fkey_command(const char *target, char *out, size_t size) {
	char quoted[2048];
	quote(target, quoted, sizeof(quoted));
	snprintf(out, size, "%s %s", is_desktop_file(target) ? "gio launch" : "xdg-open", quoted);
}

bool fkey_app_id(const char *target, char *out, size_t size) {
	if (!is_desktop_file(target)) {
		return false;
	}
	const char *base = strrchr(target, '/');
	base = base ? base + 1 : target;
	snprintf(out, size, "%.*s", (int)(strlen(base) - 8), base);
	return out[0] != '\0';
}
