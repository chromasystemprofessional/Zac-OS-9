#pragma once

/*
 * File associations: which application opens a file of each type.
 *
 * The Finder doesn't keep a table of its own. Types are shared-mime-info
 * MIME types (QMimeDatabase, the same database GIO reads), and the
 * application for each is the freedesktop default: GIO resolves it from
 * the mimeapps.list files (user, desktop-specific, system) and the
 * desktop entries' MimeType= keys, and the File Exchange control panel
 * changes it through GIO, which writes the user's
 * $XDG_CONFIG_HOME/mimeapps.list. Opening a file (appOpenFile) asks GIO
 * for the same default, so what a document's icon shows is what opens it.
 */

#include <QString>
#include <QStringList>
#include <cstdint>
#include <functional>
#include <vector>

class QWidget;

/* The file's MIME type; an alias reports its original's. Empty for a
 * folder, or an alias whose original is missing. */
QString fileMimeType(const QString &path);

/* The extension the Finder hides from `fileName` (".txt", ".tar.gz"),
 * or empty when it shows the name whole: only a suffix shared-mime-info
 * knows is hidden, and never one that would leave nothing in front. */
QString hiddenExtension(const QString &fileName);
/* `fileName` without its hidden extension. */
QString nameWithoutExtension(const QString &fileName);

/* The desktop file ID ("org.gnome.TextEditor.desktop") of the application
 * that opens `mimeType`, or empty if none does. Cached until the
 * association files or the installed applications change. */
QString defaultAppFor(const QString &mimeType);
/* Every application that says it can open `mimeType`, default first. */
QStringList appsFor(const QString &mimeType);
/* Make `appId` the default for `mimeType` (the user's mimeapps.list). */
bool setDefaultApp(const QString &mimeType, const QString &appId, QString *error = nullptr);

/* An application's name and icon by desktop file ID, for apps the Finder's
 * own catalogue leaves out (NoDisplay ones can still open files). The icon
 * is straight-alpha ARGB at `size`, empty if none resolves. */
QString appDisplayName(const QString &appId);
std::vector<uint32_t> appIconPixels(const QString &appId, int size);

/* One row of the File Exchange panel. */
struct FileType {
	QString mimeType;
	QString description; /* "PNG image" */
	QStringList extensions; /* "png", without dots */
};
/* Every type some installed application can open, by description. */
std::vector<FileType> openableFileTypes();

/* Bumped whenever an association or the set of applications changes. */
unsigned fileAssocGeneration();
/* Repaint `widget` when associations change, and call `f`. */
void watchFileAssociations(QWidget *widget);
void fileAssocOnChange(std::function<void()> f);
/* Forget everything cached and repaint (the application list changed). */
void fileAssocInvalidate();
