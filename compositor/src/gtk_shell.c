#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wlr/types/wlr_compositor.h>
#include "gtk-shell-protocol.h"
#include "platinum-shell-v1-protocol.h"
#include "server.h"
#include "settings.h"

struct gtk_surface {
	struct wl_list link;
	struct wl_resource *resource;
	struct wlr_surface *surface;
	struct wl_listener destroy;
	char *bus, *app_menu, *menubar, *window, *application;
};

static struct wl_list surfaces;
static struct plat_server *the_server;

static void free_properties(struct gtk_surface *surface) {
	free(surface->bus);
	free(surface->app_menu);
	free(surface->menubar);
	free(surface->window);
	free(surface->application);
}

static void surface_resource_destroy(struct wl_resource *resource) {
	struct gtk_surface *surface = wl_resource_get_user_data(resource);
	if (!surface) {
		return;
	}
	if (surface->surface) {
		wl_list_remove(&surface->destroy.link);
	}
	wl_list_remove(&surface->link);
	free_properties(surface);
	free(surface);
	platinum_shell_gtk_changed(the_server);
}

static void surface_destroy(struct wl_listener *listener, void *data) {
	struct gtk_surface *surface = wl_container_of(listener, surface, destroy);
	wl_list_remove(&surface->destroy.link);
	surface->surface = NULL;
	free_properties(surface);
	surface->bus = surface->app_menu = surface->menubar = surface->window = surface->application = NULL;
	platinum_shell_gtk_changed(the_server);
}

static bool bounded(const char *text) {
	return !text || strlen(text) <= 1024;
}

static void set_dbus_properties(struct wl_client *client, struct wl_resource *resource,
		const char *app_id, const char *app_menu, const char *menubar,
		const char *window, const char *application, const char *bus) {
	struct gtk_surface *surface = wl_resource_get_user_data(resource);
	if (!surface->surface) {
		return;
	}
	if (!bounded(app_id) || !bounded(app_menu) || !bounded(menubar) ||
			!bounded(window) || !bounded(application) || !bounded(bus)) {
		wl_client_post_implementation_error(client, "GTK menu properties exceed size limit");
		return;
	}
	char *new_bus = strdup(bus ? bus : ""), *new_app_menu = strdup(app_menu ? app_menu : ""),
		*new_menubar = strdup(menubar ? menubar : ""), *new_window = strdup(window ? window : ""),
		*new_application = strdup(application ? application : "");
	if (!new_bus || !new_app_menu || !new_menubar || !new_window || !new_application) {
		free(new_bus);
		free(new_app_menu);
		free(new_menubar);
		free(new_window);
		free(new_application);
		wl_client_post_no_memory(client);
		return;
	}
	free_properties(surface);
	surface->bus = new_bus;
	surface->app_menu = new_app_menu;
	surface->menubar = new_menubar;
	surface->window = new_window;
	surface->application = new_application;
	platinum_shell_gtk_changed(the_server);
}

static void modal(struct wl_client *client, struct wl_resource *resource) {
	/* GTK's xdg-toplevel parent relationship already supplies modality. */
}

static void present(struct wl_client *client, struct wl_resource *resource, uint32_t time) {
	/* Do not let an arbitrary GTK present request steal keyboard focus. */
}

static const struct gtk_surface1_interface surface_impl = {
	.set_dbus_properties = set_dbus_properties,
	.set_modal = modal,
	.unset_modal = modal,
	.present = present,
};

static void get_surface(struct wl_client *client, struct wl_resource *resource,
		uint32_t id, struct wl_resource *wl_surface) {
	struct wlr_surface *wlr_surface = wlr_surface_from_resource(wl_surface);
	struct gtk_surface *existing;
	wl_list_for_each(existing, &surfaces, link) {
		if (existing->surface == wlr_surface) {
			wl_client_post_implementation_error(client, "GTK surface already exists");
			return;
		}
	}
	struct gtk_surface *surface = calloc(1, sizeof(*surface));
	if (!surface) {
		wl_client_post_no_memory(client);
		return;
	}
	surface->resource = wl_resource_create(client, &gtk_surface1_interface, 1, id);
	if (!surface->resource) {
		free(surface);
		wl_client_post_no_memory(client);
		return;
	}
	surface->surface = wlr_surface;
	wl_resource_set_implementation(surface->resource, &surface_impl, surface, surface_resource_destroy);
	surface->destroy.notify = surface_destroy;
	wl_signal_add(&wlr_surface->events.destroy, &surface->destroy);
	wl_list_insert(&surfaces, &surface->link);
}

static void startup_id(struct wl_client *client, struct wl_resource *resource, const char *id) {
}

static void bell(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface) {
	pl_beep();
}

static const struct gtk_shell1_interface shell_impl = {
	.get_gtk_surface = get_surface,
	.set_startup_id = startup_id,
	.system_bell = bell,
};

static void bind_shell(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
	struct wl_resource *resource = wl_resource_create(client, &gtk_shell1_interface, 1, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &shell_impl, NULL, NULL);
	gtk_shell1_send_capabilities(resource, GTK_SHELL1_CAPABILITY_GLOBAL_APP_MENU |
		GTK_SHELL1_CAPABILITY_GLOBAL_MENU_BAR);
}

void gtk_shell_send_active(struct wl_resource *resource, struct plat_server *server) {
	if (wl_resource_get_version(resource) < PLATINUM_SHELL_V1_ACTIVE_GTK_MENU_SINCE_VERSION) {
		return;
	}
	struct wlr_surface *focused = server->focused_view ?
		server->focused_view->impl->get_surface(server->focused_view) : NULL;
	struct gtk_surface *surface;
	wl_list_for_each(surface, &surfaces, link) {
		if (surface->surface && surface->surface == focused && surface->bus) {
			platinum_shell_v1_send_active_gtk_menu(resource, surface->bus,
				surface->app_menu, surface->menubar, surface->window, surface->application);
			return;
		}
	}
	platinum_shell_v1_send_active_gtk_menu(resource, "", "", "", "", "");
}

void gtk_shell_init(struct plat_server *server) {
	the_server = server;
	wl_list_init(&surfaces);
	if (!wl_global_create(server->display, &gtk_shell1_interface, 1, NULL, bind_shell)) {
		fprintf(stderr, "zacos9-wm: could not create GTK menu protocol\n");
	}
}
