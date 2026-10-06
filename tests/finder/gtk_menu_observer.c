#include <stdio.h>
#include <string.h>
#include <wayland-client.h>
#include <gio/gio.h>
#include <poll.h>
#include <stdlib.h>
#include "menubar.h"
#include "platinum-shell-v1-client-protocol.h"

static struct platinum_shell_v1 *shell;
static uint32_t pid;

struct mb_menu *menus_new_menu(struct mb_menu *menus, int *n, const char *title) {
	struct mb_menu *menu = &menus[(*n)++];
	menu->title = g_strdup(title);
	return menu;
}

struct mb_item *menus_add_item(struct mb_menu *menu, const char *label, char key,
		bool enabled, enum action action, const char *arg) {
	if (menu->n == MAX_ITEMS) {
		return NULL;
	}
	struct mb_item *item = &menu->items[menu->n++];
	item->label = g_strdup(label);
	item->key = key;
	item->enabled = enabled;
	item->action = action;
	item->arg = g_strdup(arg);
	return item;
}

static void print_menu(struct mb_menu *menu) {
	if (menu->title) {
		printf("MENU %u\t%s\n", pid, menu->title);
	}
	for (int i = 0; i < menu->n; i++) {
		struct mb_item *item = &menu->items[i];
		if (item->arg && item->enabled) {
			printf("ITEM %s\t%s\n", item->label ? item->label : "", item->arg);
		}
		if (item->submenu) {
			print_menu(item->submenu);
			g_free(item->submenu);
		}
		g_free(item->label);
		g_free(item->arg);
	}
	g_free(menu->title);
}

void menubar_apps_changed(void) {
	struct mb_menu menus[MAX_TITLES] = {0};
	int n = 0;
	if (appmenu_add_menus(menus, &n, MAX_TITLES)) {
		for (int i = 0; i < n; i++) {
			print_menu(&menus[i]);
		}
	}
	fflush(stdout);
}

static void position(void *data, struct platinum_shell_v1 *shell,
		struct wl_surface *surface, int32_t x, int32_t y) {}

static void active(void *data, struct platinum_shell_v1 *shell, uint32_t process) {
	pid = process;
	appmenu_set_active_pid(process);
}

static void menu(void *data, struct platinum_shell_v1 *shell, const char *bus,
		const char *app_menu, const char *menubar, const char *window, const char *application) {
	GVariant *metadata = g_variant_ref_sink(g_variant_new("(usssss)", pid, bus,
		app_menu, menubar, window, application));
	char *text = g_variant_print(metadata, TRUE);
	puts(text);
	fflush(stdout);
	g_free(text);
	g_variant_unref(metadata);
	appmenu_set_active_gtk(bus, app_menu, menubar, window, application);
}

static const struct platinum_shell_v1_listener listener = {
	.window_position = position,
	.active_client = active,
	.active_gtk_menu = menu,
};

static void global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version) {
	if (!strcmp(interface, platinum_shell_v1_interface.name) && version >= 4) {
		shell = wl_registry_bind(registry, name, &platinum_shell_v1_interface, 4);
		platinum_shell_v1_add_listener(shell, &listener, NULL);
	}
}

static void removed(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = {
	.global = global, .global_remove = removed,
};

int main(void) {
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "observer: could not connect to Wayland\n");
		return 1;
	}
	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	appmenu_init();
	if (wl_display_roundtrip(display) < 0 || !shell) {
		fprintf(stderr, "observer: missing v4 platinum shell\n");
		return 1;
	}
	for (;;) {
		if (wl_display_dispatch_pending(display) < 0 || wl_display_flush(display) < 0) {
			break;
		}
		struct pollfd fds[18] = {
			{.fd = wl_display_get_fd(display), .events = POLLIN},
			{.fd = 0, .events = POLLIN},
		};
		int timeout = 100;
		int count = appmenu_poll_prepare(fds + 2, 16, &timeout);
		if (poll(fds, count + 2, timeout) < 0) {
			break;
		}
		appmenu_poll_dispatch(fds + 2);
		if (fds[0].revents & POLLIN) {
			if (wl_display_dispatch(display) < 0) {
				break;
			}
		}
		if (fds[1].revents & POLLIN) {
			char *line = NULL;
			size_t size = 0;
			if (getline(&line, &size, stdin) <= 0) {
				free(line);
				break;
			}
			line[strcspn(line, "\n")] = '\0';
			if (!strncmp(line, "click ", 6)) {
				appmenu_perform(line + 6);
			}
			free(line);
		}
	}
	wl_display_disconnect(display);
	return 0;
}
