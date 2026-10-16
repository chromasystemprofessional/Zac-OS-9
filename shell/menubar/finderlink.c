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
	snprintf(out, size, "%s/zacos9-finder.%s.sock",
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

static void clear_extras(bool tricks, bool commands);

static void disconnect(void) {
	if (fd >= 0) {
		close(fd);
	}
	fd = -1;
	memset(&state, 0, sizeof(state));
	clear_extras(true, true);
}

static struct finder_extras extras;

const struct finder_extras *finder_extras(void) {
	return &extras;
}

static void clear_list(char **list, int *n) {
	for (int i = 0; i < *n; i++) {
		free(list[i]);
	}
	*n = 0;
}

static void clear_extras(bool tricks, bool commands) {
	if (tricks) {
		extras.can_trick = false;
		clear_list(extras.attached, &extras.n_attached);
		clear_list(extras.available, &extras.n_available);
	}
	if (commands) {
		clear_list(extras.commands, &extras.n_commands);
	}
}

/* "tricks 1\t+attached\t*available..." and "commands\tname...". */
static void parse_extras(const char *line) {
	const bool tricks = strncmp(line, "tricks ", 7) == 0;
	clear_extras(tricks, !tricks);
	char *copy = strdup(line);
	char *save = NULL;
	char *field = strtok_r(copy, "\t", &save);
	if (tricks) {
		extras.can_trick = strcmp(field, "tricks 1") == 0;
	}
	while ((field = strtok_r(NULL, "\t", &save))) {
		if (!tricks && extras.n_commands < FINDER_EXTRAS) {
			extras.commands[extras.n_commands++] = strdup(field);
		} else if (tricks && field[0] == '+' && field[1] && extras.n_attached < FINDER_EXTRAS) {
			extras.attached[extras.n_attached++] = strdup(field + 1);
		} else if (tricks && field[0] == '*' && field[1] && extras.n_available < FINDER_EXTRAS) {
			extras.available[extras.n_available++] = strdup(field + 1);
		}
	}
	free(copy);
}

static bool parse_line(const char *line) {
	if (strcmp(line, "logout") == 0) {
		logout_begin();
		return false;
	}
	if (strncmp(line, "tricks ", 7) == 0 || strcmp(line, "commands") == 0 ||
			strncmp(line, "commands\t", 9) == 0) {
		parse_extras(line);
		return false;
	}
	if (strncmp(line, "context ", 8) == 0) {
		menubar_context_menu(line + 8);
		return false;
	}
	if (strncmp(line, "pathmenu ", 9) == 0) {
		menubar_path_menu(line + 9);
		return false;
	}
	struct finder_state s = { .connected = true, .label = -1 };
	if (sscanf(line, "state selection=%d window=%d trash=%d view=%d label=%d erase=%d network=%d unmount=%d sort=%d columns=%u custom=%d popup=%d popups=%d documents=%d",
			&s.selection, &s.window, &s.trash, &s.view, &s.label, &s.erase,
			&s.network, &s.unmount, &s.sort, &s.columns, &s.custom, &s.popup, &s.popups,
			&s.documents) < 3) {
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
	/* Paths (open-path) can be long: the line is as long as it needs. */
	const size_t size = strlen(command) + 6;
	char *line = malloc(size);
	const int n = snprintf(line, size, "cmd %s\n", command);
	if (write(fd, line, (size_t)n) < 0) {
		disconnect();
	}
	free(line);
}
