/*
 * platinum-shell-v1: window hints from Platinum's own programs (frame
 * style, remembered position) and window-position reports back to them.
 */
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "platinum-shell-v1-protocol.h"
#include "launch.h"
#include "server.h"

struct surface_hint {
	struct wl_list link;
	struct wlr_surface *surface;
	int style;           /* enum decor_style, or -1 */
	bool has_position;
	int x, y;
	bool reported;       /* last reported position, to avoid repeats */
	int rx, ry;
	struct wl_listener surface_destroy;
};

static struct wl_list hints;     /* surface_hint.link */
static struct wl_list resources; /* bound platinum_shell_v1 resources */
static struct plat_server *the_server;

static struct surface_hint *find_hint(struct wlr_surface *surface) {
	struct surface_hint *h;
	wl_list_for_each(h, &hints, link) {
		if (h->surface == surface) {
			return h;
		}
	}
	return NULL;
}

static void hint_surface_destroy(struct wl_listener *listener, void *data) {
	struct surface_hint *h = wl_container_of(listener, h, surface_destroy);
	wl_list_remove(&h->surface_destroy.link);
	wl_list_remove(&h->link);
	free(h);
}

static struct surface_hint *get_hint(struct wlr_surface *surface) {
	struct surface_hint *h = find_hint(surface);
	if (!h) {
		h = calloc(1, sizeof(*h));
		h->surface = surface;
		h->style = -1;
		h->surface_destroy.notify = hint_surface_destroy;
		wl_signal_add(&surface->events.destroy, &h->surface_destroy);
		wl_list_insert(&hints, &h->link);
	}
	return h;
}

int platinum_shell_style_for(struct wlr_surface *surface) {
	struct surface_hint *h = find_hint(surface);
	return h ? h->style : -1;
}

bool platinum_shell_position_for(struct wlr_surface *surface, int *x, int *y) {
	struct surface_hint *h = find_hint(surface);
	if (!h || !h->has_position) {
		return false;
	}
	*x = h->x;
	*y = h->y;
	return true;
}

void platinum_shell_report_position(struct plat_view *view) {
	struct wlr_surface *surface = view->impl->get_surface(view);
	if (!surface || !surface->resource || !view->mapped) {
		return;
	}
	struct wl_client *client = wl_resource_get_client(surface->resource);
	struct wl_resource *shell = NULL, *r;
	wl_resource_for_each(r, &resources) {
		if (wl_resource_get_client(r) == client) {
			shell = r;
		}
	}
	if (!shell) {
		return; /* not one of ours */
	}
	struct wlr_box box = view_frame_box(view);
	struct surface_hint *h = get_hint(surface);
	if (h->reported && h->rx == box.x && h->ry == box.y) {
		return;
	}
	h->reported = true;
	h->rx = box.x;
	h->ry = box.y;
	platinum_shell_v1_send_window_position(shell, surface->resource, box.x, box.y);
}

static struct plat_view *mapped_view_for(struct wlr_surface *surface) {
	struct plat_view *view;
	wl_list_for_each(view, &the_server->views, link) {
		if (view->impl->get_surface(view) == surface) {
			return view;
		}
	}
	return NULL;
}

static void handle_set_window_style(struct wl_client *client, struct wl_resource *resource,
		struct wl_resource *surface_resource, uint32_t style) {
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);
	get_hint(surface)->style = style == PLATINUM_SHELL_V1_STYLE_MOVABLE_MODAL
		? DECOR_STYLE_MOVABLE_MODAL : DECOR_STYLE_DOCUMENT;
	/* Already on screen: reframe now. */
	struct plat_view *view = mapped_view_for(surface);
	if (view) {
		view_update_frame(view);
	}
}

static void handle_set_window_position(struct wl_client *client, struct wl_resource *resource,
		struct wl_resource *surface_resource, int32_t x, int32_t y) {
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);
	struct surface_hint *h = get_hint(surface);
	h->has_position = true;
	h->x = x;
	h->y = y;
	struct plat_view *view = mapped_view_for(surface);
	if (view) {
		view_move_to(view, x, y);
	}
}

/* The process behind the front window, for the menu bar's global menus. */
static uint32_t active_pid;

uint32_t view_pid(struct plat_view *view) {
	if (view->type == PLAT_VIEW_XWAYLAND) {
		return view->xsurface->pid > 0 ? (uint32_t)view->xsurface->pid : 0;
	}
	struct wlr_surface *surface = view->impl->get_surface(view);
	if (!surface || !surface->resource) {
		return 0;
	}
	pid_t pid = 0;
	wl_client_get_credentials(wl_resource_get_client(surface->resource), &pid, NULL, NULL);
	return pid > 0 ? (uint32_t)pid : 0;
}

void platinum_shell_launch_ready(struct plat_view *view) {
	launch_ready(view->server, view_pid(view), view->app_id);
}

static void handle_begin_launch(struct wl_client *client, struct wl_resource *resource,
		uint32_t cookie, uint32_t pid, const char *app_id) {
	if (!launch_begin(the_server, resource, cookie, pid, app_id)) {
		wl_client_post_implementation_error(client, "Cannot track application launch");
		return;
	}
}

static void handle_update_launch(struct wl_client *client, struct wl_resource *resource,
		uint32_t cookie, uint32_t pid) {
	launch_update(the_server, resource, cookie, pid);
	/* The launched client may have mapped before the PID update arrived. */
	if (pid) {
		struct plat_view *view;
		wl_list_for_each(view, &the_server->views, link) {
			if (view->mapped) {
				platinum_shell_launch_ready(view);
			}
		}
	}
}

static void handle_cancel_launch(struct wl_client *client, struct wl_resource *resource,
		uint32_t cookie) {
	launch_cancel(the_server, resource, cookie);
}

static void send_active(struct wl_resource *resource) {
	if (wl_resource_get_version(resource) >= PLATINUM_SHELL_V1_ACTIVE_CLIENT_SINCE_VERSION) {
		platinum_shell_v1_send_active_client(resource, active_pid);
	}
	gtk_shell_send_active(resource, the_server);
}

void platinum_shell_gtk_changed(struct plat_server *server) {
	struct wl_resource *resource;
	wl_resource_for_each(resource, &resources) {
		gtk_shell_send_active(resource, server);
	}
}

void platinum_shell_focus_changed(struct plat_server *server) {
	struct plat_view *view = server->focused_view;
	uint32_t pid = view ? view_pid(view) : 0;
	if (pid == active_pid) {
		platinum_shell_gtk_changed(server);
		return;
	}
	active_pid = pid;
	struct wl_resource *r;
	wl_resource_for_each(r, &resources) {
		send_active(r);
	}
}

static void handle_destroy(struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void handle_keep_awake(struct wl_client *client, struct wl_resource *resource,
		uint32_t id, uint32_t pid, const char *app_id, uint32_t what) {
	idle_keep_awake(the_server, client, id, pid, app_id, what);
}

static void handle_user_activity(struct wl_client *client, struct wl_resource *resource) {
	idle_input(the_server, IDLE_INPUT_MOTION);
}

static void handle_lock(struct wl_client *client, struct wl_resource *resource, uint32_t callback,
		uint32_t always) {
	lock_request(the_server, client, callback, always != 0);
}

static void handle_force_quit(struct wl_client *client, struct wl_resource *resource,
		const char *app_id) {
	if (!app_id || !*app_id) {
		return;
	}
	/* Each process once, however many windows it has; never ourselves. */
	uint32_t killed[64];
	size_t n = 0;
	struct plat_view *view;
	wl_list_for_each(view, &the_server->views, link) {
		if (!view->app_id || strcmp(view->app_id, app_id) != 0) {
			continue;
		}
		const uint32_t pid = view_pid(view);
		bool seen = pid <= 1 || pid == (uint32_t)getpid();
		for (size_t i = 0; i < n && !seen; i++) {
			seen = killed[i] == pid;
		}
		if (!seen && n < sizeof(killed) / sizeof(killed[0])) {
			wlr_log(WLR_INFO, "force quit: %s (process %u)", app_id, pid);
			kill((pid_t)pid, SIGKILL);
			killed[n++] = pid;
		}
	}
}

static void handle_activate_app(struct wl_client *client, struct wl_resource *resource,
		const char *app_id) {
	if (app_id && *app_id) {
		view_activate_app(the_server, app_id);
	}
}

/* ⌘-Tab: the menu bar (the one client that knows the programs' names)
 * picks the program and answers with activate_app. */
void platinum_shell_switch_app(struct plat_server *server, int step) {
	struct wl_resource *resource;
	wl_resource_for_each(resource, &resources) {
		if (wl_resource_get_version(resource) >= PLATINUM_SHELL_V1_SWITCH_APP_SINCE_VERSION) {
			platinum_shell_v1_send_switch_app(resource, step);
		}
	}
}

static const struct platinum_shell_v1_interface shell_impl = {
	.set_window_style = handle_set_window_style,
	.set_window_position = handle_set_window_position,
	.destroy = handle_destroy,
	.begin_launch = handle_begin_launch,
	.update_launch = handle_update_launch,
	.cancel_launch = handle_cancel_launch,
	.keep_awake = handle_keep_awake,
	.user_activity = handle_user_activity,
	.lock = handle_lock,
	.force_quit = handle_force_quit,
	.activate_app = handle_activate_app,
};

static void resource_destroy(struct wl_resource *resource) {
	launch_cancel_owner(the_server, resource);
	wl_list_remove(wl_resource_get_link(resource));
}

static void bind_shell(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
	struct wl_resource *resource =
		wl_resource_create(client, &platinum_shell_v1_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &shell_impl, NULL, resource_destroy);
	wl_list_insert(&resources, wl_resource_get_link(resource));
	send_active(resource);
}

void platinum_shell_init(struct plat_server *server) {
	the_server = server;
	wl_list_init(&hints);
	wl_list_init(&resources);
	wl_global_create(server->display, &platinum_shell_v1_interface, 8, NULL, bind_shell);
}
