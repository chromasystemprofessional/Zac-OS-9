/*
 * Special > Log Out, once the Sniffer has asked: every program is asked to
 * close its windows (so each can still offer to save), and the session ends
 * once none is left. A program still open after LOGOUT_WAIT_MS (one whose
 * Save dialog was cancelled, say) cancels the logout, as on Mac OS 9; the
 * Sniffer says so.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "menubar.h"

#define LOGOUT_WAIT_MS 30000

static long deadline; /* 0 while not logging out */

static long now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Programs other than the Sniffer with a window open. */
static int programs_open(void) {
	struct app apps[MAX_ITEMS];
	const int n = apps_collect(apps, MAX_ITEMS);
	int open = 0;
	for (int i = 0; i < n; i++) {
		open += strcmp(apps[i].app_id, FINDER_APP_ID) != 0;
	}
	return open;
}

void logout_begin(void) {
	struct app apps[MAX_ITEMS];
	const int n = apps_collect(apps, MAX_ITEMS);
	for (int i = 0; i < n; i++) {
		if (strcmp(apps[i].app_id, FINDER_APP_ID) != 0) {
			app_quit(apps[i].app_id);
		}
	}
	deadline = now_ms() + LOGOUT_WAIT_MS;
}

int logout_poll_timeout(void) {
	return deadline ? 250 : -1;
}

void logout_tick(void) {
	if (!deadline) {
		return;
	}
	if (programs_open() == 0) {
		deadline = 0;
		/* $ZACOS9_LOGOUT_COMMAND for tests and nested sessions. */
		const char *command = getenv("ZACOS9_LOGOUT_COMMAND");
		char own[128];
		if (!command) {
			const char *id = getenv("XDG_SESSION_ID");
			snprintf(own, sizeof(own), "loginctl terminate-session %s",
				id && *id && strspn(id, "0123456789abcdefghijklmnopqrstuvwxyz") == strlen(id)
					? id : "self");
			command = own;
		}
		launch(command);
	} else if (now_ms() > deadline) {
		deadline = 0;
		finder_send("log-out-cancelled");
	}
}
