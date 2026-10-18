/*
 * zacos9-finder-ctl: script the Sniffer (Mac OS 9's scriptable Finder,
 * gap plan 6.4) over its socket, $XDG_RUNTIME_DIR/zacos9-finder.DISPLAY.sock,
 * where it takes "cmd NAME" lines (shell/finder/finder.cpp, Finder::command).
 */
#define _DEFAULT_SOURCE
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "icons.h"
#include "settings.h"

static const char usage[] =
	"usage: zacos9-finder-ctl COMMAND ...\n"
	"\n"
	"  open PATH...            open files, folders or programs, as a double-click would\n"
	"  reveal PATH             open the folder PATH is in, with PATH selected\n"
	"  label LABEL PATH...     label items: 0 or none, 1 to 7, or a label's name\n"
	"                          (Sniffer Preferences > Labels)\n"
	"  comment PATH [TEXT]     set an item's Get Info comment; no TEXT removes it\n"
	"  command NAME            a menu command, as the menu bar sends it, for the\n"
	"                          front window's selection: new-folder, empty-trash,\n"
	"                          clean-up, view-list, select-all, find, ...\n"
	"\n"
	"The Sniffer must be running in this session (WAYLAND_DISPLAY).\n";

static int sock = -1;

static void connect_sniffer(void) {
	const char *runtime = getenv("XDG_RUNTIME_DIR"), *display = getenv("WAYLAND_DISPLAY");
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	int n = snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/zacos9-finder.%s.sock",
		runtime && *runtime ? runtime : "/tmp", display && *display ? display : "wayland-0");
	sock = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (n >= (int)sizeof(addr.sun_path) || sock < 0 ||
			connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "zacos9-finder-ctl: the Sniffer isn't running (%s)\n", addr.sun_path);
		exit(1);
	}
}

static void send_line(const char *line) {
	size_t len = strlen(line);
	while (len > 0) {
		ssize_t w = write(sock, line, len);
		if (w < 0 && errno == EINTR) {
			continue;
		}
		if (w <= 0) {
			perror("zacos9-finder-ctl");
			exit(1);
		}
		line += w;
		len -= (size_t)w;
	}
}

/* "cmd NAME[ ARG][\tARG2]\n": a tab or newline in an argument would end it. */
static void command(const char *name, const char *arg, const char *arg2) {
	for (const char *a = arg; a && *a; a++) {
		if (*a == '\n' || *a == '\t') {
			fprintf(stderr, "zacos9-finder-ctl: no tabs or line breaks in \"%s\"\n", arg);
			exit(2);
		}
	}
	for (const char *a = arg2; a && *a; a++) {
		if (*a == '\n') {
			fprintf(stderr, "zacos9-finder-ctl: no line breaks in \"%s\"\n", arg2);
			exit(2);
		}
	}
	char *line;
	if (asprintf(&line, "cmd %s%s%s%s%s\n", name, arg ? " " : "", arg ? arg : "",
			arg2 ? "\t" : "", arg2 ? arg2 : "") < 0) {
		exit(1);
	}
	send_line(line);
	free(line);
}

/* The absolute path, the item itself if it's a link. */
static char *absolute(const char *path) {
	if (path[0] == '/') {
		return strdup(path);
	}
	char cwd[PATH_MAX];
	if (!getcwd(cwd, sizeof(cwd))) {
		perror("zacos9-finder-ctl");
		exit(1);
	}
	char *out;
	if (asprintf(&out, "%s/%s", cwd, path) < 0) {
		exit(1);
	}
	return out;
}

static int label_number(const char *s) {
	char *end;
	long n = strtol(s, &end, 10);
	if (*s && !*end) {
		return n >= 0 && n < PL_LABEL_COUNT ? (int)n : -1;
	}
	if (strcasecmp(s, "none") == 0) {
		return 0;
	}
	for (int i = 1; i < PL_LABEL_COUNT; i++) {
		if (strcasecmp(s, pl_label_name(i)) == 0) {
			return i;
		}
	}
	return -1;
}

int main(int argc, char **argv) {
	if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
		fputs(usage, argc < 2 ? stderr : stdout);
		return argc < 2 ? 2 : 0;
	}
	const char *what = argv[1];
	if (strcmp(what, "open") == 0 && argc >= 3) {
		connect_sniffer();
		for (int i = 2; i < argc; i++) {
			char *p = absolute(argv[i]);
			command("open-path", p, NULL);
			free(p);
		}
	} else if (strcmp(what, "reveal") == 0 && argc == 3) {
		connect_sniffer();
		char *p = absolute(argv[2]);
		command("reveal-path", p, NULL);
		free(p);
	} else if (strcmp(what, "label") == 0 && argc >= 4) {
		const int label = label_number(argv[2]);
		if (label < 0) {
			fprintf(stderr, "zacos9-finder-ctl: no label \"%s\"\n", argv[2]);
			return 2;
		}
		connect_sniffer();
		char n[16];
		snprintf(n, sizeof(n), "%d", label);
		for (int i = 3; i < argc; i++) {
			char *p = absolute(argv[i]);
			command("label-path", n, p);
			free(p);
		}
	} else if (strcmp(what, "comment") == 0 && (argc == 3 || argc == 4)) {
		connect_sniffer();
		char *p = absolute(argv[2]);
		command("comment-path", p, argc == 4 ? argv[3] : "");
		free(p);
	} else if (strcmp(what, "command") == 0 && argc == 3) {
		connect_sniffer();
		command(argv[2], NULL, NULL);
	} else {
		fputs(usage, stderr);
		return 2;
	}
	close(sock);
	return 0;
}
