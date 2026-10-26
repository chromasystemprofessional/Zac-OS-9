#ifndef ZACOS9_ICONS_H
#define ZACOS9_ICONS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Finder icons: ZacOS 9's own 32x32 and 16x16 pixel art
 * (assets/icons/platinum-icons.picon).
 */

#include "draw.h"

enum pl_icon_kind {
	PL_ICON_FOLDER,
	PL_ICON_DOCUMENT,
	PL_ICON_APPLICATION,
	PL_ICON_DISK,
	PL_ICON_TRASH_EMPTY,
	PL_ICON_TRASH_FULL,
	PL_ICON_CAUTION,    /* alerts */
	PL_ICON_DISK_IMAGE, /* a Macintosh disk or CD image */
	PL_ICON_CLASSIC,    /* a classic Mac OS application (runs in Classic) */
	PL_ICON_WINDOWS,    /* a Windows application (runs in Wine) */
	PL_ICON_SHARED_FOLDER, /* a folder shared from Get Info */
	PL_ICON_SYSTEM_FOLDER, /* the System Folder, with Zac on it */
	PL_ICON_CONTROL_PANELS, /* the Control Panels folder */
	/* System extensions shown during the startup parade. */
	PL_ICON_EXT_OPENTRANSPORT,
	PL_ICON_EXT_APPLETALK,
	PL_ICON_EXT_BLUETOOTH,
	PL_ICON_EXT_AUDIO,
	PL_ICON_EXT_PRINTMONITOR,
	/* Extensions Manager: a kind of extension each. */
	PL_ICON_EXT_GENERIC,
	PL_ICON_EXT_DISPLAY,
	PL_ICON_EXT_ETHERNET,
	PL_ICON_EXT_WIFI,
	PL_ICON_EXT_USB,
	PL_ICON_EXT_INPUT,
	PL_ICON_EXT_CAMERA,
	PL_ICON_EXT_POWER,
	PL_ICON_EXT_SENSOR,
	PL_ICON_EXT_SECURITY,
	PL_ICON_EXT_CHIP,
	PL_ICON_CLIPPING,          /* text or a picture dragged out of a document */
	PL_ICON_INTERNET_LOCATION, /* a link dragged out of a browser */
	PL_ICON_PRINTER,           /* a desktop printer */
	/* Control panels' own (the Zac menu's Control Panels). */
	PL_ICON_CP_MOUSE,
	PL_ICON_CP_KEYBOARD,
	PL_ICON_CP_TRACKPAD,
	PL_ICON_CP_ACCESSIBILITY,
	PL_ICON_CP_GENERAL,
	PL_ICON_CP_NUMBERS,
	PL_ICON_CP_TEXT,
	PL_ICON_CP_DATETIME,
	PL_ICON_CP_ZACMENU,
	PL_ICON_CP_APPEARANCE,
	PL_ICON_CP_SCREENSAVER,
	PL_ICON_CP_USERS,
	PL_ICON_CP_KEYCHAIN,
	PL_ICON_CP_COLLAR,
	PL_ICON_CP_SPEECH,
	PL_ICON_CP_RETRIEVER,
	PL_ICON_CP_FILETRANSLATOR,
	PL_ICON_CP_WALKIES,
	PL_ICON_CP_MEMORY,
	PL_ICON_NETWORK_DISK, /* a network volume (AFP, SMB), as AppleShare's were */
	PL_ICON_FONT,         /* a font: a suitcase, "A" drawn on it in the font */
	PL_ICON_APPLICATIONS_FOLDER, /* Applications: a folder with a bone */
	PL_ICON_HOME_FOLDER,         /* Home: Zac in his doghouse */
	/* The System Folder's folders, each with its emblem. */
	PL_ICON_SF_APPEARANCE,
	PL_ICON_SF_EXTENSIONS,
	PL_ICON_SF_SOUNDS,
	PL_ICON_SF_STARTUP_ITEMS,
	PL_ICON_SF_APPLICATION_SUPPORT,
	PL_ICON_SF_DEVICE_DRIVERS,
	PL_ICON_SF_NETWORK,
	PL_ICON_SF_SYSTEM_LOGS,
	PL_ICON_SF_SOFTWARE_COMPONENTS,
	PL_ICON_SF_FONTS,
	PL_ICON_SF_FAVORITES,
	PL_ICON_SF_TOY_BOX_ITEMS,
	PL_ICON_SF_FOLDER_TRICKS,
	PL_ICON_SF_CONTEXTUAL_MENU_ITEMS,
	PL_ICON_SF_PREFERENCES,
	PL_ICON_SF_UTILITIES,
	/* The Zac menu's desk accessories. */
	PL_ICON_APP_BARKRECORDER,
	PL_ICON_APP_CALCULATOR,
	PL_ICON_APP_DOGEARS,
	PL_ICON_APP_JIGSAW,
	PL_ICON_APP_KEYTAGS,
	PL_ICON_APP_NOTEPAD,
	PL_ICON_APP_PEDIGREE,
	PL_ICON_APP_PLOTTER,
	PL_ICON_APP_STASH,
	/* One extension's own icon each (tools/extension-icons). */
#include "icons_custom.h"
	PL_ICON_COUNT,
};

#define PL_ICON_LARGE 32
#define PL_ICON_SMALL 16

/* size x size ARGB pixels, alpha 0 or 255. size is 32 or 16. */
const uint32_t *pl_icon(enum pl_icon_kind kind, int size);

/* A ZacOS program's own icon, from its desktop file's name or path
 * ("zacos9-calculator", ".../zacos9-calculator.desktop");
 * PL_ICON_APPLICATION for any other. */
enum pl_icon_kind pl_icon_for_program(const char *desktop_id);

/* Draw an icon; selected icons are darkened, as the Finder does.
 * TODO: measure the exact selected transform. */
void pl_icon_paint(struct pl_canvas *c, int x, int y, enum pl_icon_kind kind,
		int size, bool selected);
/* The same, tinted with a Finder label color (0 = no label). */
void pl_icon_paint_label(struct pl_canvas *c, int x, int y, enum pl_icon_kind kind,
		int size, bool selected, uint32_t label_color);

/*
 * Finder labels: index 0 is "None", 1..7 the Mac OS 8 defaults (Label
 * control panel), colors from the standard 16-color system palette.
 */
#define PL_LABEL_COUNT 8
struct pl_label {
	const char *name;
	uint32_t color; /* ARGB; 0 for None */
};
extern const struct pl_label pl_labels[PL_LABEL_COUNT];

#ifdef __cplusplus
}
#endif

#endif
