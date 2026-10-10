/*
 * Global menus: other programs' menus in our menu bar.
 *
 * We own com.canonical.AppMenu.Registrar. A program that sees it (Qt 6
 * does, by itself) exports its menu bar as com.canonical.dbusmenu, calls
 * RegisterWindow, and hides its own in-window menu bar. On Wayland the
 * window id it gives is meaningless (Qt sends 1), so a menu is matched to
 * the front window by process: zacos9-wm tells us the front window's pid
 * (platinum_shell_v1.active_client), D-Bus tells us the caller's. A
 * process with several windows shows its latest registered menu.
 *
 * GTK menus instead come from the focused surface's GMenu/GAction export
 * metadata. Their current owner takes priority over registrar menus.
 *
 * Layouts are fetched asynchronously and kept, so menus_rebuild never
 * waits on another program. GLib's main context runs inside the menu
 * bar's own poll() loop (appmenu_poll_prepare/dispatch).
 *
 * See docs/app-integration.md.
 */
#include <ctype.h>
#include <gio/gio.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>

#include "menubar.h"

#define REGISTRAR_NAME "com.canonical.AppMenu.Registrar"
#define REGISTRAR_PATH "/com/canonical/AppMenu/Registrar"
#define DBUSMENU_IFACE "com.canonical.dbusmenu"

static const char registrar_xml[] =
	"<node>"
	" <interface name='" REGISTRAR_NAME "'>"
	"  <method name='RegisterWindow'>"
	"   <arg type='u' name='windowId' direction='in'/>"
	"   <arg type='o' name='menuObjectPath' direction='in'/>"
	"  </method>"
	"  <method name='UnregisterWindow'>"
	"   <arg type='u' name='windowId' direction='in'/>"
	"  </method>"
	"  <method name='GetMenuForWindow'>"
	"   <arg type='u' name='windowId' direction='in'/>"
	"   <arg type='s' name='service' direction='out'/>"
	"   <arg type='o' name='menuObjectPath' direction='out'/>"
	"  </method>"
	" </interface>"
	"</node>";

struct reg {
	guint32 pid, window_id;
	char *sender, *path;
	GVariant *layout; /* (ia{sv}av), the root item; NULL until fetched */
	bool fetching, dirty;
	GHashTable *asked; /* item ids sent AboutToShow */
	int asking;        /* AboutToShow replies still to come */
};

static GDBusConnection *bus;
static GPtrArray *regs; /* struct reg *, oldest first */
static guint32 active_pid;
static GMainContext *ctx;
static bool debug; /* ZACOS9_APPMENU_DEBUG=1: log registrations and focus */

/* GTK metadata belongs to the focused surface. Bind proxies to the unique
 * owner, so a restarted service cannot receive an old menu's clicks. */
static struct {
	char *name, *owner, *paths[4]; /* application, window, menubar, app menu */
	guint watch, refresh;
	guint64 generation;
	guint32 verified_pid;
	bool checking_owner;
	GMenuModel *app_menu, *menubar;
	GActionGroup *groups[4];
	GPtrArray *models;
} gtk;

static void gtk_schedule_refresh(void);

static void gtk_items_changed(GMenuModel *model, gint position, gint removed,
		gint added, gpointer data) {
	gtk_schedule_refresh();
}

static void gtk_action_changed(GActionGroup *group, const char *name, gpointer data) {
	gtk_schedule_refresh();
}

static void gtk_enabled_changed(GActionGroup *group, const char *name,
		gboolean enabled, gpointer data) {
	gtk_schedule_refresh();
}

static void gtk_state_changed(GActionGroup *group, const char *name,
		GVariant *state, gpointer data) {
	gtk_schedule_refresh();
}

static void gtk_models_clear(void) {
	if (!gtk.models) {
		return;
	}
	for (guint i = 0; i < gtk.models->len; i++) {
		g_signal_handlers_disconnect_by_func(g_ptr_array_index(gtk.models, i),
			gtk_items_changed, NULL);
	}
	g_ptr_array_set_size(gtk.models, 0);
}

static void gtk_watch_model(GMenuModel *model, int depth) {
	if (!model || depth > 16 || gtk.models->len >= 256) {
		return;
	}
	for (guint i = 0; i < gtk.models->len; i++) {
		if (g_ptr_array_index(gtk.models, i) == model) {
			return;
		}
	}
	g_ptr_array_add(gtk.models, g_object_ref(model));
	g_signal_connect(model, "items-changed", G_CALLBACK(gtk_items_changed), NULL);
	int count = g_menu_model_get_n_items(model);
	for (int i = 0; i < count; i++) {
		GMenuLinkIter *links = g_menu_model_iterate_item_links(model, i);
		GMenuModel *child;
		while (g_menu_link_iter_get_next(links, NULL, &child)) {
			gtk_watch_model(child, depth + 1);
			g_object_unref(child);
		}
		g_object_unref(links);
	}
}

static gboolean gtk_refresh(gpointer data) {
	gtk.refresh = 0;
	/* Keep old proxies alive while rediscovering links: dropping the last
	 * reference first would restart lazy subscriptions on every refresh. */
	GPtrArray *old = gtk.models;
	for (guint i = 0; i < old->len; i++) {
		g_signal_handlers_disconnect_by_func(g_ptr_array_index(old, i),
			gtk_items_changed, NULL);
	}
	gtk.models = g_ptr_array_new_with_free_func(g_object_unref);
	gtk_watch_model(gtk.app_menu, 0);
	gtk_watch_model(gtk.menubar, 0);
	g_ptr_array_unref(old);
	menubar_apps_changed();
	return G_SOURCE_REMOVE;
}

static void gtk_schedule_refresh(void) {
	if (!gtk.refresh) {
		gtk.refresh = g_idle_add(gtk_refresh, NULL);
	}
}

static void gtk_exports_clear(void) {
	gtk.generation++;
	gtk.verified_pid = 0;
	gtk.checking_owner = false;
	g_clear_pointer(&gtk.owner, g_free);
	if (gtk.refresh) {
		g_source_remove(gtk.refresh);
		gtk.refresh = 0;
	}
	gtk_models_clear();
	g_clear_object(&gtk.app_menu);
	g_clear_object(&gtk.menubar);
	for (guint i = 0; i < G_N_ELEMENTS(gtk.groups); i++) {
		if (gtk.groups[i]) {
			g_signal_handlers_disconnect_by_data(gtk.groups[i], &gtk);
			g_clear_object(&gtk.groups[i]);
		}
	}
}

struct gtk_owner_check {
	guint64 generation;
	guint32 pid;
	char *owner;
};

static void gtk_owner_checked(GObject *source, GAsyncResult *result, gpointer data) {
	struct gtk_owner_check *check = data;
	GError *error = NULL;
	GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
	guint32 pid = 0;
	if (reply) {
		g_variant_get(reply, "(u)", &pid);
		g_variant_unref(reply);
	}
	bool current = check->generation == gtk.generation && check->pid == active_pid;
	if (current) {
		gtk.checking_owner = false;
	}
	if (current && error) {
		fprintf(stderr, "zacos9-menubar: GTK owner %s: %s\n", check->owner, error->message);
	}
	if (!current || !pid || pid != active_pid) {
		if (current && pid && debug) {
			fprintf(stderr, "appmenu: reject GTK owner %s pid %u (front %u)\n",
				check->owner, pid, active_pid);
		}
		g_clear_error(&error);
		g_free(check->owner);
		g_free(check);
		return;
	}
	g_clear_error(&error);
	GDBusConnection *connection = G_DBUS_CONNECTION(source);
	const char *owner = check->owner;
	gtk.owner = g_strdup(owner);
	gtk.verified_pid = pid;
	if (*gtk.paths[3]) {
		gtk.app_menu = G_MENU_MODEL(g_dbus_menu_model_get(connection, owner, gtk.paths[3]));
	}
	if (*gtk.paths[2]) {
		gtk.menubar = G_MENU_MODEL(g_dbus_menu_model_get(connection, owner, gtk.paths[2]));
	}
	for (guint i = 0; i < G_N_ELEMENTS(gtk.groups); i++) {
		if (!*gtk.paths[i]) {
			continue;
		}
		gtk.groups[i] = G_ACTION_GROUP(g_dbus_action_group_get(connection, owner, gtk.paths[i]));
		g_signal_connect(gtk.groups[i], "action-added", G_CALLBACK(gtk_action_changed), &gtk);
		g_signal_connect(gtk.groups[i], "action-removed", G_CALLBACK(gtk_action_changed), &gtk);
		g_signal_connect(gtk.groups[i], "action-enabled-changed",
			G_CALLBACK(gtk_enabled_changed), &gtk);
		g_signal_connect(gtk.groups[i], "action-state-changed",
			G_CALLBACK(gtk_state_changed), &gtk);
		/* Listing starts the asynchronous DescribeAll subscription. */
		char **actions = g_action_group_list_actions(gtk.groups[i]);
		g_strfreev(actions);
	}
	gtk_schedule_refresh();
	g_free(check->owner);
	g_free(check);
}

static void gtk_owner_appeared(GDBusConnection *connection, const char *name,
		const char *owner, gpointer data) {
	gtk_exports_clear();
	menubar_apps_changed();
	if (!active_pid) {
		return;
	}
	struct gtk_owner_check *check = g_new0(struct gtk_owner_check, 1);
	check->generation = gtk.generation;
	check->pid = active_pid;
	check->owner = g_strdup(owner);
	gtk.checking_owner = true;
	g_dbus_connection_call(connection, "org.freedesktop.DBus", "/org/freedesktop/DBus",
		"org.freedesktop.DBus", "GetConnectionUnixProcessID", g_variant_new("(s)", owner),
		G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 1000, NULL, gtk_owner_checked, check);
}

static void gtk_owner_vanished(GDBusConnection *connection, const char *name,
		gpointer data) {
	gtk_exports_clear();
	menubar_apps_changed();
}

static void gtk_start_watch(void) {
	if (bus && gtk.name && *gtk.name && !gtk.watch) {
		gtk.watch = g_bus_watch_name_on_connection(bus, gtk.name, G_BUS_NAME_WATCHER_FLAGS_NONE,
			gtk_owner_appeared, gtk_owner_vanished, NULL, NULL);
	}
}

void appmenu_set_active_gtk(const char *bus_name, const char *app_menu_path,
		const char *menubar_path, const char *window_path, const char *application_path) {
	const char *name = bus_name ? bus_name : "";
	const char *paths[] = { application_path, window_path, menubar_path, app_menu_path };
	bool valid = *name && g_dbus_is_unique_name(name);
	for (guint i = 0; i < G_N_ELEMENTS(paths); i++) {
		if (!paths[i]) {
			paths[i] = "";
		}
		if (*paths[i] && !g_variant_is_object_path(paths[i])) {
			valid = false;
		}
	}
	valid = valid && (*paths[2] || *paths[3]);
	if (!valid) {
		name = "";
		for (guint i = 0; i < G_N_ELEMENTS(paths); i++) {
			paths[i] = "";
		}
	}
	bool same = g_strcmp0(name, gtk.name) == 0;
	for (guint i = 0; i < G_N_ELEMENTS(paths); i++) {
		same &= g_strcmp0(paths[i], gtk.paths[i]) == 0;
	}
	if (same) {
		gtk_start_watch();
		return;
	}
	if (gtk.watch) {
		g_bus_unwatch_name(gtk.watch);
		gtk.watch = 0;
	}
	gtk_exports_clear();
	g_free(gtk.name);
	gtk.name = g_strdup(name);
	for (guint i = 0; i < G_N_ELEMENTS(paths); i++) {
		g_free(gtk.paths[i]);
		gtk.paths[i] = g_strdup(paths[i]);
	}
	if (!gtk.models) {
		gtk.models = g_ptr_array_new_with_free_func(g_object_unref);
	}
	gtk_start_watch();
	menubar_apps_changed();
}

static void reg_free(gpointer data) {
	struct reg *r = data;
	g_free(r->sender);
	g_free(r->path);
	if (r->layout) {
		g_variant_unref(r->layout);
	}
	if (r->asked) {
		g_hash_table_destroy(r->asked);
	}
	g_free(r);
}

static struct reg *find_reg(const char *sender, const char *path) {
	for (guint i = 0; i < regs->len; i++) {
		struct reg *r = g_ptr_array_index(regs, i);
		if (strcmp(r->sender, sender) == 0 && strcmp(r->path, path) == 0) {
			return r;
		}
	}
	return NULL;
}

/* The latest registration of the front window's process. */
static struct reg *active_reg(void) {
	if (!active_pid || !regs) {
		return NULL;
	}
	for (guint i = regs->len; i-- > 0;) {
		struct reg *r = g_ptr_array_index(regs, i);
		if (r->pid == active_pid) {
			return r;
		}
	}
	return NULL;
}

static bool reg_is_live(struct reg *r) {
	for (guint i = 0; regs && i < regs->len; i++) {
		if (g_ptr_array_index(regs, i) == r) {
			return true;
		}
	}
	return false;
}

/* ---- fetching layouts ------------------------------------------------------ */

static void fetch_layout(struct reg *r);

/* Some programs (Electron) fill a submenu only when told it is about to
 * open, and export it empty until then. Ask once for each empty submenu we
 * can show - the menus and their submenus - then fetch the layout again. */
static void about_to_show_done(GObject *source, GAsyncResult *res, gpointer data) {
	struct reg *r = data;
	GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, NULL);
	if (reply) {
		g_variant_unref(reply);
	}
	if (reg_is_live(r) && --r->asking == 0) {
		fetch_layout(r);
	}
}

static void ask_empty_submenus(struct reg *r, GVariant *item, int depth) {
	gint32 id;
	GVariant *props, *children;
	g_variant_get(item, "(i@a{sv}@av)", &id, &props, &children);
	const char *display = NULL;
	bool submenu = g_variant_lookup(props, "children-display", "&s", &display) &&
		strcmp(display, "submenu") == 0;
	gsize count = g_variant_n_children(children);
	if (depth >= 1 && submenu && count == 0 &&
			!g_hash_table_contains(r->asked, GINT_TO_POINTER(id))) {
		g_hash_table_add(r->asked, GINT_TO_POINTER(id));
		r->asking++;
		g_dbus_connection_call(bus, r->sender, r->path, DBUSMENU_IFACE, "AboutToShow",
			g_variant_new("(i)", id), NULL, G_DBUS_CALL_FLAGS_NONE, 2000, NULL,
			about_to_show_done, r);
	}
	for (gsize i = 0; depth < 2 && i < count; i++) {
		GVariant *child = g_variant_get_child_value(children, i);
		GVariant *inner = g_variant_get_variant(child);
		ask_empty_submenus(r, inner, depth + 1);
		g_variant_unref(inner);
		g_variant_unref(child);
	}
	g_variant_unref(props);
	g_variant_unref(children);
}

static void layout_fetched(GObject *source, GAsyncResult *res, gpointer data) {
	struct reg *r = data;
	GError *err = NULL;
	GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &err);
	if (!reg_is_live(r)) { /* unregistered meanwhile */
		if (reply) {
			g_variant_unref(reply);
		}
		g_clear_error(&err);
		return;
	}
	r->fetching = false;
	if (!reply) {
		fprintf(stderr, "zacos9-menubar: menus of %s%s: %s\n", r->sender, r->path, err->message);
		g_error_free(err);
	} else {
		if (r->layout) {
			g_variant_unref(r->layout);
		}
		r->layout = g_variant_get_child_value(reply, 1);
		g_variant_unref(reply);
		if (debug) {
			fprintf(stderr, "appmenu: layout of %s%s\n", r->sender, r->path);
		}
		if (!r->asked) {
			r->asked = g_hash_table_new(NULL, NULL);
		}
		ask_empty_submenus(r, r->layout, 0);
	}
	if (r->dirty) {
		fetch_layout(r);
	} else if (r->pid == active_pid) {
		menubar_apps_changed();
	}
}

static void fetch_layout(struct reg *r) {
	if (r->fetching) {
		r->dirty = true;
		return;
	}
	r->fetching = true;
	r->dirty = false;
	const char *props[] = { NULL };
	g_dbus_connection_call(bus, r->sender, r->path, DBUSMENU_IFACE, "GetLayout",
		g_variant_new("(ii^as)", 0, -1, props), G_VARIANT_TYPE("(u(ia{sv}av))"),
		G_DBUS_CALL_FLAGS_NONE, 5000, NULL, layout_fetched, r);
}

/* LayoutUpdated, ItemsPropertiesUpdated: fetch it all again. */
static void menu_changed(GDBusConnection *c, const char *sender, const char *path,
		const char *iface, const char *signal, GVariant *params, gpointer data) {
	struct reg *r = find_reg(sender, path);
	if (r) {
		fetch_layout(r);
	}
}

/* A program left the bus: forget its menus. */
static void name_owner_changed(GDBusConnection *c, const char *sender, const char *path,
		const char *iface, const char *signal, GVariant *params, gpointer data) {
	const char *name, *old_owner, *new_owner;
	g_variant_get(params, "(&s&s&s)", &name, &old_owner, &new_owner);
	if (*new_owner || name[0] != ':') {
		return;
	}
	bool front = false;
	for (guint i = regs->len; i-- > 0;) {
		struct reg *r = g_ptr_array_index(regs, i);
		if (strcmp(r->sender, name) == 0) {
			front |= r->pid == active_pid;
			g_ptr_array_remove_index(regs, i);
		}
	}
	if (front) {
		menubar_apps_changed();
	}
}

/* ---- the registrar ----------------------------------------------------------- */

static guint32 sender_pid(const char *sender) {
	GVariant *v = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus",
		"/org/freedesktop/DBus", "org.freedesktop.DBus", "GetConnectionUnixProcessID",
		g_variant_new("(s)", sender), G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE,
		1000, NULL, NULL);
	guint32 pid = 0;
	if (v) {
		g_variant_get(v, "(u)", &pid);
		g_variant_unref(v);
	}
	return pid;
}

static void remove_window(const char *sender, guint32 window_id) {
	bool front = false;
	for (guint i = regs->len; i-- > 0;) {
		struct reg *r = g_ptr_array_index(regs, i);
		if (r->window_id == window_id && strcmp(r->sender, sender) == 0) {
			front |= r->pid == active_pid;
			g_ptr_array_remove_index(regs, i);
		}
	}
	if (front) {
		menubar_apps_changed();
	}
}

static void registrar_call(GDBusConnection *c, const char *sender, const char *path,
		const char *iface, const char *method, GVariant *params,
		GDBusMethodInvocation *inv, gpointer data) {
	guint32 window_id;
	if (strcmp(method, "RegisterWindow") == 0) {
		const char *menu_path;
		g_variant_get(params, "(u&o)", &window_id, &menu_path);
		remove_window(sender, window_id);
		struct reg *r = g_new0(struct reg, 1);
		r->pid = sender_pid(sender);
		r->window_id = window_id;
		r->sender = g_strdup(sender);
		r->path = g_strdup(menu_path);
		g_ptr_array_add(regs, r);
		if (debug) {
			fprintf(stderr, "appmenu: register %s%s window %u pid %u\n", sender, menu_path,
				window_id, r->pid);
		}
		g_dbus_method_invocation_return_value(inv, NULL);
		fetch_layout(r); /* after replying: the program may wait for the reply */
	} else if (strcmp(method, "UnregisterWindow") == 0) {
		g_variant_get(params, "(u)", &window_id);
		remove_window(sender, window_id);
		g_dbus_method_invocation_return_value(inv, NULL);
	} else if (strcmp(method, "GetMenuForWindow") == 0) {
		g_variant_get(params, "(u)", &window_id);
		for (guint i = regs->len; i-- > 0;) {
			struct reg *r = g_ptr_array_index(regs, i);
			if (r->window_id == window_id) {
				g_dbus_method_invocation_return_value(inv,
					g_variant_new("(so)", r->sender, r->path));
				return;
			}
		}
		g_dbus_method_invocation_return_dbus_error(inv,
			"com.canonical.AppMenu.Registrar.Error.NotFound", "no menu for that window");
	} else {
		g_dbus_method_invocation_return_dbus_error(inv,
			"org.freedesktop.DBus.Error.UnknownMethod", method);
	}
}

static const GDBusInterfaceVTable registrar_vtable = { .method_call = registrar_call };

void appmenu_init(void) {
	debug = g_getenv("ZACOS9_APPMENU_DEBUG") != NULL;
	ctx = g_main_context_default();
	g_main_context_acquire(ctx);
	GError *err = NULL;
	bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
	if (!bus) {
		fprintf(stderr, "zacos9-menubar: no session bus, no global menus: %s\n", err->message);
		g_error_free(err);
		return;
	}
	regs = g_ptr_array_new_with_free_func(reg_free);
	GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(registrar_xml, NULL);
	g_dbus_connection_register_object(bus, REGISTRAR_PATH, info->interfaces[0],
		&registrar_vtable, NULL, NULL, NULL);
	g_dbus_node_info_unref(info);
	g_dbus_connection_signal_subscribe(bus, NULL, DBUSMENU_IFACE, "LayoutUpdated",
		NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE, menu_changed, NULL, NULL);
	g_dbus_connection_signal_subscribe(bus, NULL, DBUSMENU_IFACE, "ItemsPropertiesUpdated",
		NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE, menu_changed, NULL, NULL);
	g_dbus_connection_signal_subscribe(bus, "org.freedesktop.DBus", "org.freedesktop.DBus",
		"NameOwnerChanged", "/org/freedesktop/DBus", NULL, G_DBUS_SIGNAL_FLAGS_NONE,
		name_owner_changed, NULL, NULL);
	/* Owning the name is what makes programs hide their own menu bars, so
	 * it comes last, once we can show their menus. */
	g_bus_own_name_on_connection(bus, REGISTRAR_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
		NULL, NULL, NULL, NULL);
	gtk_start_watch(); /* Focus metadata can arrive in the initial Wayland roundtrip. */
}

void appmenu_set_active_pid(uint32_t pid) {
	if (pid == active_pid) {
		return;
	}
	active_pid = pid;
	/* Changing processes invalidates both verified exports and in-flight
	 * owner checks; watch again in case metadata arrived before the pid. */
	if (gtk.watch) {
		g_bus_unwatch_name(gtk.watch);
		gtk.watch = 0;
	}
	gtk_exports_clear();
	gtk_start_watch();
	if (debug) {
		fprintf(stderr, "appmenu: front pid %u\n", pid);
	}
	menubar_apps_changed();
}

/* ---- building our menus from a layout ----------------------------------------- */

/* "_File" -> "File", "__" -> "_". */
static void strip_mnemonic(const char *in, char *out, size_t size) {
	size_t o = 0;
	for (size_t i = 0; in[i] && o + 1 < size; i++) {
		if (in[i] == '_') {
			if (in[i + 1] != '_') {
				continue;
			}
			i++;
		}
		out[o++] = in[i];
	}
	out[o] = '\0';
}

/* Control+letter (⌘ here) is the only shortcut the menu bar can show. */
static char shortcut_key(GVariant *props) {
	GVariant *sc = g_variant_lookup_value(props, "shortcut", G_VARIANT_TYPE("aas"));
	char key = 0;
	if (sc && g_variant_n_children(sc) > 0) {
		GVariant *keys = g_variant_get_child_value(sc, 0);
		gsize n = 0;
		const char **parts = g_variant_get_strv(keys, &n);
		if (n == 2 && strcmp(parts[0], "Control") == 0 && strlen(parts[1]) == 1 &&
				isalnum((unsigned char)parts[1][0])) {
			key = (char)toupper((unsigned char)parts[1][0]);
		}
		g_free(parts);
		g_variant_unref(keys);
	}
	if (sc) {
		g_variant_unref(sc);
	}
	return key;
}

static bool prop_bool(GVariant *props, const char *name, bool fallback) {
	gboolean b;
	return g_variant_lookup(props, name, "b", &b) ? b : fallback;
}

static bool prop_is(GVariant *props, const char *name, const char *value) {
	const char *s;
	return g_variant_lookup(props, name, "&s", &s) && strcmp(s, value) == 0;
}

static GVariant *child_node(GVariant *children, gsize i) {
	GVariant *boxed = g_variant_get_child_value(children, i);
	GVariant *node = g_variant_get_variant(boxed);
	g_variant_unref(boxed);
	return node;
}

/* A menu's items, from a node's children. Submenus nest one level, as far
 * as the menu bar draws them. */
static void fill_menu(struct mb_menu *menu, GVariant *children, const struct reg *r, int depth) {
	for (gsize i = 0; i < g_variant_n_children(children); i++) {
		GVariant *node = child_node(children, i), *props, *sub;
		gint32 id;
		g_variant_get(node, "(i@a{sv}@av)", &id, &props, &sub);
		if (prop_bool(props, "visible", true)) {
			if (prop_is(props, "type", "separator")) {
				menus_add_item(menu, NULL, 0, false, ACT_NONE, NULL);
			} else {
				const char *raw = "";
				g_variant_lookup(props, "label", "&s", &raw);
				char label[256], arg[512];
				strip_mnemonic(raw, label, sizeof(label));
				snprintf(arg, sizeof(arg), "%s %s %d", r->sender, r->path, id);
				bool has_sub = g_variant_n_children(sub) > 0 ||
					prop_is(props, "children-display", "submenu");
				struct mb_item *it = menus_add_item(menu, label, shortcut_key(props),
					prop_bool(props, "enabled", true), has_sub ? ACT_NONE : ACT_DBUSMENU,
					has_sub ? NULL : arg);
				if (it) {
					gint32 state = 0;
					it->checked = g_variant_lookup(props, "toggle-state", "i", &state) &&
						state == 1;
					if (has_sub && depth == 0) {
						it->submenu = g_new0(struct mb_menu, 1);
						fill_menu(it->submenu, sub, r, depth + 1);
					} else if (has_sub) {
						it->enabled = false;
					}
				}
			}
		}
		g_variant_unref(props);
		g_variant_unref(sub);
		g_variant_unref(node);
	}
}

/* Native GTK exports use app./win.; unity-gtk-module uses unity. and
 * exports its action group alongside its menu. Some exporters retain the
 * prefix in the action group, others export the unqualified action name. */
static GActionGroup *gtk_resolve_action(const char *qualified, const char **action,
		int *index) {
	if (!qualified) {
		return NULL;
	}
	const char *dot = strchr(qualified, '.');
	if (!dot || !dot[1]) {
		return NULL;
	}
	int preferred;
	bool unity = false;
	if (g_str_has_prefix(qualified, "app.")) {
		preferred = 0;
	} else if (g_str_has_prefix(qualified, "win.")) {
		preferred = 1;
	} else if (g_str_has_prefix(qualified, "unity.")) {
		/* appmenu-gtk3-module exports at the focused window's path. */
		preferred = 1;
		unity = true;
	} else {
		return NULL;
	}
	for (int attempt = 0; attempt < 5; attempt++) {
		int i = attempt == 0 ? preferred : attempt - 1;
		/* app and win must never resolve to another window/application. */
		if (attempt && !unity) {
			break;
		}
		if (!gtk.groups[i]) {
			continue;
		}
		if (g_action_group_has_action(gtk.groups[i], dot + 1)) {
			*action = dot + 1;
		} else if (g_action_group_has_action(gtk.groups[i], qualified)) {
			*action = qualified;
		} else {
			continue;
		}
		*index = i;
		return gtk.groups[i];
	}
	return NULL;
}

static char gtk_shortcut(GMenuModel *model, int item) {
	char *accel = NULL;
	if (!g_menu_model_get_item_attribute(model, item, "accel", "s", &accel)) {
		g_menu_model_get_item_attribute(model, item, "x-gtk-accel", "s", &accel);
	}
	char key = 0;
	if (accel) {
		const char *letter = strchr(accel, '>');
		if (letter && letter[1] && !letter[2] &&
				(g_ascii_strncasecmp(accel, "<Primary>", 9) == 0 ||
				 g_ascii_strncasecmp(accel, "<Control>", 9) == 0 ||
				 g_ascii_strncasecmp(accel, "<Ctrl>", 6) == 0) &&
				g_ascii_isalnum(letter[1])) {
			key = g_ascii_toupper(letter[1]);
		}
	}
	g_free(accel);
	return key;
}

static void gtk_fill_menu(struct mb_menu *menu, GMenuModel *model, int depth,
		int submenu_depth) {
	if (depth > 16) {
		return;
	}
	int count = g_menu_model_get_n_items(model);
	for (int i = 0; i < count && menu->n < MAX_ITEMS; i++) {
		GMenuModel *section = g_menu_model_get_item_link(model, i, G_MENU_LINK_SECTION);
		if (section) {
			int before = menu->n;
			if (before && menu->items[before - 1].label) {
				menus_add_item(menu, NULL, 0, false, ACT_NONE, NULL);
			}
			int start = menu->n;
			gtk_fill_menu(menu, section, depth + 1, submenu_depth);
			if (menu->n == start) {
				menu->n = before; /* don't add separators for empty sections */
			}
			g_object_unref(section);
			continue;
		}
		char *raw = NULL, *qualified = NULL, *hidden = NULL;
		g_menu_model_get_item_attribute(model, i, G_MENU_ATTRIBUTE_LABEL, "s", &raw);
		g_menu_model_get_item_attribute(model, i, G_MENU_ATTRIBUTE_ACTION, "s", &qualified);
		g_menu_model_get_item_attribute(model, i, "hidden-when", "s", &hidden);
		GVariant *target = g_menu_model_get_item_attribute_value(model, i,
			G_MENU_ATTRIBUTE_TARGET, NULL);
		const char *action = NULL;
		int group_index = 0;
		GActionGroup *group = gtk_resolve_action(qualified, &action, &group_index);
		bool enabled = group && g_action_group_get_action_enabled(group, action);
		const GVariantType *parameter = group ?
			g_action_group_get_action_parameter_type(group, action) : NULL;
		bool valid = group && (parameter ? target && g_variant_is_of_type(target, parameter) :
			target == NULL);
		enabled &= valid;
		GMenuModel *sub = g_menu_model_get_item_link(model, i, G_MENU_LINK_SUBMENU);
		bool visible = !(g_strcmp0(hidden, "action-missing") == 0 && !group) &&
			!(g_strcmp0(hidden, "action-disabled") == 0 && !enabled) &&
			!(g_strcmp0(hidden, "action-invalid") == 0 && !valid);
		if (visible && raw) {
			char label[256];
			strip_mnemonic(raw, label, sizeof(label));
			char *arg = NULL;
			if (group && !sub) {
				GVariant *route = g_variant_ref_sink(g_variant_new("(tss@av)",
					gtk.generation, gtk.paths[group_index], action,
					g_variant_new_array(G_VARIANT_TYPE_VARIANT,
						target ? (GVariant *[]){ g_variant_new_variant(target) } : NULL,
						target ? 1 : 0)));
				char *text = g_variant_print(route, TRUE);
				arg = g_strconcat("gtk:", text, NULL);
				g_free(text);
				g_variant_unref(route);
			}
			struct mb_item *it = menus_add_item(menu, label, gtk_shortcut(model, i),
				sub && !qualified ? true : enabled, sub ? ACT_NONE : ACT_DBUSMENU, arg);
			g_free(arg);
			if (it) {
				GVariant *state = group ? g_action_group_get_action_state(group, action) : NULL;
				it->checked = state && (target ? g_variant_equal(state, target) :
					g_variant_is_of_type(state, G_VARIANT_TYPE_BOOLEAN) &&
					g_variant_get_boolean(state));
				g_clear_pointer(&state, g_variant_unref);
				if (sub && submenu_depth == 0) {
					it->submenu = g_new0(struct mb_menu, 1);
					gtk_fill_menu(it->submenu, sub, depth + 1, submenu_depth + 1);
				} else if (sub) {
					it->enabled = false;
				}
			}
		}
		g_clear_object(&sub);
		g_clear_pointer(&target, g_variant_unref);
		g_free(raw);
		g_free(qualified);
		g_free(hidden);
	}
}

static void gtk_add_top(struct mb_menu *menus, int *n, int max,
		GMenuModel *model, int depth) {
	if (!model || depth > 16) {
		return;
	}
	int count = g_menu_model_get_n_items(model);
	for (int i = 0; i < count && *n < max; i++) {
		GMenuModel *section = g_menu_model_get_item_link(model, i, G_MENU_LINK_SECTION);
		if (section) {
			gtk_add_top(menus, n, max, section, depth + 1);
			g_object_unref(section);
			continue;
		}
		GMenuModel *sub = g_menu_model_get_item_link(model, i, G_MENU_LINK_SUBMENU);
		char *raw = NULL;
		g_menu_model_get_item_attribute(model, i, G_MENU_ATTRIBUTE_LABEL, "s", &raw);
		if (sub && raw) {
			char title[256];
			strip_mnemonic(raw, title, sizeof(title));
			gtk_fill_menu(menus_new_menu(menus, n, title), sub, 0, 0);
		}
		g_free(raw);
		g_clear_object(&sub);
	}
}

static bool gtk_add_menus(struct mb_menu *menus, int *n, int max) {
	if (!gtk.owner || !active_pid || gtk.verified_pid != active_pid) {
		return false;
	}
	int before = *n;
	if (gtk.app_menu && g_menu_model_get_n_items(gtk.app_menu) > 0 && *n < max) {
		gtk_fill_menu(menus_new_menu(menus, n, "Application"), gtk.app_menu, 0, 0);
	}
	gtk_add_top(menus, n, max, gtk.menubar, 0);
	return *n > before;
}

bool appmenu_add_menus(struct mb_menu *menus, int *n, int max) {
	if (gtk_add_menus(menus, n, max)) {
		return true;
	}
	struct reg *r = active_reg();
	if (!r || !r->layout) {
		return false;
	}
	GVariant *props, *top;
	gint32 id;
	g_variant_get(r->layout, "(i@a{sv}@av)", &id, &props, &top);
	int before = *n;
	for (gsize i = 0; i < g_variant_n_children(top) && *n < max; i++) {
		GVariant *node = child_node(top, i), *mprops, *items;
		gint32 mid;
		g_variant_get(node, "(i@a{sv}@av)", &mid, &mprops, &items);
		if (prop_bool(mprops, "visible", true)) {
			const char *raw = "";
			g_variant_lookup(mprops, "label", "&s", &raw);
			char title[256];
			strip_mnemonic(raw, title, sizeof(title));
			fill_menu(menus_new_menu(menus, n, title), items, r, 0);
		}
		g_variant_unref(mprops);
		g_variant_unref(items);
		g_variant_unref(node);
	}
	g_variant_unref(props);
	g_variant_unref(top);
	return *n > before;
}

/* ---- choosing an item ---------------------------------------------------------- */

static void gtk_activated(GObject *source, GAsyncResult *result, gpointer data) {
	GError *error = NULL;
	GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
	if (!reply) {
		fprintf(stderr, "zacos9-menubar: GTK action %s: %s\n", (char *)data, error->message);
		g_clear_error(&error);
	} else {
		g_variant_unref(reply);
	}
	g_free(data);
}

static void gtk_perform(const char *text) {
	GError *error = NULL;
	GVariant *route = g_variant_parse(G_VARIANT_TYPE("(tssav)"), text, NULL, NULL, &error);
	if (!route) {
		fprintf(stderr, "zacos9-menubar: invalid GTK action: %s\n", error->message);
		g_clear_error(&error);
		return;
	}
	guint64 generation;
	const char *path, *action;
	GVariant *targets;
	g_variant_get(route, "(t&s&s@av)", &generation, &path, &action, &targets);
	GActionGroup *group = NULL;
	for (guint i = 0; i < G_N_ELEMENTS(gtk.groups); i++) {
		if (gtk.groups[i] && g_strcmp0(gtk.paths[i], path) == 0) {
			group = gtk.groups[i];
			break;
		}
	}
	if (generation == gtk.generation && gtk.owner && gtk.verified_pid == active_pid && group &&
			g_action_group_has_action(group, action) &&
			g_action_group_get_action_enabled(group, action)) {
		const GVariantType *parameter = g_action_group_get_action_parameter_type(group, action);
		GVariant *target = g_variant_n_children(targets) == 1 ?
			g_variant_get_child_value(targets, 0) : NULL;
		GVariant *value = target ? g_variant_get_variant(target) : NULL;
		bool valid = parameter ? value && g_variant_is_of_type(value, parameter) :
			g_variant_n_children(targets) == 0;
		if (valid) {
			g_dbus_connection_call(bus, gtk.owner, path, "org.gtk.Actions", "Activate",
				g_variant_new("(s@av@a{sv})", action, g_variant_ref(targets),
					g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0)),
				G_VARIANT_TYPE_UNIT, G_DBUS_CALL_FLAGS_NONE, 5000, NULL,
				gtk_activated, g_strdup(action));
		}
		g_clear_pointer(&value, g_variant_unref);
		g_clear_pointer(&target, g_variant_unref);
	}
	g_variant_unref(targets);
	g_variant_unref(route);
}

void appmenu_perform(const char *arg) {
	if (bus && arg && g_str_has_prefix(arg, "gtk:")) {
		gtk_perform(arg + 4);
		return;
	}
	char sender[256], path[256];
	int id;
	if (!bus || !arg || sscanf(arg, "%255s %255s %d", sender, path, &id) != 3) {
		return;
	}
	g_dbus_connection_call(bus, sender, path, DBUSMENU_IFACE, "Event",
		g_variant_new("(isvu)", id, "clicked", g_variant_new_int32(0), 0u),
		NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

/* ---- GLib inside the menu bar's poll() loop -------------------------------------- */

#define MAX_GFDS 16
static GPollFD gfds[MAX_GFDS];
static int n_gfds;
static gint max_priority;

int appmenu_poll_prepare(struct pollfd *fds, int max, int *timeout) {
	if (!ctx) {
		return 0;
	}
	g_main_context_prepare(ctx, &max_priority);
	gint gtimeout = -1;
	n_gfds = g_main_context_query(ctx, max_priority, &gtimeout, gfds, MAX_GFDS);
	if (n_gfds > MAX_GFDS || n_gfds > max) {
		n_gfds = MAX_GFDS < max ? MAX_GFDS : max;
	}
	for (int i = 0; i < n_gfds; i++) {
		fds[i] = (struct pollfd){ .fd = gfds[i].fd, .events = gfds[i].events };
	}
	if (gtimeout >= 0 && (*timeout < 0 || gtimeout < *timeout)) {
		*timeout = gtimeout;
	}
	return n_gfds;
}

void appmenu_poll_dispatch(const struct pollfd *fds) {
	if (!ctx) {
		return;
	}
	for (int i = 0; i < n_gfds; i++) {
		gfds[i].revents = fds[i].revents;
	}
	if (g_main_context_check(ctx, max_priority, gfds, n_gfds)) {
		g_main_context_dispatch(ctx);
	}
}
