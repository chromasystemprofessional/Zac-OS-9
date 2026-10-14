#pragma once

#include <QImage>
#include <QString>
#include <functional>
#include <vector>

/*
 * What the Pack (Mac OS 9's Application palette) lists: one tile per
 * running program, by name, the Sniffer always among them, as the
 * Application menu has them.
 */
inline constexpr char SNIFFER_APP_ID[] = "zacos9-finder";

/* A window, as zacos9-wm reports it (wlr-foreign-toplevel). */
struct WindowInfo {
	QString appId, title;
	bool active = false, minimized = false;
};

struct PackApp {
	QString appId, name;
	QString desktopFile; /* empty if none was found */
	QString iconName;
	bool active = false; /* in front */
	bool hidden = false; /* every window hidden */
};

/* A program's name, icon and desktop file, from its app id; false if no
 * desktop file names it. */
struct AppDetails {
	QString name, iconName, desktopFile;
};
using AppLookup = std::function<bool(const QString &appId, AppDetails *details)>;
bool lookUpApp(const QString &appId, AppDetails *details); /* through GIO */

/* The tiles, alphabetical by name; the Sniffer in front when no window is. */
std::vector<PackApp> packApps(const std::vector<WindowInfo> &windows, const AppLookup &lookup);

/* An app's icon at `size` pixels: its theme icon, the Sniffer's logo, or
 * the generic application icon. */
QImage appIcon(const PackApp &app, int size);

/* Opens `files` with the program (dropped on its tile). */
bool openWithApp(const PackApp &app, const QStringList &files);
