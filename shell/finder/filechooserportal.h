#pragma once

#include <QImage>
#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

class QFileDialog;

struct FileChooserLocation {
	QString name;
	QString path;
	bool usb = false;
	/* The startup disk is the Finder's Macintosh view: a read-only tree of
	 * folders and links. These are the real folders/files behind its links,
	 * the only places that can be chosen. Empty for an ordinary drive. */
	QStringList realRoots;
	bool folderAlias = false;
	QString error = {};
};

QList<FileChooserLocation> fileChooserLocations();
QList<FileChooserLocation> fileChooserNetworkLocations(
	const QByteArray &mountInfo, const QString &runtime);
void prepareFileChooserDialog(QFileDialog &dialog);
bool registerFileChooserPortal();
/* The Finder desktop's disk icon, at 32x32 1x pixels. */
QImage fileChooserDriveIcon(bool usb, bool selected);
bool fileChooserPathWithinDrive(const QString &path, const QString &root);
/* `browsing` also admits the startup disk's own (virtual) folders. */
bool fileChooserPathWithinDisk(const QString &path, const QString &root,
	const QStringList &realRoots, bool browsing);
QString fileChooserDroppedFolder(const QList<QUrl> &urls, const QString &root,
	const QStringList &realRoots);
QString fileChooserRealPath(const QString &path);
/* General Controls' Documents: the folder an Open or Save dialog for
 * `appId` starts in ("last": the one last used in that program; "documents":
 * the Documents folder, made if missing), or empty to start from the list
 * of disks, as for "Folder set by the application" (programs' own
 * suggestions never skip it). */
QString fileChooserStartFolder(const QString &appId);
/* The folder last used in `appId`'s dialog, for "last". */
void fileChooserRememberFolder(const QString &appId, const QString &folder);
QStringList fileChooserRecentFolders();
QStringList fileChooserRecentFolders(const FileChooserLocation &location);
void fileChooserRememberRecentFolder(const QString &folder);
/* The location among `locations` holding the real folder `folder`, and in
 * *browse the path to it there (on the startup disk, through its links);
 * an empty location if none does. */
FileChooserLocation fileChooserLocationFor(const QList<FileChooserLocation> &locations,
	const QString &folder, QString *browse);
/* Mac OS 9's Recent pop-up: documents and folders used lately, newest
 * first (recently-used.xbel), at most Zac Menu Options' count of recent
 * documents; gone ones and other computers' are left out. */
QStringList fileChooserRecentItems();
/* Loads the Finder's Macintosh view ahead of the first request. */
void fileChooserWarmUp();
/* Rebuilds the startup disk's chooser tree under `parent` (replacing it). */
FileChooserLocation fileChooserStartupDisk(const QString &parent);
