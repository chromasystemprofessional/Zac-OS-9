#pragma once

#include <QList>
#include <QString>

/*
 * The Internet panel's settings, kept where every Linux program looks:
 * default applications in mimeapps.list (through GIO), the downloads folder
 * in user-dirs.dirs (XDG_DOWNLOAD_DIR).
 */
struct DefaultApp {
	QString id;   /* desktop file ID: "firefox-esr.desktop" */
	QString name; /* as listed */
};

enum class AppRole { WebBrowser, Mail };

/* The applications that can be the default, by name, and the current one
 * (empty if none is set or it's gone). */
QList<DefaultApp> defaultAppChoices(AppRole role);
QString defaultApp(AppRole role);
/* Makes `id` the default for every type the role covers (a browser: http,
 * https and HTML; mail: mailto). False if it couldn't. */
bool setDefaultApp(AppRole role, const QString &id);

/* The downloads folder: XDG_DOWNLOAD_DIR from user-dirs.dirs, else
 * ~/Downloads. Setting it writes the file (keeping its other lines). */
QString downloadFolder();
bool setDownloadFolder(const QString &path);
