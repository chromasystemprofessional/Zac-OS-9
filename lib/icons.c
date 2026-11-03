#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <cairo.h>

#include "icons.h"
#include "theme.h"

/* The set: assets/icons/platinum-icons.picon, compiled by
 * tools/icons/build_icons.py. */
#include "icons_data.h"

/* Theme bundle overrides (icons/<name>-<size>.png), by kind. Only kinds
 * the extractor maps from the user's own Mac OS 9 install are listed;
 * everything else always uses the built-in art. */
static const char *icon_bundle_name(enum pl_icon_kind kind) {
	switch (kind) {
	case PL_ICON_FOLDER: return "folder";
	case PL_ICON_SYSTEM_FOLDER: return "system-folder";
	case PL_ICON_TRASH_EMPTY: return "trash-empty";
	case PL_ICON_TRASH_FULL: return "trash-full";
	case PL_ICON_DOCUMENT: return "document";
	case PL_ICON_SHARED_FOLDER: return "shared-folder";
	case PL_ICON_DISK: return "disk";
	case PL_ICON_NETWORK_DISK: return "network-disk";
	case PL_ICON_DISK_IMAGE: return "disk-image";
	case PL_ICON_CAUTION: return "caution";
	default: return NULL;
	}
}

/* The bundle's icons/<name>-<size>.png when the active theme supplies it,
 * else NULL. Reloads when the file moves (a theme installed or removed) or
 * is rebuilt in place (a reimport). */
static uint32_t *load_themed(int kind, int slot, int size) {
	static uint32_t *cache[PL_ICON_COUNT][2];
	static char *last_path[PL_ICON_COUNT][2];
	static struct timespec last_mtime[PL_ICON_COUNT][2];
	static off_t last_size[PL_ICON_COUNT][2];
	char relative[64];
	snprintf(relative, sizeof(relative), "icons/%s-%d.png", icon_bundle_name(kind), size);
	char path[4096];
	if (!zac_theme_asset(relative, path, sizeof(path))) {
		free(cache[kind][slot]);
		cache[kind][slot] = NULL;
		free(last_path[kind][slot]);
		last_path[kind][slot] = NULL;
		return NULL;
	}
	struct stat st;
	const bool known = stat(path, &st) == 0;
	if (cache[kind][slot] && last_path[kind][slot] && known &&
			strcmp(last_path[kind][slot], path) == 0 &&
			st.st_mtim.tv_sec == last_mtime[kind][slot].tv_sec &&
			st.st_mtim.tv_nsec == last_mtime[kind][slot].tv_nsec &&
			st.st_size == last_size[kind][slot]) {
		return cache[kind][slot];
	}
	free(cache[kind][slot]);
	cache[kind][slot] = NULL;
	free(last_path[kind][slot]);
	last_path[kind][slot] = NULL;
	if (known) {
		last_mtime[kind][slot] = st.st_mtim;
		last_size[kind][slot] = st.st_size;
	}
	cairo_surface_t *s = cairo_image_surface_create_from_png(path);
	if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS ||
			cairo_image_surface_get_width(s) != size ||
			cairo_image_surface_get_height(s) != size) {
		cairo_surface_destroy(s);
		return NULL;
	}
	cairo_surface_flush(s);
	const uint32_t *in = (const uint32_t *)cairo_image_surface_get_data(s);
	const int stride = cairo_image_surface_get_stride(s) / 4;
	const cairo_format_t format = cairo_image_surface_get_format(s);
	uint32_t *px = malloc(sizeof(uint32_t) * size * size);
	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			uint32_t v = in[y * stride + x];
			if (format != CAIRO_FORMAT_ARGB32) {
				v |= 0xFF000000u;
			}
			const uint32_t a = v >> 24;
			if (a && a < 255) {
				const uint32_t r = (((v >> 16) & 0xFF) * 255 + a / 2) / a;
				const uint32_t g = (((v >> 8) & 0xFF) * 255 + a / 2) / a;
				const uint32_t b = ((v & 0xFF) * 255 + a / 2) / a;
				v = a << 24 | (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 |
					(b > 255 ? 255 : b);
			}
			px[y * size + x] = v;
		}
	}
	cairo_surface_destroy(s);
	cache[kind][slot] = px;
	last_path[kind][slot] = strdup(path);
	return px;
}

/* Custom folders keep their center symbols over a themed folder: kinds
 * whose built-in art is the plain folder plus an emblem. */
static bool has_emblem(enum pl_icon_kind kind) {
	switch (kind) {
	case PL_ICON_SYSTEM_FOLDER:
	case PL_ICON_APPLICATIONS_FOLDER:
	case PL_ICON_HOME_FOLDER:
	case PL_ICON_CONTROL_PANELS:
	case PL_ICON_SF_APPEARANCE:
	case PL_ICON_SF_EXTENSIONS:
	case PL_ICON_SF_SOUNDS:
	case PL_ICON_SF_STARTUP_ITEMS:
	case PL_ICON_SF_APPLICATION_SUPPORT:
	case PL_ICON_SF_DEVICE_DRIVERS:
	case PL_ICON_SF_NETWORK:
	case PL_ICON_SF_SYSTEM_LOGS:
	case PL_ICON_SF_SOFTWARE_COMPONENTS:
	case PL_ICON_SF_FONTS:
	case PL_ICON_SF_FAVORITES:
	case PL_ICON_SF_TOY_BOX_ITEMS:
	case PL_ICON_SF_FOLDER_TRICKS:
	case PL_ICON_SF_CONTEXTUAL_MENU_ITEMS:
	case PL_ICON_SF_PREFERENCES:
	case PL_ICON_SF_UTILITIES:
		return true;
	default:
		return false;
	}
}

static uint32_t *decode(const char *const *rows, int size) {
	uint32_t *px = calloc((size_t)size * size, sizeof(*px));
	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			px[y * size + x] = icon_color(rows[y][x]);
		}
	}
	return px;
}

const uint32_t *pl_icon(enum pl_icon_kind kind, int size) {
	static uint32_t *cache[PL_ICON_COUNT][2];
	int s = size >= 32 ? 0 : 1;
	if (kind < 0 || kind >= PL_ICON_COUNT) {
		kind = PL_ICON_DOCUMENT;
	}
	if (icon_bundle_name(kind)) {
		uint32_t *themed = load_themed(kind, s, s == 0 ? 32 : 16);
		if (themed) {
			return themed;
		}
	}
	/* A themed folder under a built-in emblem: the emblem is the pixels
	 * where the custom art differs from the plain folder. */
	static uint32_t *composed[PL_ICON_COUNT][2];
	static char *composed_path[PL_ICON_COUNT][2];
	if (has_emblem(kind)) {
		char relative[64];
		snprintf(relative, sizeof(relative), "icons/folder-%d.png", s == 0 ? 32 : 16);
		char path[4096];
		if (zac_theme_asset(relative, path, sizeof(path))) {
			if (composed[kind][s] && composed_path[kind][s] &&
					strcmp(composed_path[kind][s], path) == 0) {
				return composed[kind][s];
			}
			free(composed[kind][s]);
			composed[kind][s] = NULL;
			free(composed_path[kind][s]);
			composed_path[kind][s] = NULL;
			uint32_t *themed = load_themed(PL_ICON_FOLDER, s, s == 0 ? 32 : 16);
			if (themed) {
				const int px = s == 0 ? 32 : 16;
				uint32_t *out = malloc(sizeof(uint32_t) * (size_t)px * px);
				if (out) {
					memcpy(out, themed, sizeof(uint32_t) * (size_t)px * px);
					if (!cache[PL_ICON_FOLDER][s]) {
						cache[PL_ICON_FOLDER][s] = decode(icon_rows[PL_ICON_FOLDER][s], px);
					}
					if (!cache[kind][s]) {
						cache[kind][s] = decode(icon_rows[kind][s], px);
					}
					const uint32_t *plain = cache[PL_ICON_FOLDER][s];
					const uint32_t *custom = cache[kind][s];
					for (int i = 0; i < px * px; i++) {
						if (custom[i] != plain[i]) {
							out[i] = custom[i];
						}
					}
					composed[kind][s] = out;
					composed_path[kind][s] = strdup(path);
					if (composed_path[kind][s]) {
						return out;
					}
					free(out);
					composed[kind][s] = NULL;
				}
			}
		} else {
			free(composed[kind][s]);
			composed[kind][s] = NULL;
			free(composed_path[kind][s]);
			composed_path[kind][s] = NULL;
		}
	}
	if (!cache[kind][s]) {
		cache[kind][s] = decode(icon_rows[kind][s], s == 0 ? 32 : 16);
	}
	return cache[kind][s];
}

enum pl_icon_kind pl_icon_for_program(const char *desktop_id) {
	static const struct {
		const char *id;
		enum pl_icon_kind kind;
	} programs[] = {
		{ "zacos9-barkrecorder", PL_ICON_APP_BARKRECORDER },
		{ "zacos9-calculator", PL_ICON_APP_CALCULATOR },
		{ "zacos9-dogears", PL_ICON_APP_DOGEARS },
		{ "zacos9-jigsaw", PL_ICON_APP_JIGSAW },
		{ "zacos9-keytags", PL_ICON_APP_KEYTAGS },
		{ "zacos9-notepad", PL_ICON_APP_NOTEPAD },
		{ "zacos9-pedigree", PL_ICON_APP_PEDIGREE },
		{ "zacos9-plotter", PL_ICON_APP_PLOTTER },
		{ "zacos9-stash", PL_ICON_APP_STASH },
		/* The control panels (lib/controlpanels.h). */
		{ "zacos9-accessibility", PL_ICON_CP_ACCESSIBILITY },
		{ "zacos9-appearance", PL_ICON_CP_APPEARANCE },
		{ "zacos9-bluetooth", PL_ICON_EXT_BLUETOOTH },
		{ "zacos9-collarpanel", PL_ICON_CP_COLLAR },
		{ "zacos9-datetime", PL_ICON_CP_DATETIME },
		{ "zacos9-energysaver", PL_ICON_EXT_POWER },
		{ "zacos9-fileexchange", PL_ICON_CP_FILETRANSLATOR },
		{ "zacos9-filesharing", PL_ICON_SHARED_FOLDER },
		{ "zacos9-general", PL_ICON_CP_GENERAL },
		{ "zacos9-internet", PL_ICON_INTERNET_LOCATION },
		{ "zacos9-keyboard", PL_ICON_CP_KEYBOARD },
		{ "zacos9-keychain", PL_ICON_CP_KEYCHAIN },
		{ "zacos9-memory", PL_ICON_CP_MEMORY },
		{ "zacos9-monitors", PL_ICON_EXT_DISPLAY },
		{ "zacos9-mouse", PL_ICON_CP_MOUSE },
		{ "zacos9-numbers", PL_ICON_CP_NUMBERS },
		{ "zacos9-retriever", PL_ICON_CP_RETRIEVER },
		{ "zacos9-screensaver", PL_ICON_CP_SCREENSAVER },
		{ "zacos9-screensharing", PL_ICON_EXT_DISPLAY },
		{ "zacos9-sound", PL_ICON_EXT_AUDIO },
		{ "zacos9-speech", PL_ICON_CP_SPEECH },
		{ "zacos9-startupdisk", PL_ICON_DISK },
		{ "zacos9-tablet", PL_ICON_EXT_INPUT },
		{ "zacos9-tcpip", PL_ICON_EXT_ETHERNET },
		{ "zacos9-text", PL_ICON_CP_TEXT },
		{ "zacos9-trackpad", PL_ICON_CP_TRACKPAD },
		{ "zacos9-users", PL_ICON_CP_USERS },
		{ "zacos9-walkies", PL_ICON_CP_WALKIES },
		{ "zacos9-zacmenu", PL_ICON_CP_ZACMENU },
	};
	if (!desktop_id) {
		return PL_ICON_APPLICATION;
	}
	const char *base = strrchr(desktop_id, '/');
	base = base ? base + 1 : desktop_id;
	const size_t len = strcspn(base, ".");
	for (size_t i = 0; i < sizeof(programs) / sizeof(programs[0]); i++) {
		if (strlen(programs[i].id) == len && strncmp(base, programs[i].id, len) == 0) {
			return programs[i].kind;
		}
	}
	return PL_ICON_APPLICATION;
}

const struct pl_label pl_labels[PL_LABEL_COUNT] = {
	{ "None", 0 },
	{ "Essential", 0xFFFF6403 },
	{ "Hot", 0xFFDD0806 },
	{ "In Progress", 0xFFF20884 },
	{ "Cool", 0xFF02ABEA },
	{ "Personal", 0xFF0000D4 },
	{ "Project 1", 0xFF1FB714 },
	{ "Project 2", 0xFF562C05 },
};

/* Each channel scaled by the label's: white becomes the label color and
 * black stays black. TODO: compare with a real Mac's labelled icons. */
static uint32_t tint(uint32_t v, uint32_t label) {
	uint32_t out = 0xFF000000u;
	for (int shift = 0; shift < 24; shift += 8) {
		uint32_t a = (v >> shift) & 0xFF, b = (label >> shift) & 0xFF;
		out |= (a * b / 255) << shift;
	}
	return out;
}

void pl_icon_paint(struct pl_canvas *c, int x, int y, enum pl_icon_kind kind,
		int size, bool selected) {
	pl_icon_paint_label(c, x, y, kind, size, selected, 0);
}

void pl_icon_paint_label(struct pl_canvas *c, int x, int y, enum pl_icon_kind kind,
		int size, bool selected, uint32_t label_color) {
	const uint32_t *px = pl_icon(kind, size);
	for (int j = 0; j < size; j++) {
		for (int i = 0; i < size; i++) {
			uint32_t v = px[j * size + i];
			if (!(v >> 24)) {
				continue;
			}
			if (label_color) {
				v = tint(v, label_color);
			}
			if (selected) {
				v = 0xFF000000u | ((v >> 1) & 0x7F7F7F);
			}
			pl_put(c, x + i, y + j, v);
		}
	}
}
