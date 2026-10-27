/*
 * Consecutive identical log lines print once (logfilter.h).
 *
 * The lines keep wlroots' shape (time since start, level, message) since
 * wlroots' own logger is replaced by this one; the last repeat count is
 * printed by atexit() so a session's log doesn't swallow its final message.
 */
#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/util/log.h>

#include "logfilter.h"

#define MESSAGE_MAX 1024

static const char *const headers[] = {
	[WLR_SILENT] = "",
	[WLR_ERROR] = "[ERROR]",
	[WLR_INFO] = "[INFO]",
	[WLR_DEBUG] = "[DEBUG]",
};

static FILE *output;
static enum wlr_log_importance verbosity = WLR_ERROR;
static struct timespec start;
static bool started;

static char last[MESSAGE_MAX];
static enum wlr_log_importance last_importance = WLR_SILENT;
static unsigned seen; /* times the message in "last" was logged, 0 for none */

static void line(enum wlr_log_importance importance, const char *message) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	long long ms = 0;
	if (started) {
		ms = (now.tv_sec - start.tv_sec) * 1000LL +
			(now.tv_nsec - start.tv_nsec) / 1000000;
	}
	if (ms < 0) {
		ms = 0;
	}
	FILE *f = output ? output : stderr;
	fprintf(f, "%02lld:%02lld:%02lld.%03lld %s %s\n", ms / 3600000,
		ms / 60000 % 60, ms / 1000 % 60, ms % 1000,
		importance < WLR_LOG_IMPORTANCE_LAST ? headers[importance] : "", message);
	fflush(f);
}

void logfilter_flush(void) {
	if (seen < 2) {
		seen = 0;
		return;
	}
	char note[64];
	snprintf(note, sizeof(note), "previous message repeated %u more time%s",
		seen - 1, seen == 2 ? "" : "s");
	line(last_importance, note);
	seen = 0;
}

void logfilter_log(enum wlr_log_importance importance, const char *fmt, va_list args) {
	if (importance > verbosity) {
		return;
	}
	char message[MESSAGE_MAX];
	vsnprintf(message, sizeof(message), fmt, args);
	if (seen && importance == last_importance && strcmp(message, last) == 0) {
		if (seen < 1000000) {
			seen++;
		}
		return;
	}
	logfilter_flush();
	snprintf(last, sizeof(last), "%s", message);
	last_importance = importance;
	seen = 1;
	line(importance, message);
}

void logfilter_set_output(FILE *out) {
	output = out;
}

void logfilter_init(enum wlr_log_importance level) {
	verbosity = level;
	clock_gettime(CLOCK_MONOTONIC, &start);
	started = true;
	atexit(logfilter_flush);
	wlr_log_init(level, logfilter_log);
}
