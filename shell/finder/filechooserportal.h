#pragma once

#include <QImage>
#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

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
QString fileChooserRealPath(const QString &path);
/* Loads the Finder's Macintosh view ahead of the first request. */
void fileChooserWarmUp();
/* Rebuilds the startup disk's chooser tree under `parent` (replacing it). */
FileChooserLocation fileChooserStartupDisk(const QString &parent);
