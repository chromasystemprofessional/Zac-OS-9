#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <glib.h>

#include "menubar.h"

static uint32_t launched_pid;
uint32_t launch_feedback_begin(void) {
	return 1;
}

void launch_feedback_update(uint32_t cookie, uint32_t pid) {
	assert(cookie == 1 && pid > 1);
	launched_pid = pid;
}

void launch_feedback_cancel(uint32_t cookie) {
	assert(!"Unexpected launch failure");
}

int main(int argc, char **argv) {
	if (argc == 3 && strcmp(argv[1], "--child") == 0) {
		FILE *file = fopen(argv[2], "w");
		assert(file);
		fprintf(file, "%u %u\n", (unsigned)getpid(), (unsigned)getsid(0));
		fclose(file);
		return 0;
	}
	char path[] = "/tmp/zacos9-menu-launch-XXXXXX";
	int fd = mkstemp(path);
	assert(fd >= 0);
	close(fd);
	char *exe = g_canonicalize_filename(argv[0], NULL);
	assert(exe);
	char command[4096];
	assert(snprintf(command, sizeof(command), "exec '%s' --child '%s'", exe, path)
		< (int)sizeof(command));
	g_free(exe);
	launch(command);
	unsigned int pid = 0, session = 0;
	for (int tries = 0; tries < 250; tries++) {
		FILE *file = fopen(path, "r");
		assert(file);
		int fields = fscanf(file, "%u %u", &pid, &session);
		fclose(file);
		if (fields == 2) {
			break;
		}
		struct timespec pause = { .tv_nsec = 20000000 };
		nanosleep(&pause, NULL);
	}
	unlink(path);
	assert(pid == launched_pid);
	assert(session == pid); /* Preserve the detached application session. */
	puts("ok: menu launch reports the actual detached application's PID");
	return 0;
}
