#include <gtk/gtk.h>

static gboolean parse_failed;

static void on_parsing_error(GtkCssProvider *provider, GtkCssSection *section,
		const GError *error, gpointer data) {
	(void)provider;
	(void)section;
	(void)data;
	g_printerr("GTK theme CSS parse error: %s\n", error->message);
	parse_failed = TRUE;
}

int main(int argc, char **argv) {
	if (argc != 2) {
		g_printerr("usage: test-gtk-theme GTK_CSS_PATH\n");
		return 2;
	}
	gchar *source = NULL;
	gsize length = 0;
	GError *error = NULL;
	if (!g_file_get_contents(argv[1], &source, &length, &error)) {
		g_printerr("Could not read GTK theme CSS: %s\n", error->message);
		g_clear_error(&error);
		return 1;
	}
	GString *css = g_string_new(NULL);
	gchar **lines = g_strsplit(source, "\n", -1);
	for (gchar **line = lines; *line; ++line) {
		if (!g_str_has_prefix(*line, "@import")) {
			g_string_append(css, *line);
			g_string_append_c(css, '\n');
		}
	}
	g_strfreev(lines);
	g_free(source);

	GtkCssProvider *provider = gtk_css_provider_new();
	g_signal_connect(provider, "parsing-error", G_CALLBACK(on_parsing_error), NULL);
	if (!gtk_css_provider_load_from_data(provider, css->str, css->len, &error)) {
		g_printerr("Could not parse GTK theme CSS: %s\n", error->message);
		g_clear_error(&error);
		parse_failed = TRUE;
	}
	g_object_unref(provider);
	g_string_free(css, TRUE);
	return parse_failed ? 1 : 0;
}
