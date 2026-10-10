#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>
#include <appmenu-gtk-parser.h>

/* GTK uses this API itself to publish menus on gtk_surface1. */
void gdk_wayland_window_set_dbus_properties_libgtk_only(GdkWindow *window,
	const char *application_id, const char *app_menu_path, const char *menubar_path,
	const char *window_object_path, const char *application_object_path, const char *unique_bus_name);

struct export {
	GDBusConnection *bus;
	GdkWindow *published_window;
	GMenu *model;
	UnityGtkActionGroup *actions;
	GPtrArray *shells, *bars;
	guint menu_id, actions_id;
	char *path;
};

static GDBusConnection *connection;
static gboolean registrar;
static guint scan_source;
static GQuark export_key;

static void destroy_export(gpointer data) {
	struct export *export = data;
	if (export->menu_id) {
		g_dbus_connection_unexport_menu_model(export->bus, export->menu_id);
	}
	if (export->actions_id) {
		g_dbus_connection_unexport_action_group(export->bus, export->actions_id);
	}
	for (guint i = 0; i < export->shells->len; i++) {
		unity_gtk_action_group_disconnect_shell(export->actions, g_ptr_array_index(export->shells, i));
	}
	g_ptr_array_unref(export->shells);
	g_ptr_array_unref(export->bars);
	g_object_unref(export->actions);
	g_object_unref(export->model);
	g_object_unref(export->bus);
	g_clear_object(&export->published_window);
	g_free(export->path);
	g_free(export);
}

static void collect_bars(GtkWidget *widget, gpointer data) {
	GPtrArray *bars = data;
	if (GTK_IS_MENU_BAR(widget)) {
		g_ptr_array_add(bars, widget);
		return;
	}
	if (GTK_IS_CONTAINER(widget)) {
		gtk_container_foreach(GTK_CONTAINER(widget), collect_bars, data);
	}
}

static gboolean global_menus(GtkWidget *window) {
	GValue value = G_VALUE_INIT;
	g_value_init(&value, G_TYPE_BOOLEAN);
	gboolean supported = gdk_screen_get_setting(gtk_widget_get_screen(window),
		"gtk-shell-shows-menubar", &value) && g_value_get_boolean(&value);
	g_value_unset(&value);
	return supported;
}

/* GTK exports an application window's menus itself when the application
 * gives it a menu model to show. An application window with a menu bar
 * widget of its own instead (GIMP 3's image windows) is ours to export. */
static gboolean gtk_exports_menus(GtkWindow *window) {
	if (!GTK_IS_APPLICATION_WINDOW(window)) {
		return FALSE;
	}
	GtkApplication *app = gtk_window_get_application(window);
	return app && (gtk_application_get_menubar(app) || gtk_application_get_app_menu(app));
}

static void connect_bars(struct export *export, GPtrArray *bars) {
	for (guint i = 0; i < bars->len; i++) {
		GtkWidget *bar = g_ptr_array_index(bars, i);
		if (!g_ptr_array_find(export->bars, bar, NULL)) {
			UnityGtkMenuShell *shell = unity_gtk_menu_shell_new(GTK_MENU_SHELL(bar));
			unity_gtk_action_group_connect_shell(export->actions, shell);
			g_menu_append_section(export->model, NULL, G_MENU_MODEL(shell));
			g_ptr_array_add(export->shells, shell);
			g_ptr_array_add(export->bars, g_object_ref(bar));
		}
	}
}

/* allow_empty: export even before the window has a menu bar widget (it
 * fills in later), for windows whose properties must be set now. */
static void export_window(GtkWindow *window, gboolean allow_empty) {
	GtkWidget *widget = GTK_WIDGET(window);
	GdkWindow *gdk_window = gtk_widget_get_window(widget);
	if (!connection || !gdk_window || !GDK_IS_WAYLAND_WINDOW(gdk_window) ||
			!global_menus(widget) || gtk_exports_menus(window) ||
			gtk_window_get_window_type(window) != GTK_WINDOW_TOPLEVEL) {
		return;
	}
	struct export *export = g_object_get_qdata(G_OBJECT(window), export_key);
	if (!export) {
		GPtrArray *bars = g_ptr_array_new();
		collect_bars(widget, bars);
		if (!bars->len && !allow_empty) {
			g_ptr_array_unref(bars);
			return;
		}
		static guint next_id;
		export = g_new0(struct export, 1);
		export->bus = g_object_ref(connection);
		export->model = g_menu_new();
		export->actions = unity_gtk_action_group_new(NULL);
		export->shells = g_ptr_array_new_with_free_func(g_object_unref);
		export->bars = g_ptr_array_new_with_free_func(g_object_unref);
		export->path = g_strdup_printf("/org/zacos9/menus/window%u", ++next_id);
		connect_bars(export, bars);
		g_ptr_array_unref(bars);
		GError *error = NULL;
		export->menu_id = g_dbus_connection_export_menu_model(connection, export->path,
			G_MENU_MODEL(export->model), &error);
		if (export->menu_id) {
			export->actions_id = g_dbus_connection_export_action_group(connection, export->path,
				G_ACTION_GROUP(export->actions), &error);
		}
		if (!export->menu_id || !export->actions_id) {
			g_warning("ZacOS GTK menu export failed: %s", error ? error->message : "unknown error");
			g_clear_error(&error);
			destroy_export(export);
			return;
		}
		g_object_set_qdata_full(G_OBJECT(window), export_key, export, destroy_export);
	} else {
		GPtrArray *bars = g_ptr_array_new();
		collect_bars(widget, bars);
		connect_bars(export, bars);
		g_ptr_array_unref(bars);
	}
	if (export->published_window != gdk_window) {
		g_set_object(&export->published_window, gdk_window);
		gdk_wayland_window_set_dbus_properties_libgtk_only(gdk_window, g_get_prgname(),
			NULL, export->path, export->path, NULL, g_dbus_connection_get_unique_name(connection));
	}
	for (guint i = 0; i < export->bars->len; i++) {
		gtk_widget_set_child_visible(g_ptr_array_index(export->bars, i), !registrar);
	}
}

static gboolean scan_windows(gpointer data) {
	scan_source = 0;
	GList *windows = gtk_window_list_toplevels();
	for (GList *item = windows; item; item = item->next) {
		if (gtk_widget_get_realized(item->data)) {
			GtkWidget *widget = item->data;
			if (GTK_IS_FILE_CHOOSER(widget)) {
				GtkFileChooser *chooser = GTK_FILE_CHOOSER(widget);
				if (gtk_file_chooser_get_action(chooser) == GTK_FILE_CHOOSER_ACTION_SAVE) {
					char *current = gtk_file_chooser_get_current_folder(chooser);
					char *home = g_strdup(g_get_home_dir());
					char *working = g_get_current_dir();
					const gboolean use_desktop = !current ||
						g_strcmp0(current, home) == 0 || g_strcmp0(current, working) == 0;
					g_free(current);
					g_free(home);
					g_free(working);
					if (use_desktop) {
						const char *desktop =
							g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP);
						if (desktop && !gtk_file_chooser_set_current_folder(chooser, desktop)) {
							g_warning("ZacOS could not set the GTK save dialog to Desktop: %s",
								desktop);
						}
					}
				}
			}
			export_window(item->data, FALSE);
		}
	}
	g_list_free(windows);
	return G_SOURCE_REMOVE;
}

static void queue_scan(void) {
	if (!scan_source) {
		scan_source = g_idle_add(scan_windows, NULL);
	}
}

static gboolean mapped(GSignalInvocationHint *hint, guint n, const GValue *values, gpointer data) {
	GtkWidget *widget = g_value_get_object(&values[0]);
	if (GTK_IS_WINDOW(widget) || GTK_IS_MENU_BAR(widget)) {
		queue_scan();
	}
	return TRUE;
}

/* GDK sends a window's menu properties once, when it is first shown, with
 * the values last given; later ones are ignored. For an application window
 * GTK gives its own on realize (no menu bar, if the application has no menu
 * model), so ours must follow straight after that, before the window shows. */
static void app_window_realized(GtkWidget *widget, gpointer data) {
	/* An application can get this far before the bus we asked for arrives. */
	if (!connection) {
		connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
	}
	export_window(GTK_WINDOW(widget), TRUE);
}

static gboolean window_added(GSignalInvocationHint *hint, guint n, const GValue *values,
		gpointer data) {
	GtkWindow *window = g_value_get_object(&values[1]);
	if (GTK_IS_APPLICATION_WINDOW(window) && !gtk_exports_menus(window) &&
			!gtk_widget_get_realized(GTK_WIDGET(window))) {
		g_signal_connect_after(window, "realize", G_CALLBACK(app_window_realized), NULL);
	}
	return TRUE;
}

static void appeared(GDBusConnection *bus, const char *name, const char *owner, gpointer data) {
	registrar = TRUE;
	queue_scan();
}

static void vanished(GDBusConnection *bus, const char *name, gpointer data) {
	registrar = FALSE;
	queue_scan();
}

static void bus_ready(GObject *object, GAsyncResult *result, gpointer data) {
	GError *error = NULL;
	GDBusConnection *bus = g_bus_get_finish(result, &error);
	if (connection && bus) {
		g_object_unref(bus); /* the same connection, got early (app_window_realized) */
	} else {
		connection = bus;
	}
	if (!connection) {
		g_warning("ZacOS GTK global menus have no session bus: %s", error->message);
		g_error_free(error);
		return;
	}
	g_bus_watch_name_on_connection(connection, "com.canonical.AppMenu.Registrar",
		G_BUS_NAME_WATCHER_FLAGS_NONE, appeared, vanished, NULL, NULL);
	queue_scan();
}

G_MODULE_EXPORT void gtk_module_init(gint *argc, gchar ***argv) {
	export_key = g_quark_from_static_string("zacos9-gtk-menu-export");
	g_type_class_ref(GTK_TYPE_WIDGET);
	g_signal_add_emission_hook(g_signal_lookup("map", GTK_TYPE_WIDGET), 0, mapped, NULL, NULL);
	g_type_class_ref(GTK_TYPE_APPLICATION);
	g_signal_add_emission_hook(g_signal_lookup("window-added", GTK_TYPE_APPLICATION), 0,
		window_added, NULL, NULL);
	g_bus_get(G_BUS_TYPE_SESSION, NULL, bus_ready, NULL);
}
