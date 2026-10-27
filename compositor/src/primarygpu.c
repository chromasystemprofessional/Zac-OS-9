/*
 * The card with the largest connected screen renders (primarygpu.h).
 *
 * Two cards are only worth reordering when both are there. WLR_DRM_DEVICES,
 * WLR_BACKENDS and a nested session already say which cards to use, and the
 * Mac's graphics switch writes WLR_DRM_DEVICES on purpose, so those are left
 * alone.
 *
 * The kernel registers the boot VGA card first, so equal screens keep the
 * firmware's order; the card with the largest screen goes first, and the
 * cross-GPU copy then goes to the smaller screens.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "primarygpu.h"

#define MAX_GPUS 8

struct gpu {
	char card[NAME_MAX + 1];
	char node[2 * PATH_MAX];
	long long area; /* pixels of its largest connected mode, 0 when none */
};

static const char *env_or(const char *name, const char *fallback) {
	const char *v = getenv(name);
	return v && *v ? v : fallback;
}

/* "card0", but not a connector such as "card0-HDMI-A-1". */
static bool is_card(const char *name) {
	if (strncmp(name, "card", 4) != 0 || !name[4]) {
		return false;
	}
	for (const char *p = name + 4; *p; p++) {
		if (*p < '0' || *p > '9') {
			return false;
		}
	}
	return true;
}

/* The largest mode of a connector, 0 when nothing is plugged into it. */
static long long connector_area(const char *dir) {
	char path[2 * PATH_MAX], line[128];
	snprintf(path, sizeof(path), "%s/status", dir);
	FILE *f = fopen(path, "r");
	if (!f) {
		return 0;
	}
	const bool connected = fgets(line, sizeof(line), f) && strncmp(line, "connected", 9) == 0;
	fclose(f);
	if (!connected) {
		return 0;
	}
	snprintf(path, sizeof(path), "%s/modes", dir);
	f = fopen(path, "r");
	if (!f) {
		return 0;
	}
	long long best = 0;
	int w, h;
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "%dx%d", &w, &h) == 2 && (long long)w * h > best) {
			best = (long long)w * h;
		}
	}
	fclose(f);
	return best;
}

static long long card_area(const char *root, const char *card) {
	DIR *dir = opendir(root);
	if (!dir) {
		return 0;
	}
	long long best = 0;
	const size_t len = strlen(card);
	struct dirent *entry;
	while ((entry = readdir(dir))) {
		if (strncmp(entry->d_name, card, len) != 0 || entry->d_name[len] != '-') {
			continue;
		}
		char path[2 * PATH_MAX];
		snprintf(path, sizeof(path), "%s/%s", root, entry->d_name);
		const long long area = connector_area(path);
		if (area > best) {
			best = area;
		}
	}
	closedir(dir);
	return best;
}

char *primary_gpu_devices(void) {
	if (getenv("WLR_DRM_DEVICES") || getenv("WLR_BACKENDS") ||
			getenv("WAYLAND_DISPLAY") || getenv("WAYLAND_SOCKET")) {
		return NULL;
	}
	const char *root = env_or("ZACOS9_DRM_ROOT", "/sys/class/drm");
	const char *nodes = env_or("ZACOS9_DRM_DEVICE_DIR", "/dev/dri");
	DIR *dir = opendir(root);
	if (!dir) {
		return NULL;
	}
	struct gpu gpus[MAX_GPUS];
	int n = 0;
	struct dirent *entry;
	while (n < MAX_GPUS && (entry = readdir(dir))) {
		if (!is_card(entry->d_name)) {
			continue;
		}
		snprintf(gpus[n].card, sizeof(gpus[n].card), "%s", entry->d_name);
		snprintf(gpus[n].node, sizeof(gpus[n].node), "%s/%s", nodes, entry->d_name);
		gpus[n].area = card_area(root, entry->d_name);
		n++;
	}
	closedir(dir);
	if (n < 2) {
		return NULL;
	}

	for (int i = 1; i < n; i++) {
		struct gpu g = gpus[i];
		int j = i - 1;
		while (j >= 0 && (gpus[j].area < g.area ||
				(gpus[j].area == g.area && strcmp(gpus[j].card, g.card) > 0))) {
			gpus[j + 1] = gpus[j];
			j--;
		}
		gpus[j + 1] = g;
	}

	size_t size = 1;
	for (int i = 0; i < n; i++) {
		size += strlen(gpus[i].node) + 1;
	}
	char *list = calloc(1, size);
	if (!list) {
		return NULL;
	}
	for (int i = 0; i < n; i++) {
		if (i) {
			strcat(list, ":");
		}
		strcat(list, gpus[i].node);
	}
	return list;
}
