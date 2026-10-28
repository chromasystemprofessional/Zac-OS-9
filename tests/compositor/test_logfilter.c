/* Consecutive identical log lines print once, then a count (src/logfilter.c):
 * one "Failed to pick cursor plane format" per pointer motion used to bury
 * the rest of a session's log. */
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/util/log.h>

#include "logfilter.h"

static int fails;
static char text[8192];

static void check(bool ok, const char *what) {
	printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

static void logged(enum wlr_log_importance importance, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	logfilter_log(importance, fmt, args);
	va_end(args);
}

static void read_all(FILE *f) {
	fflush(f);
	const long end = ftell(f);
	fseek(f, 0, SEEK_SET);
	const size_t n = fread(text, 1, sizeof(text) - 1, f);
	text[n] = '\0';
	fseek(f, end, SEEK_SET);
}

static int count(FILE *f, const char *what) {
	read_all(f);
	int n = 0;
	for (const char *p = text; (p = strstr(p, what)); p++) {
		n++;
	}
	return n;
}

int main(void) {
	FILE *out = tmpfile();
	if (!out) {
		return 1;
	}
	logfilter_set_output(out);
	logfilter_init(WLR_ERROR);

	logged(WLR_ERROR, "Failed to pick cursor plane format");
	logged(WLR_ERROR, "Failed to pick cursor plane format");
	logged(WLR_ERROR, "Failed to pick cursor plane format");
	check(count(out, "Failed to pick cursor plane format") == 1,
		"three identical lines print once");
	check(count(out, "repeated") == 0, "the count waits for the next message");

	logged(WLR_ERROR, "something else");
	check(count(out, "previous message repeated 2 more times") == 1,
		"the next message says how many were hidden");
	check(count(out, "something else") == 1, "the next message is printed");

	logfilter_init(WLR_INFO);
	logged(WLR_INFO, "an info line");
	logged(WLR_INFO, "an info line");
	logfilter_flush();
	check(count(out, "an info line") == 1 && count(out, "repeated 1 more time") == 1,
		"a message still repeating is reported at the end");

	logfilter_flush();
	check(count(out, "repeated 1 more time") == 1, "nothing repeating adds no line");

	logged(WLR_DEBUG, "a debug line");
	check(count(out, "a debug line") == 0, "at INFO, debug lines are dropped");

	logged(WLR_ERROR, "first");
	logged(WLR_ERROR, "second");
	logged(WLR_ERROR, "second");
	logged(WLR_ERROR, "third");
	read_all(out);
	const char *first = strstr(text, "first");
	const char *second = strstr(text, "second");
	const char *third = strstr(text, "third");
	check(first && second && first < second && second < third,
		"lines keep their order");
	check(count(out, "previous message repeated") == 3,
		"each run of repeats is counted once");

	return fails ? 1 : 0;
}
