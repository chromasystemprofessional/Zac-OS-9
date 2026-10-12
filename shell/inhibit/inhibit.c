/*
 * zacos9-inhibit: keeps screens awake for apps that ask over D-Bus rather
 * than with Wayland's idle-inhibit - most video in a browser running under
 * X11 (Chromium, Electron), VLC, and sandboxed (Flatpak) apps:
 *
 *   org.freedesktop.ScreenSaver (/org/freedesktop/ScreenSaver, /ScreenSaver):
 *     Inhibit, UnInhibit, SimulateUserActivity, GetActive, and the rest as
 *     no-ops
 *   org.freedesktop.impl.portal.Inhibit, as the portal backend
 *     "zacos9-inhibit" (share/xdg/xdg-desktop-portal/): Inhibit, with its
 *     Request's Close
 *
 * It locks the screens too (platinum_shell_v1.lock, compositor/src/lock.c):
 * for ScreenSaver's Lock and logind's Lock (loginctl lock-session), and,
 * holding a logind "delay" inhibitor, before the system sleeps (idle,
 * Special > Sleep, the lid), letting it sleep once they are locked - when
 * "Ask for my password to wake" is on.
 *
 * Each request becomes a platinum_shell_v1.keep_awake in zacos9-wm
 * (compositor/src/idle.c), with the asking process (its D-Bus connection's
 * PID) or app id, so the screens showing that app stay awake while the
 * others may sleep. A request ends when its app says so or leaves the bus,
 * and all of them when this exits. zacos9-wm starts it in a real session.
 * See docs/energy-saver.md.
 */
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <glib-unix.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>

#include "platinum-shell-v1-client-protocol.h"

#define PORTAL_NAME "org.freedesktop.impl.portal.desktop.zacos9inhibit"

/* The portal's flags: what an app asks to be inhibited. */
enum { PORTAL_SUSPEND = 4, PORTAL_IDLE = 8 };

static const char screensaver_xml[] =
	"<node><interface name='org.freedesktop.ScreenSaver'>"
	"<method name='Inhibit'><arg type='s' direction='in'/><arg type='s' direction='in'/>"
	"<arg type='u' direction='out'/></method>"
	"<method name='UnInhibit'><arg type='u' direction='in'/></method>"
	"<method name='SimulateUserActivity'/>"
	"<method name='GetActive'><arg type='b' direction='out'/></method>"
	"<method name='SetActive'><arg type='b' direction='in'/><arg type='b' direction='out'/></method>"
	"<method name='GetActiveTime'><arg type='u' direction='out'/></method>"
	"<method name='GetSessionIdleTime'><arg type='u' direction='out'/></method>"
	"<method name='Lock'/>"
	"<signal name='ActiveChanged'><arg type='b'/></signal>"
	"</interface></node>";

static const char portal_xml[] =
	"<node><interface name='org.freedesktop.impl.portal.Inhibit'>"
	"<method name='Inhibit'><arg type='o' direction='in'/><arg type='s' direction='in'/>"
	"<arg type='s' direction='in'/><arg type='u' direction='in'/><arg type='a{sv}' direction='in'/></method>"
	"<method name='CreateMonitor'><arg type='o' direction='in'/><arg type='o' direction='in'/>"
	"<arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='u' direction='out'/></method>"
	"<method name='QueryEndResponse'><arg type='o' direction='in'/></method>"
	"</interface></node>";

static const char request_xml[] =
	"<node><interface name='org.freedesktop.impl.portal.Request'>"
	"<method name='Close'/></interface></node>";

static struct wl_display *display;
static struct platinum_shell_v1 *shell;
static uint32_t shell_version;
static GDBusConnection *system_bus;
static int sleep_delay = -1; /* logind's delay inhibitor, while held */
static GDBusNodeInfo *screensaver_info, *portal_info, *request_info;
static GHashTable *requests; /* cookie -> struct request */
static guint32 last_cookie;

struct request {
	guint32 cookie;
	char *owner; /* the D-Bus connection that asked */
	guint handle_id; /* the portal's Request object, if it is one */
	struct platinum_keep_awake_v1 *keep; /* NULL if nothing to keep */
	GDBusConnection *bus;
};

static void request_free(gpointer data) {
	struct request *r = data;
	if (r->keep) {
		platinum_keep_awake_v1_destroy(r->keep);
		wl_display_flush(display);
	}
	if (r->handle_id) {
		g_dbus_connection_unregister_object(r->bus, r->handle_id);
	}
	g_free(r->owner);
	g_free(r);
}

static struct request *request_new(const char *owner, guint32 pid, const char *app_id, guint32 what) {
	struct request *r = g_new0(struct request, 1);
	if (++last_cookie == 0) {
		last_cookie = 1; /* 0 is no cookie */
	}
	r->cookie = last_cookie;
	r->owner = g_strdup(owner);
	if (what) {
		r->keep = platinum_shell_v1_keep_awake(shell, pid, app_id ? app_id : "", what);
		wl_display_flush(display);
	}
	g_hash_table_insert(requests, GUINT_TO_POINTER(r->cookie), r);
	return r;
}

/* Whether any screen shows the screen saver or is asleep (idle.c's file). */
static gboolean screen_saver_active(void) {
	const char *runtime = g_getenv("XDG_RUNTIME_DIR");
	const char *wayland = g_getenv("WAYLAND_DISPLAY");
	g_autofree char *path = g_strdup_printf("%s/zacos9-idle-%s", runtime ? runtime : "",
		wayland ? wayland : "wayland-0");
	g_autofree char *text = NULL;
	if (!g_file_get_contents(path, &text, NULL, NULL)) {
		return FALSE;
	}
	return strstr(text, " saver\n") || strstr(text, " asleep\n");
}

/* ---- locking ------------------------------------------------------------------ */

/* Once locked: answer the D-Bus call waiting, or let logind sleep. */
static void locked_done(void *data, struct wl_callback *callback, uint32_t unused) {
	if (callback) {
		wl_callback_destroy(callback);
	}
	if (data) {
		g_dbus_method_invocation_return_value(data, NULL);
	} else if (sleep_delay >= 0) {
		close(sleep_delay);
		sleep_delay = -1;
	}
}

static const struct wl_callback_listener locked_listener = { locked_done };

/* `always`: logind's or an app's request; else only with a password asked
 * (before sleep). `invocation` (may be NULL) is answered once locked; with
 * none, logind's delay is let go. */
static void lock_screens(bool always, GDBusMethodInvocation *invocation) {
	if (shell_version < PLATINUM_SHELL_V1_LOCK_SINCE_VERSION) {
		locked_done(invocation, NULL, 0);
		return;
	}
	struct wl_callback *callback = platinum_shell_v1_lock(shell, always ? 1 : 0);
	wl_callback_add_listener(callback, &locked_listener, invocation);
	wl_display_flush(display);
}

static void take_sleep_delay(void);

static void got_sleep_delay(GObject *source, GAsyncResult *result, gpointer data) {
	g_autoptr(GUnixFDList) fds = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) reply = g_dbus_connection_call_with_unix_fd_list_finish(G_DBUS_CONNECTION(source),
		&fds, result, &error);
	if (!reply || !fds) {
		g_printerr("zacos9-inhibit: no sleep delay from logind: %s\n", error ? error->message : "?");
		return;
	}
	gint32 index;
	g_variant_get(reply, "(h)", &index);
	sleep_delay = g_unix_fd_list_get(fds, index, NULL);
}

static void take_sleep_delay(void) {
	if (!system_bus || sleep_delay >= 0) {
		return;
	}
	g_dbus_connection_call_with_unix_fd_list(system_bus, "org.freedesktop.login1",
		"/org/freedesktop/login1", "org.freedesktop.login1.Manager", "Inhibit",
		g_variant_new("(ssss)", "sleep", "ZacOS 9", "Locks the screens first", "delay"),
		G_VARIANT_TYPE("(h)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, got_sleep_delay, NULL);
}

static void prepare_for_sleep(GDBusConnection *bus, const char *sender, const char *path,
		const char *iface, const char *signal, GVariant *args, gpointer data) {
	gboolean start;
	g_variant_get(args, "(b)", &start);
	if (start) {
		lock_screens(false, NULL); /* lets logind go once locked */
	} else {
		take_sleep_delay(); /* woken: ready for the next time */
	}
}

static void session_lock(GDBusConnection *bus, const char *sender, const char *path,
		const char *iface, const char *signal, GVariant *args, gpointer data) {
	lock_screens(true, NULL);
}

static void got_session(GObject *source, GAsyncResult *result, gpointer data) {
	g_autoptr(GVariant) reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, NULL);
	if (!reply) {
		return; /* not in a logind session */
	}
	const char *session;
	g_variant_get(reply, "(&o)", &session);
	g_dbus_connection_signal_subscribe(system_bus, "org.freedesktop.login1",
		"org.freedesktop.login1.Session", "Lock", session, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		session_lock, NULL, NULL);
}

static void got_system_bus(GObject *source, GAsyncResult *result, gpointer data) {
	system_bus = g_bus_get_finish(result, NULL);
	if (!system_bus) {
		return; /* no logind to hear from */
	}
	g_dbus_connection_signal_subscribe(system_bus, "org.freedesktop.login1",
		"org.freedesktop.login1.Manager", "PrepareForSleep", "/org/freedesktop/login1", NULL,
		G_DBUS_SIGNAL_FLAGS_NONE, prepare_for_sleep, NULL, NULL);
	g_dbus_connection_call(system_bus, "org.freedesktop.login1", "/org/freedesktop/login1",
		"org.freedesktop.login1.Manager", "GetSessionByPID", g_variant_new("(u)", (guint32)getpid()),
		G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, got_session, NULL);
	take_sleep_delay();
}

/* ---- org.freedesktop.ScreenSaver --------------------------------------------- */

struct pending {
	GDBusMethodInvocation *invocation;
	char *sender;
};

static void got_pid(GObject *source, GAsyncResult *result, gpointer data) {
	struct pending *p = data;
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
	if (!reply) {
		/* It left the bus before we could ask: nothing to keep awake for. */
		g_dbus_method_invocation_return_gerror(p->invocation, error);
	} else {
		guint32 pid = 0;
		g_variant_get(reply, "(u)", &pid);
		struct request *r = request_new(p->sender, pid, "",
			PLATINUM_SHELL_V1_KEEP_AWAKE_WHAT_SCREENS | PLATINUM_SHELL_V1_KEEP_AWAKE_WHAT_SYSTEM);
		g_dbus_method_invocation_return_value(p->invocation, g_variant_new("(u)", r->cookie));
	}
	g_free(p->sender);
	g_free(p);
}

static void screensaver_call(GDBusConnection *bus, const char *sender, const char *path,
		const char *iface, const char *method, GVariant *args, GDBusMethodInvocation *invocation,
		gpointer data) {
	if (strcmp(method, "Inhibit") == 0) {
		struct pending *p = g_new0(struct pending, 1);
		p->invocation = invocation;
		p->sender = g_strdup(sender);
		g_dbus_connection_call(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
			"org.freedesktop.DBus", "GetConnectionUnixProcessID", g_variant_new("(s)", sender),
			G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, got_pid, p);
	} else if (strcmp(method, "UnInhibit") == 0) {
		guint32 cookie;
		g_variant_get(args, "(u)", &cookie);
		struct request *r = g_hash_table_lookup(requests, GUINT_TO_POINTER(cookie));
		if (r && strcmp(r->owner, sender) == 0) {
			g_hash_table_remove(requests, GUINT_TO_POINTER(cookie));
		}
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (strcmp(method, "SimulateUserActivity") == 0) {
		platinum_shell_v1_user_activity(shell);
		wl_display_flush(display);
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (strcmp(method, "GetActive") == 0) {
		g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", screen_saver_active()));
	} else if (strcmp(method, "SetActive") == 0) {
		g_dbus_method_invocation_return_value(invocation, g_variant_new("(b)", FALSE));
	} else if (strcmp(method, "GetActiveTime") == 0 || strcmp(method, "GetSessionIdleTime") == 0) {
		g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", 0));
	} else if (strcmp(method, "Lock") == 0) {
		lock_screens(true, invocation);
	} else {
		g_dbus_method_invocation_return_value(invocation, NULL);
	}
}

/* ---- the portal's Inhibit ---------------------------------------------------- */

static void request_call(GDBusConnection *bus, const char *sender, const char *path,
		const char *iface, const char *method, GVariant *args, GDBusMethodInvocation *invocation,
		gpointer data) {
	/* Close: the app is done, or has gone (the portal tells us). */
	g_hash_table_remove(requests, data);
	g_dbus_method_invocation_return_value(invocation, NULL);
}

static const GDBusInterfaceVTable request_vtable = { request_call, NULL, NULL, { 0 } };

static void portal_call(GDBusConnection *bus, const char *sender, const char *path,
		const char *iface, const char *method, GVariant *args, GDBusMethodInvocation *invocation,
		gpointer data) {
	if (strcmp(method, "Inhibit") != 0) {
		g_dbus_method_invocation_return_dbus_error(invocation,
			"org.freedesktop.DBus.Error.NotSupported", "Only Inhibit is offered");
		return;
	}
	const char *handle, *app_id, *window;
	guint32 flags;
	g_variant_get(args, "(&o&s&su@a{sv})", &handle, &app_id, &window, &flags, NULL);
	guint32 what = 0;
	if (flags & PORTAL_IDLE) {
		what |= PLATINUM_SHELL_V1_KEEP_AWAKE_WHAT_SCREENS | PLATINUM_SHELL_V1_KEEP_AWAKE_WHAT_SYSTEM;
	}
	if (flags & PORTAL_SUSPEND) {
		what |= PLATINUM_SHELL_V1_KEEP_AWAKE_WHAT_SYSTEM;
	}
	/* The caller is the portal; the app is known by its app id (a Flatpak's),
	 * or not at all, which keeps every screen awake. */
	struct request *r = request_new(sender, 0, app_id, what);
	r->bus = bus;
	g_autoptr(GError) error = NULL;
	r->handle_id = g_dbus_connection_register_object(bus, handle, request_info->interfaces[0],
		&request_vtable, GUINT_TO_POINTER(r->cookie), NULL, &error);
	if (!r->handle_id) {
		g_hash_table_remove(requests, GUINT_TO_POINTER(r->cookie));
		g_dbus_method_invocation_return_gerror(invocation, error);
		return;
	}
	g_dbus_method_invocation_return_value(invocation, NULL);
}

/* ---- the bus ----------------------------------------------------------------- */

static void name_owner_changed(GDBusConnection *bus, const char *sender, const char *path,
		const char *iface, const char *signal, GVariant *args, gpointer data) {
	const char *name, *old_owner, *new_owner;
	g_variant_get(args, "(&s&s&s)", &name, &old_owner, &new_owner);
	if (name[0] != ':' || new_owner[0]) {
		return;
	}
	/* A connection left the bus: its requests go with it. */
	GHashTableIter it;
	gpointer value;
	g_hash_table_iter_init(&it, requests);
	while (g_hash_table_iter_next(&it, NULL, &value)) {
		if (strcmp(((struct request *)value)->owner, name) == 0) {
			g_hash_table_iter_remove(&it);
		}
	}
}

static const GDBusInterfaceVTable screensaver_vtable = { screensaver_call, NULL, NULL, { 0 } };
static const GDBusInterfaceVTable portal_vtable = { portal_call, NULL, NULL, { 0 } };

static void bus_acquired(GDBusConnection *bus, const char *name, gpointer data) {
	g_dbus_connection_signal_subscribe(bus, "org.freedesktop.DBus", "org.freedesktop.DBus",
		"NameOwnerChanged", "/org/freedesktop/DBus", NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		name_owner_changed, NULL, NULL);
	for (const char *path = "/org/freedesktop/ScreenSaver"; path;
			path = strcmp(path, "/ScreenSaver") ? "/ScreenSaver" : NULL) {
		g_dbus_connection_register_object(bus, path, screensaver_info->interfaces[0],
			&screensaver_vtable, NULL, NULL, NULL);
	}
	g_dbus_connection_register_object(bus, "/org/freedesktop/portal/desktop",
		portal_info->interfaces[0], &portal_vtable, NULL, NULL, NULL);
}

static void name_lost(GDBusConnection *bus, const char *name, gpointer data) {
	/* Another desktop's service has it, or there is no bus at all. */
	g_printerr("zacos9-inhibit: couldn't own %s\n", name);
}

/* ---- Wayland ----------------------------------------------------------------- */

static void global(void *data, struct wl_registry *registry, uint32_t name,
		const char *iface, uint32_t version) {
	if (strcmp(iface, platinum_shell_v1_interface.name) == 0 &&
			version >= PLATINUM_SHELL_V1_KEEP_AWAKE_SINCE_VERSION) {
		shell_version = version < PLATINUM_SHELL_V1_LOCK_SINCE_VERSION ? version :
			PLATINUM_SHELL_V1_LOCK_SINCE_VERSION;
		shell = wl_registry_bind(registry, name, &platinum_shell_v1_interface, shell_version);
	}
}

static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {
}

static const struct wl_registry_listener registry_listener = { global, global_remove };

static gboolean wayland_ready(gint fd, GIOCondition condition, gpointer loop) {
	if ((condition & (G_IO_ERR | G_IO_HUP)) || wl_display_dispatch(display) < 0) {
		g_main_loop_quit(loop); /* zacos9-wm has gone */
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

int main(void) {
	display = wl_display_connect(NULL);
	if (!display) {
		g_printerr("zacos9-inhibit: no Wayland display\n");
		return 1;
	}
	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);
	if (!shell) {
		g_printerr("zacos9-inhibit: this compositor can't keep screens awake for apps\n");
		return 1;
	}
	screensaver_info = g_dbus_node_info_new_for_xml(screensaver_xml, NULL);
	portal_info = g_dbus_node_info_new_for_xml(portal_xml, NULL);
	request_info = g_dbus_node_info_new_for_xml(request_xml, NULL);
	requests = g_hash_table_new_full(NULL, NULL, NULL, request_free);

	GMainLoop *loop = g_main_loop_new(NULL, FALSE);
	g_unix_fd_add(wl_display_get_fd(display), G_IO_IN | G_IO_ERR | G_IO_HUP, wayland_ready, loop);
	g_bus_own_name(G_BUS_TYPE_SESSION, "org.freedesktop.ScreenSaver", G_BUS_NAME_OWNER_FLAGS_NONE,
		bus_acquired, NULL, name_lost, NULL, NULL);
	g_bus_own_name(G_BUS_TYPE_SESSION, PORTAL_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
		NULL, NULL, name_lost, NULL, NULL);
	g_bus_get(G_BUS_TYPE_SYSTEM, NULL, got_system_bus, NULL);
	g_main_loop_run(loop);
	return 0;
}
