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
};

static GDBusConnection *bus;
static GPtrArray *regs; /* struct reg *, oldest first */
static guint32 active_pid;
static GMainContext *ctx;
static bool debug; /* ZACOS9_APPMENU_DEBUG=1: log registrations and focus */

static void reg_free(gpointer data) {
	struct reg *r = data;
	g_free(r->sender);
	g_free(r->path);
	if (r->layout) {
		g_variant_unref(r->layout);
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
}

void appmenu_set_active_pid(uint32_t pid) {
	if (pid == active_pid) {
		return;
	}
	active_pid = pid;
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

bool appmenu_add_menus(struct mb_menu *menus, int *n, int max) {
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

void appmenu_perform(const char *arg) {
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
