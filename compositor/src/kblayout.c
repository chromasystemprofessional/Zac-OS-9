#include "kblayout.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "settings.h"

/* Copies a shell-style KEY="value" line's value; true if it was KEY. */
static bool defaults_value(const char *line, const char *key, char *out, size_t size) {
	const size_t n = strlen(key);
	if (strncmp(line, key, n) != 0 || line[n] != '=') {
		return false;
	}
	const char *v = line + n + 1;
	size_t len = strcspn(v, "\n");
	if (len >= 2 && (v[0] == '"' || v[0] == '\'') && v[len - 1] == v[0]) {
		v++;
		len -= 2;
	}
	snprintf(out, size, "%.*s", (int)len, v);
	return true;
}

void kb_layouts_parse(struct kb_layouts *out, const char *list) {
	out->n = 0;
	const char *p = list;
	while (p && *p && out->n < KB_MAX_LAYOUTS) {
		const size_t len = strcspn(p, ",");
		char code[64];
		snprintf(code, sizeof(code), "%.*s", (int)len, p);
		/* Codes are letters, digits, _ - ( ): nothing that could inject
		 * into an xkb rules string. */
		bool ok = *code != '\0';
		for (const char *c = code; *c; c++) {
			ok &= (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
				(*c >= '0' && *c <= '9') || strchr("_-()", *c) != NULL;
		}
		if (ok && kb_layouts_find(out, code) < 0) {
			snprintf(out->codes[out->n++], sizeof(out->codes[0]), "%s", code);
		}
		p += len;
		p += *p == ',';
	}
}

void kb_layouts_load(struct kb_layouts *out) {
	memset(out, 0, sizeof(*out));
	char layout[256] = "", variant[256] = "";
	const char *path = getenv("ZACOS9_KEYBOARD_DEFAULTS");
	FILE *f = fopen(path ? path : "/etc/default/keyboard", "r");
	if (f) {
		char line[512];
		while (fgets(line, sizeof(line), f)) {
			if (!defaults_value(line, "XKBLAYOUT", layout, sizeof(layout)) &&
					!defaults_value(line, "XKBVARIANT", variant, sizeof(variant)) &&
					!defaults_value(line, "XKBMODEL", out->model, sizeof(out->model))) {
				defaults_value(line, "XKBOPTIONS", out->options, sizeof(out->options));
			}
		}
		fclose(f);
	}
	char list[512];
	if (pl_setting("keyboard-layouts", list, sizeof(list)) && *list) {
		kb_layouts_parse(out, list);
		if (out->n > 0) {
			return;
		}
	}
	/* Debian's: XKBLAYOUT="us,de" with XKBVARIANT=",nodeadkeys". */
	list[0] = '\0';
	const char *l = layout, *v = variant;
	while (*l) {
		const size_t ll = strcspn(l, ","), vl = strcspn(v, ",");
		char code[80];
		snprintf(code, sizeof(code), vl ? "%.*s(%.*s)" : "%.*s", (int)ll, l, (int)vl, v);
		if (strlen(list) + strlen(code) + 2 < sizeof(list)) {
			strcat(list, *list ? "," : "");
			strcat(list, code);
		}
		l += ll + (l[ll] == ',');
		v += vl + (v[vl] == ',');
	}
	kb_layouts_parse(out, list);
}

bool kb_layouts_equal(const struct kb_layouts *a, const struct kb_layouts *b) {
	if (a->n != b->n || strcmp(a->model, b->model) != 0 || strcmp(a->options, b->options) != 0) {
		return false;
	}
	for (int i = 0; i < a->n; i++) {
		if (strcmp(a->codes[i], b->codes[i]) != 0) {
			return false;
		}
	}
	return true;
}

int kb_layouts_find(const struct kb_layouts *layouts, const char *code) {
	for (int i = 0; i < layouts->n; i++) {
		if (strcmp(layouts->codes[i], code) == 0) {
			return i;
		}
	}
	return -1;
}

struct xkb_keymap *kb_layouts_keymap(struct xkb_context *ctx, const struct kb_layouts *layouts) {
	char layout[512] = "", variant[512] = "";
	for (int i = 0; i < layouts->n; i++) {
		const char *code = layouts->codes[i];
		const char *open = strchr(code, '(');
		const size_t ll = open ? (size_t)(open - code) : strlen(code);
		char part[80];
		snprintf(part, sizeof(part), "%s%.*s", i ? "," : "", (int)ll, code);
		strcat(layout, part);
		snprintf(part, sizeof(part), "%s%.*s", i ? "," : "",
			open ? (int)strcspn(open + 1, ")") : 0, open ? open + 1 : "");
		strcat(variant, part);
	}
	const struct xkb_rule_names names = {
		.model = *layouts->model ? layouts->model : NULL,
		.layout = layouts->n ? layout : NULL,
		.variant = layouts->n ? variant : NULL,
		.options = *layouts->options ? layouts->options : NULL,
	};
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!keymap && layouts->n) {
		/* A layout this system doesn't have: its default, rather than none. */
		keymap = xkb_keymap_new_from_names(ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
	}
	return keymap;
}

static void state_file(char *out, size_t size) {
	const char *state = getenv("XDG_STATE_HOME");
	if (state && *state) {
		snprintf(out, size, "%s/zacos9/keyboard-layout", state);
	} else {
		snprintf(out, size, "%s/.local/state/zacos9/keyboard-layout",
			getenv("HOME") ? getenv("HOME") : "");
	}
}

void kb_layout_remember(const char *code) {
	char path[1024];
	state_file(path, sizeof(path));
	/* mkdir -p of its folder. */
	char *slash = strrchr(path, '/');
	*slash = '\0';
	for (char *p = path + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(path, 0700);
			*p = '/';
		}
	}
	mkdir(path, 0700);
	*slash = '/';
	FILE *f = fopen(path, "w");
	if (f) {
		fprintf(f, "%s\n", code);
		fclose(f);
	}
}

bool kb_layout_remembered(char *out, int size) {
	char path[1024];
	state_file(path, sizeof(path));
	FILE *f = fopen(path, "r");
	if (!f) {
		return false;
	}
	const bool ok = fgets(out, size, f) != NULL;
	fclose(f);
	out[strcspn(out, "\n")] = '\0';
	return ok && *out;
}
