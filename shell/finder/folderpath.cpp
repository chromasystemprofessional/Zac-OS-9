#include "folderpath.h"

#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>

#include "items.h"
#include "vfs.h"

QString enclosingFolder(const QString &path) {
	if (vfsIsVirtual(path)) {
		if (vfsIsZacosVolumeRoot(path)) {
			return QString(); /* a mounted ZacOS 9 disk is its own top */
		}
		const QString id = vfsIdOf(path);
		if (id.isEmpty()) {
			return QString(); /* the startup disk itself */
		}
		const int slash = id.lastIndexOf('/');
		return slash < 0 ? vfsRoot() : vfsPathFor(id.left(slash));
	}
	const QString clean = QDir::cleanPath(path);
	if (clean == "/" || QDir(clean) == QDir(trashFilesPath())) {
		return QString();
	}
	/* A real folder the Macintosh view stands for goes up through the view. */
	const QString node = vfsPathBacking(clean);
	if (!node.isEmpty()) {
		return enclosingFolder(node);
	}
	/* The top of another disk (a USB disk, a server's volume). */
	if (QStorageInfo(clean).rootPath() == clean) {
		return QString();
	}
	return QFileInfo(clean).absolutePath();
}

QStringList folderChain(const QString &path) {
	QStringList chain;
	for (QString p = path; !p.isEmpty() && chain.size() < 64; p = enclosingFolder(p)) {
		/* A real folder the view stands for shows as it (Home, not "adam"). */
		const QString shown = vfsIsVirtual(p) ? QString() : vfsPathBacking(p);
		const QString entry = shown.isEmpty() ? p : shown;
		if (chain.contains(entry)) {
			break;
		}
		chain << entry;
	}
	return chain;
}

QString folderTitle(const QString &path) {
	if (path == vfsRoot()) {
		return vfsVolumeName();
	}
	return displayName(path);
}
