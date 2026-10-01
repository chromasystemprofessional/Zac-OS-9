#define _GNU_SOURCE
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "settings.h"

static void config_path(char *out, size_t size) {
	const char *config = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (config && *config) {
		snprintf(out, size, "%s/platinum/desktop.conf", config);
	} else {
		snprintf(out, size, "%s/.config/platinum/desktop.conf", home ? home : "");
	}
}

static char *trim(char *s) {
	while (isspace((unsigned char)*s)) {
		s++;
	}
	char *e = s + strlen(s);
	while (e > s && isspace((unsigned char)e[-1])) {
		*--e = '\0';
	}
	return s;
}

/* The file's lines, cached until its modification time changes. */
#define MAX_LINES 64
static struct {
	struct timespec mtime;
	off_t size;
	bool loaded;
	int n;
	char lines[MAX_LINES][256];
} cache;

static void load(void) {
	char path[1024];
	config_path(path, sizeof(path));
	struct stat st;
	if (stat(path, &st) != 0) {
		cache.loaded = true;
		cache.n = 0;
		cache.mtime = (struct timespec){ 0 };
		return;
	}
	if (cache.loaded && st.st_mtim.tv_sec == cache.mtime.tv_sec &&
			st.st_mtim.tv_nsec == cache.mtime.tv_nsec && st.st_size == cache.size) {
		return;
	}
	cache.loaded = true;
	cache.mtime = st.st_mtim;
	cache.size = st.st_size;
	cache.n = 0;
	FILE *f = fopen(path, "r");
	if (!f) {
		return;
	}
	while (cache.n < MAX_LINES && fgets(cache.lines[cache.n], sizeof(cache.lines[0]), f)) {
		cache.lines[cache.n][strcspn(cache.lines[cache.n], "\r\n")] = '\0';
		cache.n++;
	}
	fclose(f);
}

/* Index of `key`'s line, or -1. */
static int find_key(const char *key) {
	for (int i = 0; i < cache.n; i++) {
		const char *eq = strchr(cache.lines[i], '=');
		if (!eq) {
			continue;
		}
		char name[256];
		snprintf(name, sizeof(name), "%.*s", (int)(eq - cache.lines[i]), cache.lines[i]);
		if (strcmp(trim(name), key) == 0) {
			return i;
		}
	}
	return -1;
}

bool pl_setting(const char *key, char *out, size_t size) {
	load();
	int i = find_key(key);
	if (i < 0) {
		return false;
	}
	char value[256];
	snprintf(value, sizeof(value), "%s", strchr(cache.lines[i], '=') + 1);
	snprintf(out, size, "%s", trim(value));
	return out[0] != '\0';
}

bool pl_setting_set(const char *key, const char *value) {
	load();
	int i = find_key(key);
	if (i < 0 && value) {
		if (cache.n == 0) {
			snprintf(cache.lines[cache.n++], sizeof(cache.lines[0]), "[General]");
		}
		if (cache.n >= MAX_LINES) {
			return false;
		}
		i = cache.n++;
	}
	if (i >= 0) {
		if (value) {
			snprintf(cache.lines[i], sizeof(cache.lines[0]), "%s=%s", key, value);
		} else {
			memmove(cache.lines[i], cache.lines[i + 1], (size_t)(cache.n - i - 1) * sizeof(cache.lines[0]));
			cache.n--;
		}
	}
	char path[1024], tmp[1100];
	config_path(path, sizeof(path));
	char *slash = strrchr(path, '/');
	if (slash) {
		*slash = '\0';
		mkdir(path, 0755);
		*slash = '/';
	}
	/* Write a new file and rename it over, so readers never see half. */
	snprintf(tmp, sizeof(tmp), "%s.new", path);
	FILE *f = fopen(tmp, "w");
	if (!f) {
		return false;
	}
	for (int k = 0; k < cache.n; k++) {
		fprintf(f, "%s\n", cache.lines[k]);
	}
	if (fclose(f) != 0 || rename(tmp, path) != 0) {
		return false;
	}
	cache.loaded = false;
	return true;
}

/* ---- accents --------------------------------------------------------------- */

/* Lavender and Ivy are measured (HIG figures 2-24, 4-1 and 2-26); the
 * others keep Lavender's lightness steps with another hue (TODO: compare
 * with the real Mac OS 8 accent colours). */
static const struct {
	const char *id, *name;
	struct pl_accent ramp;
} accents[] = {
	{ "lavender", "Lavender", PL_ACCENT_DEFAULT },
	{ "graphite", "Graphite", { .corner = RGB(0xEE, 0xEE, 0xEE), .grip_hi = RGB(0xEE, 0xEE, 0xEE), .light = RGB(0xE6, 0xE6, 0xE6), .body = RGB(0xCC, 0xCC, 0xCC), .dark = RGB(0x99, 0x99, 0x99), .shadow = RGB(0x66, 0x66, 0x66), .deep = RGB(0x44, 0x44, 0x44) } },
	{ "gold", "Gold", { .corner = RGB(0xEE, 0xEE, 0xEE), .grip_hi = RGB(0xEE, 0xEE, 0xEE), .light = RGB(0xFF, 0xF2, 0xCC), .body = RGB(0xFF, 0xE6, 0x99), .dark = RGB(0xCC, 0xB3, 0x66), .shadow = RGB(0x99, 0x80, 0x33), .deep = RGB(0x88, 0x66, 0x00) } },
	{ "bronze", "Bronze", { .corner = RGB(0xEE, 0xEE, 0xEE), .grip_hi = RGB(0xEE, 0xEE, 0xEE), .light = RGB(0xF4, 0xE3, 0xD7), .body = RGB(0xE8, 0xC7, 0xB0), .dark = RGB(0xB5, 0x94, 0x7D), .shadow = RGB(0x82, 0x61, 0x4A), .deep = RGB(0x69, 0x3E, 0x1F) } },
	{ "ivy", "Ivy", PL_ACCENT_GREEN },
	{ "ocean", "Ocean", { .corner = RGB(0xEE, 0xEE, 0xEE), .grip_hi = RGB(0xEE, 0xEE, 0xEE), .light = RGB(0xCF, 0xED, 0xFC), .body = RGB(0x9E, 0xDB, 0xFA), .dark = RGB(0x6B, 0xA8, 0xC7), .shadow = RGB(0x38, 0x75, 0x94), .deep = RGB(0x07, 0x58, 0x81) } },
	{ "rose", "Rose", { .corner = RGB(0xEE, 0xEE, 0xEE), .grip_hi = RGB(0xEE, 0xEE, 0xEE), .light = RGB(0xF9, 0xD2, 0xDF), .body = RGB(0xF2, 0xA6, 0xBF), .dark = RGB(0xBF, 0x73, 0x8C), .shadow = RGB(0x8C, 0x40, 0x59), .deep = RGB(0x77, 0x11, 0x33) } },
	{ "plum", "Plum", { .corner = RGB(0xEE, 0xEE, 0xEE), .grip_hi = RGB(0xEE, 0xEE, 0xEE), .light = RGB(0xF0, 0xD6, 0xF5), .body = RGB(0xE0, 0xAD, 0xEB), .dark = RGB(0xAD, 0x7A, 0xB8), .shadow = RGB(0x7A, 0x47, 0x85), .deep = RGB(0x5F, 0x1B, 0x6D) } },
};
#define N_ACCENTS ((int)(sizeof(accents) / sizeof(accents[0])))

int pl_accent_count(void) {
	return N_ACCENTS;
}

const char *pl_accent_id(int i) {
	return i >= 0 && i < N_ACCENTS ? accents[i].id : NULL;
}

const char *pl_accent_name(int i) {
	return i >= 0 && i < N_ACCENTS ? accents[i].name : NULL;
}

struct pl_accent pl_accent_at(int i) {
	return accents[i >= 0 && i < N_ACCENTS ? i : 0].ramp;
}

int pl_accent_find(const char *id) {
	for (int i = 0; id && i < N_ACCENTS; i++) {
		if (strcmp(accents[i].id, id) == 0) {
			return i;
		}
	}
	return 0;
}

struct pl_accent pl_accent_current(void) {
	char id[64];
	return pl_accent_at(pl_setting("accent", id, sizeof(id)) ? pl_accent_find(id) : 0);
}

uint32_t pl_highlight_current(void) {
	char hex[16];
	if (pl_setting("highlight", hex, sizeof(hex)) && strlen(hex) == 6) {
		char *end;
		unsigned long v = strtoul(hex, &end, 16);
		if (*end == '\0') {
			return 0xFF000000u | (uint32_t)v;
		}
	}
	return pl_accent_current().light;
}

static bool is_dir(const char *path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

const char *pl_data_dir(void) {
	static char dir[1024];
	if (dir[0]) {
		return dir;
	}
	const char *env = getenv("PLATINUM_DATA");
	if (env && *env) {
		snprintf(dir, sizeof(dir), "%s", env);
	} else if (is_dir(PLATINUM_DATA_DIR "/sounds")) {
		snprintf(dir, sizeof(dir), "%s", PLATINUM_DATA_DIR);
	} else {
		snprintf(dir, sizeof(dir), "%s/assets", PLATINUM_SOURCE_DIR);
	}
	return dir;
}

void pl_sound_play(const char *name) {
	char chosen[64] = "platinum";
	if (!name) {
		pl_setting("alert-sound", chosen, sizeof(chosen));
		name = chosen;
	}
	if (strcmp(name, "none") == 0 || strchr(name, '/')) {
		return;
	}
	char path[1200];
	snprintf(path, sizeof(path), "%s/sounds/%s.wav", pl_data_dir(), name);
	if (access(path, R_OK) != 0) {
		return;
	}
	/* Detach fully (double fork) so the caller never waits or reaps. */
	pid_t pid = fork();
	if (pid < 0) {
		return;
	}
	if (pid == 0) {
		if (fork() == 0) {
			setsid();
			int null = open("/dev/null", O_WRONLY);
			if (null >= 0) {
				dup2(null, 1);
				dup2(null, 2);
			}
			const char *players[] = { "pw-play", "paplay", "aplay" };
			for (size_t i = 0; i < sizeof(players) / sizeof(players[0]); i++) {
				if (i == 2) {
					execlp(players[i], players[i], "-q", path, (char *)NULL);
				} else {
					execlp(players[i], players[i], path, (char *)NULL);
				}
			}
			_exit(127);
		}
		_exit(0);
	}
	waitpid(pid, NULL, 0);
}

void pl_beep(void) {
	pl_sound_play(NULL);
}
