#ifndef ZACOS9_MENUBAR_H
#define ZACOS9_MENUBAR_H

#include <stdbool.h>
#include <wayland-client.h>

#include "menudraw.h"
#include "text.h"

#define MAX_ITEMS 48
#define FINDER_APP_ID "zacos9-finder"
#define MAX_TITLES 12

struct zwlr_foreign_toplevel_handle_v1;
struct zwlr_foreign_toplevel_manager_v1;
struct zwp_virtual_keyboard_manager_v1;

/* ---- running apps (toplevels.c) ----------------------------------------- */

struct toplevel {
	struct wl_list link;
	struct zwlr_foreign_toplevel_handle_v1 *handle;
	char *title, *app_id;
	bool activated, minimized;
	/* Pending state, applied on "done". */
	char *p_title, *p_app_id;
	bool p_activated, p_minimized;
	unsigned last_active; /* activation counter, for ordering */
};

struct app {
	char *app_id;
	char *name;  /* display name from its .desktop file, else app_id */
	bool active; /* one of its windows is focused */
	bool hidden; /* every window minimized */
};

void toplevels_init(struct zwlr_foreign_toplevel_manager_v1 *mgr);
struct wl_list *toplevels_list(void);
/* Distinct apps, front-most first. Returns the count; apps[] valid until
 * the next call. */
int apps_collect(struct app *apps, int max);
const char *app_display_name(const char *app_id);
void app_activate(const char *app_id, struct wl_seat *seat);
void app_set_hidden(const char *app_id, bool hidden);
void app_close_front_window(const char *app_id);
void app_quit(const char *app_id);

/* ---- menus (menus.c) ----------------------------------------------------- */

enum action {
	ACT_NONE,
	ACT_LAUNCH,       /* arg: shell command */
	ACT_CLOSE_WINDOW,
	ACT_QUIT_APP,
	ACT_HIDE_APP,
	ACT_HIDE_OTHERS,
	ACT_SHOW_ALL,
	ACT_ACTIVATE_APP, /* arg: app id */
	ACT_ABOUT,
	ACT_EDIT_COMMAND, /* key: the ⌘ letter, sent to the front app */
	ACT_EDIT_CLEAR,
	ACT_FINDER,       /* arg: Finder command name */
	ACT_DBUSMENU,     /* arg: dbusmenu "sender path id" or opaque GTK route */
};

/* ⌘⌫ (delete) as a menu shortcut. */
#define KEY_DELETE_GLYPH '\b'

struct mb_menu;

struct mb_item {
	char *label;  /* NULL = separator */
	char key;     /* ⌘ shortcut, or 0 */
	bool enabled, checked;
	enum action action;
	char *arg;
	struct mb_menu *submenu; /* hierarchical menu (owned), or NULL */
	uint32_t swatch;         /* label color square, or 0 */
	struct plat_text *label_text, *key_text;
};

struct mb_menu {
	char *title;            /* NULL for an icon title */
	const uint32_t *icon;
	struct mb_item items[MAX_ITEMS];
	int n;
	struct plat_text *title_text;
};

/* The menus currently on the bar, rebuilt when the front app changes. */
struct mb_bar {
	struct mb_menu left[MAX_TITLES];   /* logo menu, then app menus */
	int n_left;
	struct mb_menu right[2];           /* clock (no menu), Application menu */
	int n_right;
	bool has_clock;                    /* right[0] is the clock */
	struct mbar_title left_titles[MAX_TITLES], right_titles[2];
};

void menus_rebuild(struct mb_bar *bar, int screen_w);
/* Clicking the clock flips it between the time and the date. */
void menus_toggle_clock(void);
void menus_free(struct mb_bar *bar);
void menus_perform(struct mb_item *item);
/* Building blocks for menus filled elsewhere (appmenu.c). */
struct mb_menu *menus_new_menu(struct mb_menu *menus, int *n, const char *title);
struct mb_item *menus_add_item(struct mb_menu *m, const char *label, char key,
	bool enabled, enum action action, const char *arg);

/* ---- other programs' menus (appmenu.c) ------------------------------------ */

struct pollfd;

/* Owns com.canonical.AppMenu.Registrar and prepares GTK menu integration.
 * Focus metadata is supplied with the setters below after initialization. */
void appmenu_init(void);
void appmenu_set_active_pid(uint32_t pid);
/* GTK exports of the focused surface: bus_name must be a unique name and
 * paths must be object paths. The bus owner is asynchronously checked
 * against active_pid before accepting exports. Empty bus/menu paths clear
 * GTK selection; unavailable exports fall back to the registrar. Strings
 * are copied and may be supplied before appmenu_init(). */
void appmenu_set_active_gtk(const char *bus_name, const char *app_menu_path,
	const char *menubar_path, const char *window_path, const char *application_path);
/* Appends the front program's own menus; false if it has none. */
bool appmenu_add_menus(struct mb_menu *menus, int *n, int max);
void appmenu_perform(const char *arg);
/* GLib's main context in our poll() loop: fills up to max pollfds and
 * may shorten the timeout; dispatch gets the same fds back after poll(). */
int appmenu_poll_prepare(struct pollfd *fds, int max, int *timeout);
void appmenu_poll_dispatch(const struct pollfd *fds);

/* ---- apple-menu-equivalent items and launching (launch.c) ---------------- */

void launch(const char *command);
uint32_t launch_feedback_begin(void);
void launch_feedback_update(uint32_t cookie, uint32_t pid);
void launch_feedback_cancel(uint32_t cookie);
/* Fills the logo menu with "About", a separator and the launchable items
 * from ~/.config/zacos9/ZacOS 9 Menu Items (or built-in defaults). */
void launch_fill_logo_menu(struct mb_menu *menu);

/* ---- the Finder (finderlink.c) ------------------------------------------ */

struct finder_state {
	bool connected;
	int selection, window, trash, view;
	int label; /* the selection's common label index, or -1 */
	int erase; /* exactly one mounted USB disk is selected */
};

bool finder_connect(void);
int finder_fd(void);          /* -1 when not connected */
bool finder_read(void);       /* true if the state changed */
const struct finder_state *finder_state(void);
void finder_send(const char *command);

/* ---- keystrokes for Edit commands (keys.c) -------------------------------- */

void keys_init(struct zwp_virtual_keyboard_manager_v1 *mgr, struct wl_seat *seat);
bool keys_available(void);
void keys_send_command(char key); /* ⌘key, delivered as Ctrl+key */
void keys_send_clear(void);       /* Clear = Delete */

/* ---- the logo (logo.c) --------------------------------------------------- */

/* The logo comes from lib/logo.h. */
#include "logo.h"

/* ---- app-wide state (main.c) ---------------------------------------------- */

struct wl_seat *menubar_seat(void);
/* Called when the set of apps or the front app changes. */
void menubar_apps_changed(void);

#endif
