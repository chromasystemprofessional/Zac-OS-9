/* Run under dbus-run-session; no display, compositor or GTK dependency. */
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "appmenu.c"

static int changes, activations;
static char *last_action;
static GVariant *last_target;

void menubar_apps_changed(void) {
	changes++;
}

struct mb_menu *menus_new_menu(struct mb_menu *menus, int *n, const char *title) {
	struct mb_menu *menu = &menus[(*n)++];
	menu->title = g_strdup(title);
	return menu;
}

struct mb_item *menus_add_item(struct mb_menu *menu, const char *label, char key,
		bool enabled, enum action action, const char *arg) {
	if (menu->n >= MAX_ITEMS) {
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

static void clear_menu(struct mb_menu *menu) {
	for (int i = 0; i < menu->n; i++) {
		g_free(menu->items[i].label);
		g_free(menu->items[i].arg);
		if (menu->items[i].submenu) {
			clear_menu(menu->items[i].submenu);
			g_free(menu->items[i].submenu);
		}
	}
	g_free(menu->title);
	memset(menu, 0, sizeof(*menu));
}

static void drain(void) {
	while (g_main_context_iteration(NULL, FALSE)) {
	}
}

#define WAIT(condition) do { \
	gint64 deadline = g_get_monotonic_time() + 3000000; \
	while (!(condition) && g_get_monotonic_time() < deadline) { \
		drain(); \
		g_usleep(1000); \
	} \
	assert(condition); \
} while (0)

static void activated(GSimpleAction *action, GVariant *target, gpointer data) {
	activations++;
	g_free(last_action);
	last_action = g_strdup(g_action_get_name(G_ACTION(action)));
	g_clear_pointer(&last_target, g_variant_unref);
	last_target = target ? g_variant_ref(target) : NULL;
}

static GSimpleAction *add_action(GSimpleActionGroup *group, const char *name,
		const GVariantType *type, GVariant *state) {
	GSimpleAction *action = state ? g_simple_action_new_stateful(name, type, state) :
		g_simple_action_new(name, type);
	g_signal_connect(action, "activate", G_CALLBACK(activated), NULL);
	g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(action));
	return action;
}

static struct mb_menu rendered[MAX_TITLES];
static int rendered_n;

static bool render_has(const char *title, int items) {
	for (int i = 0; i < rendered_n; i++) {
		clear_menu(&rendered[i]);
	}
	rendered_n = 0;
	appmenu_add_menus(rendered, &rendered_n, MAX_TITLES);
	for (int i = 0; i < rendered_n; i++) {
		if (g_strcmp0(rendered[i].title, title) == 0 && rendered[i].n == items) {
			return true;
		}
	}
	return false;
}

static void qt_event(GDBusConnection *connection, const char *sender, const char *path,
		const char *interface, const char *method, GVariant *params,
		GDBusMethodInvocation *invocation, gpointer data) {
	gint32 id;
	const char *event;
	GVariant *value;
	guint32 timestamp;
	g_variant_get(params, "(i&s@vu)", &id, &event, &value, &timestamp);
	assert(id == 2 && strcmp(event, "clicked") == 0);
	g_variant_unref(value);
	activations++;
	g_dbus_method_invocation_return_value(invocation, NULL);
}

static void expect_activation_error(const char *route) {
	int fds[2];
	assert(pipe(fds) == 0);
	assert(fcntl(fds[0], F_SETFL, O_NONBLOCK) == 0);
	int saved_stderr = dup(STDERR_FILENO);
	assert(saved_stderr >= 0 && dup2(fds[1], STDERR_FILENO) >= 0);
	close(fds[1]);
	appmenu_perform(route);
	char diagnostic[1024] = { 0 };
	ssize_t size = 0;
	WAIT(size > 0 || (size = read(fds[0], diagnostic, sizeof(diagnostic) - 1)) > 0);
	assert(dup2(saved_stderr, STDERR_FILENO) >= 0);
	close(saved_stderr);
	close(fds[0]);
	assert(strstr(diagnostic, "GTK action open:"));
	assert(strstr(diagnostic, "org.freedesktop.DBus.Error"));
}

int main(void) {
	GError *error = NULL;
	char *address = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, NULL, &error);
	assert(address && !error);
	GDBusConnection *exporter = g_dbus_connection_new_for_address_sync(address,
		G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
		G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION, NULL, NULL, &error);
	g_free(address);
	assert(exporter && !error);
	const char *name = g_dbus_connection_get_unique_name(exporter);
	appmenu_set_active_gtk(name, "/AppMenu", "/Menu", "/Window", "/App");
	assert(!bus && !gtk.watch && strcmp(gtk.name, name) == 0);
	appmenu_set_active_pid(getpid());
	appmenu_init();
	assert(bus);
	/* Invalidate an owner lookup while it is in flight. */
	while (!gtk.checking_owner) {
		g_main_context_iteration(NULL, TRUE);
	}
	appmenu_set_active_pid(999);
	appmenu_set_active_pid(getpid());
	WAIT(gtk.owner);
	assert(gtk.verified_pid == (guint32)getpid());
	appmenu_set_active_gtk("", "", "", "", "");

	/* A registrar menu remains available underneath focused GTK metadata. */
	struct reg *r = g_new0(struct reg, 1);
	r->pid = getpid();
	r->sender = g_strdup(g_dbus_connection_get_unique_name(bus));
	r->path = g_strdup("/Qt");
	r->layout = g_variant_parse(G_VARIANT_TYPE("(ia{sv}av)"),
		"(0, {}, [<(1, {'label': <'_Qt'>}, "
		"[<(2, {'label': <'Legacy'>}, @av [])>])>])", NULL, NULL, &error);
	assert(r->layout && !error);
	g_ptr_array_add(regs, r);
	appmenu_set_active_pid(getpid());
	assert(render_has("Qt", 1));
	const char *xml = "<node><interface name='com.canonical.dbusmenu'>"
		"<method name='Event'><arg type='i' direction='in'/>"
		"<arg type='s' direction='in'/><arg type='v' direction='in'/>"
		"<arg type='u' direction='in'/></method></interface></node>";
	GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(xml, &error);
	GDBusInterfaceVTable vtable = { .method_call = qt_event };
	guint qt_export = g_dbus_connection_register_object(bus, "/Qt", info->interfaces[0],
		&vtable, NULL, NULL, &error);
	assert(qt_export && !error);
	appmenu_perform(rendered[0].items[0].arg);
	WAIT(activations == 1);
	activations = 0;

	GSimpleActionGroup *app = g_simple_action_group_new();
	GSimpleActionGroup *win = g_simple_action_group_new();
	GSimpleActionGroup *unity = g_simple_action_group_new();
	GSimpleAction *about = add_action(app, "about", NULL, NULL);
	GSimpleAction *radio = add_action(app, "mode", G_VARIANT_TYPE_STRING,
		g_variant_new_string("one"));
	GSimpleAction *toggle = add_action(win, "toggle", NULL, g_variant_new_boolean(TRUE));
	GSimpleAction *open = add_action(win, "open", NULL, NULL);
	GSimpleAction *window_compat = add_action(win, "compat", NULL, NULL);
	GSimpleAction *compat = add_action(unity, "unity.compat", NULL, NULL);
	guint app_export = g_dbus_connection_export_action_group(exporter, "/App",
		G_ACTION_GROUP(app), &error);
	guint win_export = g_dbus_connection_export_action_group(exporter, "/Window",
		G_ACTION_GROUP(win), &error);
	guint unity_export = g_dbus_connection_export_action_group(exporter, "/Menu",
		G_ACTION_GROUP(unity), &error);
	assert(app_export && win_export && unity_export && !error);

	GMenu *app_menu = g_menu_new();
	g_menu_append(app_menu, "_About", "app.about");
	GMenu *file = g_menu_new(), *section = g_menu_new(), *nested = g_menu_new();
	GMenuItem *item = g_menu_item_new("_Open", "win.open");
	g_menu_item_set_attribute(item, "accel", "s", "<Primary>o");
	g_menu_append_item(section, item);
	g_object_unref(item);
	g_menu_append(section, "_Toggle", "win.toggle");
	g_menu_append_section(file, NULL, G_MENU_MODEL(section));
	GMenu *second = g_menu_new();
	g_menu_append(second, "Mode _One", "app.mode::one");
	g_menu_append(second, "Mode _Two", "app.mode::two");
	g_menu_append(nested, "_Nested", "win.open");
	g_menu_append_submenu(second, "_More", G_MENU_MODEL(nested));
	g_menu_append(second, "Compatibility", "unity.compat");
	g_menu_append(second, "Unknown", "other.open");
	item = g_menu_item_new("Hidden", "app.missing");
	g_menu_item_set_attribute(item, "hidden-when", "s", "action-missing");
	g_menu_append_item(second, item);
	g_object_unref(item);
	g_menu_append_section(file, NULL, G_MENU_MODEL(second));
	GMenu *menubar = g_menu_new();
	g_menu_append_submenu(menubar, "_File", G_MENU_MODEL(file));
	guint app_menu_export = g_dbus_connection_export_menu_model(exporter, "/AppMenu",
		G_MENU_MODEL(app_menu), &error);
	guint menu_export = g_dbus_connection_export_menu_model(exporter, "/Menu",
		G_MENU_MODEL(menubar), &error);
	assert(app_menu_export && menu_export && !error);
	appmenu_set_active_gtk(name, "/AppMenu", "/Menu", "/Window", "/App");
	WAIT(gtk.owner && gtk.groups[0] &&
		g_action_group_has_action(gtk.groups[0], "mode") &&
		g_action_group_has_action(gtk.groups[1], "open") &&
		g_action_group_has_action(gtk.groups[2], "unity.compat"));
	assert(gtk.verified_pid == (guint32)getpid());
	WAIT(render_has("File", 8) && rendered[1].items[5].submenu &&
		rendered[1].items[5].submenu->n == 1);
	assert(rendered_n == 2);
	struct mb_menu *menu = &rendered[1];
	assert(strcmp(menu->items[0].label, "Open") == 0 && menu->items[0].key == 'O');
	assert(menu->items[0].enabled && menu->items[1].checked);
	assert(!menu->items[2].label);
	assert(menu->items[3].checked && !menu->items[4].checked);
	assert(menu->items[5].submenu && menu->items[5].submenu->n == 1);
	assert(menu->items[6].enabled && !menu->items[7].enabled);
	appmenu_perform(menu->items[4].arg);
	WAIT(activations == 1);
	assert(strcmp(last_action, "mode") == 0);
	assert(g_variant_is_of_type(last_target, G_VARIANT_TYPE_STRING));
	assert(strcmp(g_variant_get_string(last_target, NULL), "two") == 0);
	appmenu_perform(menu->items[6].arg);
	WAIT(activations == 2);
	assert(strcmp(last_action, "compat") == 0); /* window export wins */
	appmenu_perform(menu->items[1].arg);
	WAIT(activations == 3);
	assert(strcmp(last_action, "toggle") == 0 && !last_target);
	appmenu_perform(rendered[0].items[0].arg);
	WAIT(activations == 4);
	assert(strcmp(last_action, "about") == 0);
	g_action_map_remove_action(G_ACTION_MAP(win), "compat");
	WAIT(!g_action_group_has_action(gtk.groups[1], "compat"));
	WAIT(render_has("File", 8));
	appmenu_perform(rendered[1].items[6].arg);
	WAIT(activations == 5);
	assert(strcmp(last_action, "unity.compat") == 0); /* menu-path fallback */
	activations = 4;

	char *stale = g_strdup(menu->items[0].arg);
	int before = changes;
	g_simple_action_set_enabled(open, FALSE);
	g_simple_action_set_state(toggle, g_variant_new_boolean(FALSE));
	g_simple_action_set_state(radio, g_variant_new_string("two"));
	WAIT(!g_action_group_get_action_enabled(gtk.groups[1], "open"));
	WAIT(changes > before);
	WAIT(render_has("File", 8) && !rendered[1].items[1].checked &&
		rendered[1].items[4].checked);
	assert(!rendered[1].items[0].enabled);
	appmenu_perform(stale); /* Recheck enabled state at activation time. */
	drain();
	assert(activations == 4);
	g_simple_action_set_enabled(open, TRUE);
	WAIT(g_action_group_get_action_enabled(gtk.groups[1], "open"));

	/* Sections introduced after the first snapshot must be subscribed too. */
	GMenu *late = g_menu_new();
	g_menu_append(late, "Late", "win.open");
	g_menu_append_section(file, NULL, G_MENU_MODEL(late));
	WAIT(render_has("File", 10));
	g_menu_append(late, "Later", "app.about");
	WAIT(render_has("File", 11));
	g_action_map_remove_action(G_ACTION_MAP(win), "open");
	WAIT(!g_action_group_has_action(gtk.groups[1], "open"));
	WAIT(render_has("File", 11) && !rendered[1].items[9].enabled);
	g_action_map_add_action(G_ACTION_MAP(win), G_ACTION(open));
	WAIT(g_action_group_has_action(gtk.groups[1], "open"));

	/* Export owner must match the focused pid, even with valid metadata. */
	before = changes;
	appmenu_set_active_pid(999);
	WAIT(changes > before + 1 && !gtk.checking_owner);
	assert(gtk.watch && !gtk.owner);
	assert(!render_has("File", 11));
	appmenu_set_active_pid(getpid());
	WAIT(render_has("File", 11));
	appmenu_set_active_gtk("", "", "", "", "");
	assert(render_has("Qt", 1));
	appmenu_perform(stale);
	drain();
	assert(activations == 4);
	g_free(stale);
	appmenu_set_active_gtk("org.zacos9.NoSuchOwner", "", "/Menu", "", "");
	assert(!gtk.watch && !gtk.owner && !*gtk.name);
	assert(render_has("Qt", 1));
	appmenu_set_active_gtk(name, "", "not/a/path", "", "");
	assert(!gtk.watch && !*gtk.name);
	appmenu_set_active_gtk(":12345.67890", "", "/Menu", "", "");
	WAIT(gtk.watch && !gtk.owner);
	assert(render_has("Qt", 1));
	appmenu_set_active_gtk(name, "", "/Menu", "/Window", "/App");
	WAIT(render_has("File", 11));
	assert(rendered_n == 1); /* app-menu path is optional */
	stale = g_strdup(rendered[0].items[9].arg);
	assert(g_dbus_connection_close_sync(exporter, NULL, &error));
	assert(!error);
	WAIT(!gtk.owner);
	assert(render_has("Qt", 1));
	g_object_unref(exporter);

	/* A new unique owner requires fresh focused metadata. */
	address = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, NULL, &error);
	exporter = g_dbus_connection_new_for_address_sync(address,
		G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
		G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION, NULL, NULL, &error);
	g_free(address);
	assert(exporter && !error);
	assert(g_dbus_connection_export_action_group(exporter, "/App", G_ACTION_GROUP(app), &error));
	win_export = g_dbus_connection_export_action_group(exporter, "/Window",
		G_ACTION_GROUP(win), &error);
	assert(win_export);
	assert(g_dbus_connection_export_action_group(exporter, "/Menu", G_ACTION_GROUP(unity), &error));
	assert(g_dbus_connection_export_menu_model(exporter, "/Menu", G_MENU_MODEL(menubar), &error));
	assert(render_has("Qt", 1));
	appmenu_set_active_gtk(g_dbus_connection_get_unique_name(exporter),
		"", "/Menu", "/Window", "/App");
	WAIT(render_has("File", 11) && rendered[0].items[9].enabled);
	appmenu_perform(stale);
	drain();
	assert(activations == 4);
	g_free(stale);
	appmenu_perform(rendered[0].items[9].arg);
	WAIT(activations == 5);
	assert(strcmp(last_action, "open") == 0);
	g_dbus_connection_unexport_action_group(exporter, win_export);
	expect_activation_error(rendered[0].items[9].arg);
	assert(activations == 5);
	appmenu_set_active_gtk("", "", "", "", "");
	for (int i = 0; i < rendered_n; i++) {
		clear_menu(&rendered[i]);
	}
	g_dbus_connection_unregister_object(bus, qt_export);
	g_dbus_node_info_unref(info);
	assert(g_dbus_connection_close_sync(exporter, NULL, &error));
	g_object_unref(exporter);
	g_object_unref(app_menu);
	g_object_unref(file);
	g_object_unref(section);
	g_object_unref(second);
	g_object_unref(nested);
	g_object_unref(menubar);
	g_object_unref(late);
	g_object_unref(about);
	g_object_unref(radio);
	g_object_unref(toggle);
	g_object_unref(open);
	g_object_unref(window_compat);
	g_object_unref(compat);
	g_object_unref(app);
	g_object_unref(win);
	g_object_unref(unity);
	g_free(last_action);
	g_clear_pointer(&last_target, g_variant_unref);
	puts("ok: GTK models/actions, updates, focus, owner loss and Qt fallback");
	return 0;
}
