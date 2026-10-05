#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <wlr/util/log.h>

#include "launch.h"
#include "server.h"

struct pending_launch {
	struct wl_list link;
	void *owner;
	uint32_t cookie, pid;
	char *app_id;
	int64_t deadline;
};

static int64_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void remove_launch(struct pending_launch *launch) {
	wl_list_remove(&launch->link);
	free(launch->app_id);
	free(launch);
}

bool launch_pending(struct plat_server *server) {
	return !wl_list_empty(&server->pending_launches);
}

static int expire(void *data) {
	struct plat_server *server = data;
	struct pending_launch *launch, *tmp;
	int64_t now = now_ms();
	wl_list_for_each_safe(launch, tmp, &server->pending_launches, link) {
		if (now >= launch->deadline) {
			wlr_log(WLR_INFO, "Launch feedback timed out for %s (pid %u)",
				launch->app_id, launch->pid);
			remove_launch(launch);
		}
	}
	input_refresh_cursor(server);
	if (launch_pending(server)) {
		wl_event_source_timer_update(server->launch_timer, 100);
	}
	return 0;
}

void launch_init(struct plat_server *server) {
	wl_list_init(&server->pending_launches);
	server->launch_timer = wl_event_loop_add_timer(
		wl_display_get_event_loop(server->display), expire, server);
	if (!server->launch_timer) {
		wlr_log(WLR_ERROR, "Could not create launch feedback timer");
		exit(EXIT_FAILURE);
	}
}

bool launch_begin(struct plat_server *server, void *owner, uint32_t cookie,
		uint32_t pid, const char *app_id) {
	struct pending_launch *launch;
	wl_list_for_each(launch, &server->pending_launches, link) {
		if (launch->owner == owner && launch->cookie == cookie) {
			launch->pid = pid;
			return true;
		}
	}
	if (wl_list_length(&server->pending_launches) >= 32) {
		wlr_log(WLR_ERROR, "Too many pending application launches");
		return false;
	}
	launch = calloc(1, sizeof(*launch));
	if (!launch) {
		return false;
	}
	launch->app_id = strdup(app_id);
	if (!launch->app_id) {
		free(launch);
		return false;
	}
	launch->owner = owner;
	launch->cookie = cookie;
	launch->pid = pid;
	launch->deadline = now_ms() + 15000;
	wl_list_insert(&server->pending_launches, &launch->link);
	wl_event_source_timer_update(server->launch_timer, 100);
	input_refresh_cursor(server);
	return true;
}

void launch_update(struct plat_server *server, void *owner, uint32_t cookie, uint32_t pid) {
	struct pending_launch *launch;
	wl_list_for_each(launch, &server->pending_launches, link) {
		if (launch->owner == owner && launch->cookie == cookie) {
			launch->pid = pid;
			return;
		}
	}
}

void launch_cancel(struct plat_server *server, void *owner, uint32_t cookie) {
	struct pending_launch *launch, *tmp;
	wl_list_for_each_safe(launch, tmp, &server->pending_launches, link) {
		if (launch->owner == owner && launch->cookie == cookie) {
			remove_launch(launch);
		}
	}
	input_refresh_cursor(server);
}

void launch_cancel_owner(struct plat_server *server, void *owner) {
	struct pending_launch *launch, *tmp;
	wl_list_for_each_safe(launch, tmp, &server->pending_launches, link) {
		if (launch->owner == owner) {
			remove_launch(launch);
		}
	}
	input_refresh_cursor(server);
}

static bool same_app(const char *desktop, const char *app_id) {
	if (!app_id || !*app_id || !*desktop) {
		return false;
	}
	size_t len = strlen(desktop);
	if (len > 8 && strcmp(desktop + len - 8, ".desktop") == 0) {
		len -= 8;
	}
	return strlen(app_id) == len && strncasecmp(desktop, app_id, len) == 0;
}

static bool descendant(uint32_t pid, uint32_t ancestor) {
	if (!ancestor) {
		return false;
	}
	for (int depth = 0; pid > 1 && depth < 32; depth++) {
		if (pid == ancestor) {
			return true;
		}
		char path[64], line[4096];
		snprintf(path, sizeof(path), "/proc/%u/stat", pid);
		FILE *file = fopen(path, "r");
		if (!file) {
			return false; /* The process may have exited already. */
		}
		bool read = fgets(line, sizeof(line), file) != NULL;
		fclose(file);
		char *end = read ? strrchr(line, ')') : NULL;
		unsigned int parent;
		char state;
		if (!end || sscanf(end + 1, " %c %u", &state, &parent) != 2 || parent == pid) {
			return false;
		}
		pid = parent;
	}
	return false;
}

void launch_ready(struct plat_server *server, uint32_t pid, const char *app_id) {
	struct pending_launch *launch, *tmp;
	wl_list_for_each_safe(launch, tmp, &server->pending_launches, link) {
		if (same_app(launch->app_id, app_id) || descendant(pid, launch->pid)) {
			remove_launch(launch);
		}
	}
	input_refresh_cursor(server);
}
