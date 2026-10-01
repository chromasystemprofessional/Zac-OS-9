/*
 * The menu bar's line to the Finder (see shell/finder/finder.h for the
 * protocol): Finder menu commands go out, selection/window/Trash state
 * comes back so the right items are enabled.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "menubar.h"

static int fd = -1;
static char buf[1024];
static size_t buf_len;
static struct finder_state state;

static void socket_path(char *out, size_t size) {
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	const char *display = getenv("WAYLAND_DISPLAY");
	snprintf(out, size, "%s/platinum-finder.%s.sock",
		runtime ? runtime : "/tmp", display ? display : "wayland-0");
}

bool finder_connect(void) {
	if (fd >= 0) {
		return true;
	}
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	socket_path(addr.sun_path, sizeof(addr.sun_path));
	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd < 0) {
		return false;
	}
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
		fd = -1;
		return false;
	}
	buf_len = 0;
	return true;
}

int finder_fd(void) {
	return fd;
}

const struct finder_state *finder_state(void) {
	return &state;
}

static void disconnect(void) {
	if (fd >= 0) {
		close(fd);
	}
	fd = -1;
	memset(&state, 0, sizeof(state));
}

static bool parse_line(const char *line) {
	struct finder_state s = { .connected = true };
	if (sscanf(line, "state selection=%d window=%d trash=%d",
			&s.selection, &s.window, &s.trash) != 3) {
		return false;
	}
	bool changed = memcmp(&s, &state, sizeof(s)) != 0;
	state = s;
	return changed;
}

bool finder_read(void) {
	bool changed = false;
	for (;;) {
		ssize_t n = read(fd, buf + buf_len, sizeof(buf) - 1 - buf_len);
		if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
			disconnect();
			return true;
		}
		if (n < 0) {
			break;
		}
		buf_len += (size_t)n;
		buf[buf_len] = '\0';
		char *nl;
		while ((nl = strchr(buf, '\n'))) {
			*nl = '\0';
			changed |= parse_line(buf);
			size_t used = (size_t)(nl - buf) + 1;
			memmove(buf, nl + 1, buf_len - used + 1);
			buf_len -= used;
		}
		if (buf_len == sizeof(buf) - 1) {
			buf_len = 0; /* runaway line: drop it */
		}
	}
	return changed;
}

void finder_send(const char *command) {
	if (!finder_connect()) {
		return;
	}
	char line[128];
	int n = snprintf(line, sizeof(line), "cmd %s\n", command);
	if (write(fd, line, (size_t)n) < 0) {
		disconnect();
	}
}
