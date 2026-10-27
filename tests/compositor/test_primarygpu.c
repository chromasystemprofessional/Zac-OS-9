/* Which card renders (src/primarygpu.c): the one with the largest connected
 * screen goes first, so the cross-GPU copy wlroots makes for the other card
 * carries the smaller screens. */
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "primarygpu.h"

static int fails;
static const char *root;

/* One path below another; the test's names are short, but say so if not. */
static void join(char *out, size_t len, const char *dir, const char *name) {
	const int n = snprintf(out, len, "%s/%s", dir, name);
	if (n < 0 || (size_t)n >= len) {
		fprintf(stderr, "path too long: %s/%s\n", dir, name);
		exit(1);
	}
}

static void write_file(const char *path, const char *text) {
	FILE *f = fopen(path, "w");
	if (!f) {
		perror(path);
		exit(1);
	}
	fputs(text, f);
	fclose(f);
}

static void card(const char *name) {
	char dir[2 * PATH_MAX];
	join(dir, sizeof(dir), root, name);
	mkdir(dir, 0755);
}

static void connector(const char *card_name, const char *name, const char *status,
		const char *modes) {
	char label[2 * NAME_MAX], dir[2 * PATH_MAX], path[2 * PATH_MAX];
	snprintf(label, sizeof(label), "%s-%s", card_name, name);
	join(dir, sizeof(dir), root, label);
	mkdir(dir, 0755);
	join(path, sizeof(path), dir, "status");
	char line[64];
	snprintf(line, sizeof(line), "%s\n", status);
	write_file(path, line);
	join(path, sizeof(path), dir, "modes");
	write_file(path, modes);
}

/* A fresh fake /sys/class/drm and /dev/dri for one case. */
static void new_root(void) {
	char template[] = "/tmp/zacos9-primarygpu-XXXXXX";
	char *dir = mkdtemp(template);
	if (!dir) {
		perror("mkdtemp");
		exit(1);
	}
	static char kept[PATH_MAX];
	snprintf(kept, sizeof(kept), "%s", dir);
	root = kept;
	setenv("ZACOS9_DRM_ROOT", kept, 1);
	setenv("ZACOS9_DRM_DEVICE_DIR", "/dev/zacos9-test", 1);
	unsetenv("WLR_DRM_DEVICES");
	unsetenv("WLR_BACKENDS");
	unsetenv("WAYLAND_DISPLAY");
	unsetenv("WAYLAND_SOCKET");
}

static void expect(const char *want, const char *what) {
	char *got = primary_gpu_devices();
	const bool same = (got == NULL) == (want == NULL) && (!want || strcmp(got, want) == 0);
	printf("%s  %s (%s)\n", same ? "ok  " : "FAIL", what, got ? got : "left alone");
	fails += !same;
	free(got);
}

int main(void) {
	new_root();
	card("card0");
	connector("card0", "HDMI-A-1", "connected", "1920x1080\n1280x720\n");
	card("card1");
	connector("card1", "HDMI-A-1", "connected", "3840x2160\n1920x1080\n");
	expect("/dev/zacos9-test/card1:/dev/zacos9-test/card0",
		"the card with the largest screen renders");

	new_root();
	card("card0");
	connector("card0", "DP-1", "connected", "1920x1080\n");
	card("card1");
	connector("card1", "HDMI-A-1", "connected", "1920x1080\n");
	expect("/dev/zacos9-test/card0:/dev/zacos9-test/card1",
		"equal screens keep the firmware's order");

	new_root();
	card("card0");
	connector("card0", "HDMI-A-1", "connected", "1920x1080\n");
	card("card1");
	connector("card1", "HDMI-A-1", "disconnected", "3840x2160\n");
	expect("/dev/zacos9-test/card0:/dev/zacos9-test/card1",
		"a card with nothing plugged in does not win");

	new_root();
	card("card0");
	connector("card0", "DP-1", "connected", "1280x720\n");
	connector("card0", "HDMI-A-1", "connected", "2560x1440\n");
	card("card1");
	connector("card1", "DP-1", "connected", "1920x1080\n");
	expect("/dev/zacos9-test/card0:/dev/zacos9-test/card1",
		"a card's largest of several screens counts");

	new_root();
	card("card0");
	connector("card0", "HDMI-A-1", "connected", "1920x1080\n");
	expect(NULL, "one card is left as it is");

	new_root();
	card("card0");
	connector("card0", "HDMI-A-1", "connected", "1920x1080\n");
	card("card1");
	connector("card1", "HDMI-A-1", "connected", "3840x2160\n");
	setenv("WLR_DRM_DEVICES", "/dev/dri/card1", 1);
	expect(NULL, "a choice already made is left alone");

	new_root();
	card("card0");
	connector("card0", "HDMI-A-1", "connected", "1920x1080\n");
	card("card1");
	connector("card1", "HDMI-A-1", "connected", "3840x2160\n");
	setenv("WAYLAND_DISPLAY", "wayland-0", 1);
	expect(NULL, "a nested session leaves the host's cards alone");

	return fails ? 1 : 0;
}
