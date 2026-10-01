#ifndef PLATINUM_MENUBAR_H
#define PLATINUM_MENUBAR_H

#include <stdbool.h>
#include <wayland-client.h>

#include "menudraw.h"
#include "text.h"

#define MAX_ITEMS 48
#define FINDER_APP_ID "platinum-finder"
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
};

/* ⌘⌫ (delete) as a menu shortcut. */
#define KEY_DELETE_GLYPH '\b'

struct mb_item {
	char *label;  /* NULL = separator */
	char key;     /* ⌘ shortcut, or 0 */
	bool enabled, checked;
	enum action action;
	char *arg;
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
	struct mbar_title left_titles[MAX_TITLES], right_titles[2];
};

void menus_rebuild(struct mb_bar *bar, int screen_w);
/* Clicking the clock flips it between the time and the date. */
void menus_toggle_clock(void);
void menus_free(struct mb_bar *bar);
void menus_perform(struct mb_item *item);

/* ---- apple-menu-equivalent items and launching (launch.c) ---------------- */

void launch(const char *command);
/* Fills the logo menu with "About", a separator and the launchable items
 * from ~/.config/platinum/Platinum Menu Items (or built-in defaults). */
void launch_fill_logo_menu(struct mb_menu *menu);

/* ---- the Finder (finderlink.c) ------------------------------------------ */

struct finder_state {
	bool connected;
	int selection, window, trash;
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

/* MBAR_ICON_SIZE² ARGB pixels. */
const uint32_t *logo_pixels(void);

/* ---- app-wide state (main.c) ---------------------------------------------- */

struct wl_seat *menubar_seat(void);
/* Called when the set of apps or the front app changes. */
void menubar_apps_changed(void);

#endif
