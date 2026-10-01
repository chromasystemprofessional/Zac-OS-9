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

bool pl_setting(const char *key, char *out, size_t size) {
	char path[1024];
	config_path(path, sizeof(path));
	FILE *f = fopen(path, "r");
	if (!f) {
		return false;
	}
	char line[512];
	bool found = false;
	while (!found && fgets(line, sizeof(line), f)) {
		char *eq = strchr(line, '=');
		if (!eq) {
			continue;
		}
		*eq = '\0';
		if (strcmp(trim(line), key) == 0) {
			snprintf(out, size, "%s", trim(eq + 1));
			found = out[0] != '\0';
		}
	}
	fclose(f);
	return found;
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
