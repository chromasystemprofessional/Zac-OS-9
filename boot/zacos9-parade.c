/*
 * zacos9-parade: the startup screen's extension parade, as it happens.
 * Started first thing at boot (zacos9-parade.service), it watches the
 * kernel's module list and, as each module behind one of the curated
 * extensions (lib/extensions.c) loads, tells the boot splash to show that
 * extension's icon ("plymouth update --status=zacos9-ext:INDEX", see
 * boot/plymouth/zacos9.script) and adds its key to /run/zacos9/parade,
 * from which the compositor's Welcome screen carries the parade on
 * (compositor/src/startup.c). It stops after a couple of minutes.
 *
 * For tests: ZACOS9_PARADE_MODULES (the module list to read),
 * ZACOS9_PARADE_FILE, ZACOS9_PARADE_PLYMOUTH (the plymouth command),
 * ZACOS9_PARADE_SECONDS (how long to watch) and ZACOS9_PARADE_ANYTIME=1
 * (run even long after boot).
 */
#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "extensions.h"

#define POLL_MS 50
#define PING_RETRY_MS 250
#define LATE_BOOT_S 300 /* started later than this: not at boot, so do nothing */

extern char **environ;

static const char *env_or(const char *name, const char *fallback) {
	const char *v = getenv(name);
	return v && *v ? v : fallback;
}

static double clock_s(clockid_t id) {
	struct timespec ts;
	clock_gettime(id, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int run(const char *const argv[]) {
	pid_t pid;
	if (posix_spawn(&pid, argv[0], NULL, NULL, (char *const *)argv, environ) != 0) {
		return -1;
	}
	int status;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR) {
			return -1;
		}
	}
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int main(void) {
	if (clock_s(CLOCK_BOOTTIME) > LATE_BOOT_S && !getenv("ZACOS9_PARADE_ANYTIME")) {
		return 0;
	}
	const char *modules_path = env_or("ZACOS9_PARADE_MODULES", "/proc/modules");
	const char *record_path = env_or("ZACOS9_PARADE_FILE", "/run/zacos9/parade");
	const char *plymouth = env_or("ZACOS9_PARADE_PLYMOUTH", "/usr/bin/plymouth");
	const double seconds = atof(env_or("ZACOS9_PARADE_SECONDS", "120"));

	if (!getenv("ZACOS9_PARADE_FILE")) {
		mkdir("/run/zacos9", 0755);
	}
	FILE *record = fopen(record_path, "w");
	if (!record) {
		fprintf(stderr, "zacos9-parade: cannot write %s: %s\n", record_path, strerror(errno));
		return 1;
	}
	fchmod(fileno(record), 0644);

	/* In the order they loaded; `sent` of them are on the splash. */
	int order[256], n = 0, sent = 0;
	bool seen[256] = { false };
	double next_ping = 0;
	const double end = clock_s(CLOCK_MONOTONIC) + seconds;

	for (;;) {
		FILE *f = fopen(modules_path, "r");
		if (f) {
			/* The kernel lists the newest first. */
			int found[512], k = 0;
			char line[512];
			while (k < 512 && fgets(line, sizeof(line), f)) {
				line[strcspn(line, " \n")] = 0;
				const struct pl_extension *e = pl_extension_for_module(line);
				if (e) {
					found[k++] = (int)(e - pl_extensions);
				}
			}
			fclose(f);
			while (k > 0) {
				const int i = found[--k];
				if (!seen[i] && n < 256) {
					seen[i] = true;
					order[n++] = i;
					fprintf(record, "%s\n", pl_extensions[i].key);
					fflush(record);
				}
			}
		}

		const double now = clock_s(CLOCK_MONOTONIC);
		if (sent < n && now >= next_ping) {
			const char *ping[] = { plymouth, "--ping", NULL };
			if (run(ping) != 0) {
				next_ping = now + PING_RETRY_MS / 1000.0;
			}
			while (sent < n && now >= next_ping) {
				char status[64];
				snprintf(status, sizeof(status), "--status=zacos9-ext:%d", order[sent]);
				const char *update[] = { plymouth, "update", status, NULL };
				if (run(update) != 0) {
					next_ping = now + PING_RETRY_MS / 1000.0;
					break;
				}
				sent++;
			}
		}

		if (now >= end) {
			break;
		}
		const struct timespec pause = { 0, POLL_MS * 1000000L };
		nanosleep(&pause, NULL);
	}
	fclose(record);
	return 0;
}
