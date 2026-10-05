#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include "settings.h"

static void reap_children(int signal_number) {
	(void)signal_number;
	int saved_errno = errno;
	while (waitpid(-1, NULL, WNOHANG) > 0) {}
	errno = saved_errno;
}

int main(void) {
	char line[256];
	while (fgets(line, sizeof(line), stdin)) {
		line[strcspn(line, "\n")] = '\0';
		if (strcmp(line, "reaper") == 0) {
			struct sigaction action = { .sa_handler = reap_children, .sa_flags = SA_RESTART };
			sigemptyset(&action.sa_mask);
			sigaction(SIGCHLD, &action, NULL);
		} else if (strncmp(line, "start ", 6) == 0) {
			pl_sound_loop_start(line + 6);
		} else if (strcmp(line, "stop") == 0) {
			pl_sound_loop_stop();
		} else if (strncmp(line, "event ", 6) == 0) {
			pl_sound_event(line + 6);
		} else if (strncmp(line, "preview ", 8) == 0) {
			pl_sound_preview(line + 8, 3);
		} else if (strcmp(line, "quit") == 0) {
			return 0; /* Deliberately do not call stop: test pipe EOF cleanup. */
		}
		puts("ok");
		fflush(stdout);
	}
	return 0;
}
