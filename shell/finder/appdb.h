#pragma once

#include <QString>
#include <QStringList>
#include <cstdint>
#include <functional>
#include <vector>

/*
 * The installed GUI applications, read from their XDG desktop entries.
 *
 * Discovery and launching both go through GIO, so the desktop entry
 * specification is followed rather than approximated: XDG data directory
 * precedence (later directories don't shadow earlier ones), Hidden,
 * NoDisplay, OnlyShowIn/NotShowIn against $XDG_CURRENT_DESKTOP, TryExec,
 * Exec field codes, Path, Terminal and DBusActivatable. Flatpak and Snap
 * applications appear because they install desktop entries like any other.
 * Left out: ZacOS 9's own entries ("zacos9-*") and Wine's "Uninstall …".
 * System tools remain discoverable but belong in System Folder. Applications
 * excludes the installation baseline and automatically installed Debian apps.
 *
 * An application's identity is its desktop file ID ("firefox.desktop",
 * "org.gnome.Nautilus.desktop"), never its display name: names are
 * translated, change between releases and repeat between packages.
 */

/* One Desktop Action ("New Window", "New Private Window", ...). */
struct AppAction {
	QString id;   /* the action's key in the desktop file */
	QString name; /* its Name= */
};

struct AppEntry {
	QString id;   /* desktop file ID; the identity key */
	QString name; /* Name=, with '/' replaced so it can be a Finder name */
	/* `name`, made unique within a folder: two entries both called
	 * "Files" become "Files (org.gnome.Nautilus)" and "Files (nemo)". */
	QString folderName;
	QString comment;    /* Comment=, shown in Get Info */
	QString file;       /* the desktop file's path, shown in Get Info */
	QString commandLine;/* Exec=, shown in Get Info; never run as shell text */
	QStringList categories; /* Categories=, split on ';' */
	bool systemUtility = false;
	bool userInstalled = true;
	bool terminal = false;
	std::vector<AppAction> actions;

	/* Where this application came from, for Get Info (not shown in the
	 * Finder's pixel-measured Get Info window yet: there is no Mac OS 9
	 * reference for a Version/Package row). Empty when it can't be
	 * found: not every application has an owning package. */
	QString version;
	/* "dpkg:<package>" or "flatpak:<app id>"; empty if neither applies
	 * (a desktop entry dropped in by hand, say). Never guessed: a
	 * Flatpak export is never attributed to the "flatpak" package
	 * itself, which owns the launcher, not the application. */
	QString origin;

	/* The application's own icon, resolved through the freedesktop icon
	 * theme: straight-alpha ARGB, 32x32 and 16x16. Empty when none was
	 * found (no icon, an unreadable one, or no GUI platform to render
	 * it in, as in a headless test); the generic application icon is
	 * drawn instead. */
	std::vector<uint32_t> icon32, icon16;
};

/* Every application that should show on this desktop, sorted by name.
 * Read once and cached; re-read when the installed set changes. */
const std::vector<AppEntry> &appList();
/* The application with this desktop file ID, or nullptr. */
const AppEntry *appById(const QString &id);

/* Re-read the installed applications now. The Finder calls this when a
 * desktop entry directory changes, and from its Refresh command. */
void appRefresh();
/* Watch the XDG application directories and call `f` after a change.
 * Safe to call more than once; every callback is kept. */
void appOnChange(std::function<void()> f);

/* Launch an application by desktop file ID, or one of its Desktop
 * Actions. Returns false if the entry has gone away or won't start. */
bool appLaunch(const QString &id, const QString &actionId = QString());
/* An alias of an application is a link to its desktop file: the entry for
 * that file (resolving links), if it's one Applications shows; and
 * launching from the file itself, which works for any entry. */
const AppEntry *appByFile(const QString &desktopFile);
bool appLaunchFile(const QString &desktopFile);
/* Open a document with its registered application and launch feedback. */
bool appOpenFile(const QString &path);
