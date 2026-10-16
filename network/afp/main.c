/*
 * zacos9-afp: connect to Apple file servers the way a classic Mac does
 * (AFP 2.1/2.2 over TCP/IP, or over AppleTalk), for the Network Browser.
 *
 *   zacos9-afp appletalk-servers
 *       the AFP servers on this computer's AppleTalk network (NBP), one
 *       "NAME<TAB>NET.NODE:SOCKET" line each; nothing if there are none,
 *       exit status 3 if this computer has no AppleTalk address (atalkd
 *       isn't running)
 *   zacos9-afp info SERVER
 *       what the Chooser shows before logging in: tab-separated
 *       name / machine / version / uam lines, and "guest yes" if guests
 *       may connect
 *   zacos9-afp volumes [--user NAME] [--cleartext] SERVER
 *       logs in (the password is the first line on stdin) and lists the
 *       volumes, one per line ("<name>\tpassword" if one has its own)
 *   zacos9-afp mount [--user NAME] [--cleartext] [--foreground] SERVER VOLUME DIR
 *       logs in and serves the volume at DIR (FUSE) until it is unmounted
 *       (fusermount3 -u DIR)
 *
 * SERVER is a host name or address, with ":PORT" if not 548, or
 * "appletalk:NAME" (Mac OS 8.6 and earlier share only over AppleTalk),
 * looked up by name each time: a Mac's AppleTalk address can change. Without
 * --user, as a guest. --cleartext allows sending the password as is, for
 * servers that know nothing better. On failure, a sentence for the person
 * goes to stderr; the exit status is 2 when logging in was refused.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>

#include "afp.h"
#include "afpfs.h"
#include "asp.h"

static void usage(void) {
	fprintf(stderr, "usage: zacos9-afp appletalk-servers\n"
		"       zacos9-afp info SERVER\n"
		"       zacos9-afp volumes [--user NAME] [--cleartext] SERVER\n"
		"       zacos9-afp mount [--user NAME] [--cleartext] [--foreground] SERVER VOLUME DIR\n");
	exit(64);
}

/* "host", "host:port", "[v6]:port", "appletalk:NAME" (all of it the host) */
static void parse_server(const char *s, char *host, size_t hostlen, char *port, size_t portlen) {
	const char *colon = NULL;
	if (strncmp(s, "appletalk:", 10) == 0) {
		snprintf(host, hostlen, "%s", s);
		snprintf(port, portlen, "%s", "");
		return;
	}
	if (s[0] == '[') {
		const char *end = strchr(s, ']');
		if (end) {
			snprintf(host, hostlen, "%.*s", (int)(end - s - 1), s + 1);
			colon = end[1] == ':' ? end + 1 : NULL;
		} else {
			snprintf(host, hostlen, "%s", s);
		}
	} else {
		colon = strchr(s, ':');
		if (colon && strchr(colon + 1, ':')) {
			colon = NULL; /* a bare IPv6 address */
		}
		snprintf(host, hostlen, "%.*s", colon ? (int)(colon - s) : (int)strlen(s), s);
	}
	snprintf(port, portlen, "%s", colon ? colon + 1 : "548");
}

static int appletalk_servers(void) {
	struct nbp_entry e[64];
	int n;
	int r = nbp_lookup("=", "AFPServer", e, 64, &n, 1500);
	if (r < 0) {
		fprintf(stderr, "AppleTalk isn’t on here (%s).\n", strerror(-r));
		return 3;
	}
	for (int i = 0; i < n; i++) {
		printf("%s\t%u.%u:%u\n", e[i].name, ntohs(e[i].addr.sat_addr.s_net), e[i].addr.sat_addr.s_node,
			e[i].addr.sat_port);
	}
	return 0;
}

static int info(const char *server) {
	char host[256], port[16];
	parse_server(server, host, sizeof(host), port, sizeof(port));
	struct afp_server_info si;
	int r = afp_server_info(host, port, &si);
	if (r < 0) {
		const char *shown = strncmp(server, "appletalk:", 10) == 0 ? server + 10 : server;
		fprintf(stderr, r == -EPROTO ? "“%s” isn’t a file server.\n"
			: r == -ENETDOWN ? "AppleTalk isn’t on here, so “%s” can’t be reached.\n"
			                 : "The server “%s” could not be reached. It may not be on the network.\n",
			shown);
		return 1;
	}
	printf("name\t%s\nmachine\t%s\n", si.name, si.machine);
	int guest = 0;
	for (int i = 0; i < si.nversions; i++) {
		printf("version\t%s\n", si.versions[i]);
	}
	for (int i = 0; i < si.nuams; i++) {
		printf("uam\t%s\n", si.uams[i]);
		guest |= strcasecmp(si.uams[i], "No User Authent") == 0;
	}
	printf("guest\t%s\n", guest ? "yes" : "no");
	return 0;
}

int main(int argc, char **argv) {
	if (argc == 2 && strcmp(argv[1], "appletalk-servers") == 0) {
		return appletalk_servers();
	}
	if (argc < 3) {
		usage();
	}
	const char *cmd = argv[1];
	if (strcmp(cmd, "info") == 0) {
		return info(argv[2]);
	}
	const char *user = NULL;
	bool cleartext = false, foreground = false;
	int i = 2;
	for (; i < argc && strncmp(argv[i], "--", 2) == 0; i++) {
		if (strcmp(argv[i], "--user") == 0 && i + 1 < argc) {
			user = argv[++i];
		} else if (strcmp(argv[i], "--cleartext") == 0) {
			cleartext = true;
		} else if (strcmp(argv[i], "--foreground") == 0) {
			foreground = true;
		} else {
			usage();
		}
	}
	const bool mount = strcmp(cmd, "mount") == 0;
	if ((!mount && strcmp(cmd, "volumes") != 0) || argc - i != (mount ? 3 : 1)) {
		usage();
	}
	char host[256], port[16];
	parse_server(argv[i], host, sizeof(host), port, sizeof(port));

	/* The password: never on the command line. */
	char password[256] = "";
	if (user) {
		if (!fgets(password, sizeof(password), stdin)) {
			password[0] = 0;
		}
		password[strcspn(password, "\r\n")] = 0;
	}
	struct afp a;
	char err[300];
	int r = afp_connect(&a, host, port, user, password, cleartext, err, sizeof(err));
	memset(password, 0, sizeof(password));
	if (r < 0) {
		fprintf(stderr, "%s\n", err);
		return r == -EACCES ? 2 : 1;
	}
	if (!mount) {
		struct afp_volume vols[64];
		int n;
		r = afp_volumes(&a, vols, 64, &n);
		for (int k = 0; r == 0 && k < n; k++) {
			printf("%s%s\n", vols[k].name, vols[k].has_password ? "\tpassword" : "");
		}
		afp_disconnect(&a);
		if (r < 0) {
			fprintf(stderr, "The server’s volumes could not be listed (%s).\n", strerror(-r));
		}
		return r < 0;
	}
	const char *volume = argv[i + 1], *dir = argv[i + 2];
	if ((r = afp_open_volume(&a, volume)) < 0) {
		fprintf(stderr, r == -EACCES ? "You don’t have access to “%s”.\n"
			: "The volume “%s” could not be opened.\n", volume);
		afp_disconnect(&a);
		return r == -EACCES ? 2 : 1;
	}
	/* The volume answers before it is handed to the system. */
	struct afp_entry root;
	if ((r = afp_stat(&a, AFP_ROOT_DIR, "", &root)) < 0) {
		fprintf(stderr, "The volume “%s” isn’t answering.\n", volume);
		afp_disconnect(&a);
		return 1;
	}
	char source[300];
	snprintf(source, sizeof(source), "afp://%s/%s", argv[i], volume);
	return afpfs_run(&a, dir, source, foreground) == 0 ? 0 : 1;
}
