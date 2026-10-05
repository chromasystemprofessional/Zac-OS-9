#include "fileops.h"
#include "platinumshell.h"
#include "settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QDrag>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QPixmap>
#include <QStorageInfo>
#include <QUrl>
#include <QWidget>
#include <algorithm>

#include "alert.h"
#include "finder.h"
#include "vfs.h"

static bool sameDisk(const QString &a, const QString &b) {
	return QStorageInfo(a).device() == QStorageInfo(b).device();
}

static bool copyRecursively(const QString &src, const QString &dst) {
	QFileInfo info(src);
	if (!info.isDir() || info.isSymLink()) {
		return QFile::copy(src, dst);
	}
	if (!QDir().mkpath(dst)) {
		return false;
	}
	QDir dir(src);
	const auto entries = dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot |
		QDir::Hidden | QDir::System);
	for (const QString &name : entries) {
		if (!copyRecursively(dir.filePath(name), dst + "/" + name)) {
			return false;
		}
	}
	return true;
}

/* "Report copy", "Report copy 2", ... (Mac OS 8 naming). */
static QString copyName(const QDir &dir, const QString &name) {
	QString base = name + " copy";
	if (!dir.exists(base)) {
		return base;
	}
	for (int i = 2;; i++) {
		QString candidate = base + " " + QString::number(i);
		if (!dir.exists(candidate)) {
			return candidate;
		}
	}
}

QStringList transferItems(const QStringList &paths, const QString &destDir, bool forceCopy) {
	QStringList changed;
	const QDir dest(destDir);
	const QString destAbs = dest.absolutePath();
	for (const QString &src : paths) {
		QFileInfo info(src);
		const QString srcAbs = info.absoluteFilePath();
		/* A folder can't go inside itself. */
		if (destAbs == srcAbs || destAbs.startsWith(srcAbs + "/")) {
			continue;
		}
		const bool sameFolder = info.absolutePath() == destAbs;
		const bool copy = forceCopy || !sameDisk(srcAbs, destAbs);
		if (sameFolder && !copy) {
			continue; /* dropped where it already is */
		}
		QString name = info.fileName();
		if (sameFolder) {
			name = copyName(dest, name);
		} else if (dest.exists(name)) {
			continue;
		}
		const QString target = dest.filePath(name);
		const QString resource = info.absolutePath() + "/._" + info.fileName();
		const QString resourceTarget = dest.filePath("._" + name);
		const bool resourceFork = info.isFile() && !info.isSymLink() &&
			!info.fileName().startsWith("._") && QFileInfo::exists(resource);
		if (resourceFork && (QFileInfo(resource).isSymLink() || !QFileInfo(resource).isFile() ||
				QFileInfo::exists(resourceTarget))) {
			Alert::ask("The resource fork for “" + info.fileName() +
				"” cannot be transferred safely; the file has not been moved.", "OK", QString());
			continue;
		}
		bool ok = copy ? copyRecursively(srcAbs, target) : QDir().rename(srcAbs, target);
		if (ok && resourceFork) {
			ok = copy ? QFile::copy(resource, resourceTarget) : QDir().rename(resource, resourceTarget);
			if (!ok) {
				const bool restored = copy ? QFile::remove(target) : QDir().rename(target, srcAbs);
				Alert::ask(restored
					? "The resource fork could not be transferred. The original file is unchanged."
					: "The resource fork could not be transferred, and the file could not be restored. "
					  "Check both the source and destination folders.", "OK", QString());
			}
		}
		if (ok) {
			changed << destAbs;
			if (!copy) {
				changed << info.absolutePath();
			}
		}
	}
	changed.removeDuplicates();
	return changed;
}

QStringList draggedPaths(const QMimeData *mime) {
	QStringList paths;
	for (const QUrl &url : mime->urls()) {
		if (url.isLocalFile()) {
			paths << url.toLocalFile();
		}
	}
	return paths;
}

/* Gray dotted outline of an icon's silhouette and its label box. */
static void outlineItem(pl_canvas *c, Item *item, int ox, int oy) {
	const uint32_t *px = pl_icon(item->kind, PL_ICON_LARGE);
	auto opaque = [&](int x, int y) {
		return x >= 0 && y >= 0 && x < PL_ICON_LARGE && y < PL_ICON_LARGE &&
			(px[y * PL_ICON_LARGE + x] >> 24);
	};
	for (int y = 0; y < PL_ICON_LARGE; y++) {
		for (int x = 0; x < PL_ICON_LARGE; x++) {
			bool edge = opaque(x, y) &&
				(!opaque(x - 1, y) || !opaque(x + 1, y) || !opaque(x, y - 1) || !opaque(x, y + 1));
			if (edge && (x + y) % 2 == 0) {
				pl_put(c, ox + x, oy + y, GRAY(0x5));
			}
		}
	}
}

void startItemDrag(QWidget *source, const std::vector<Item *> &items,
		const std::vector<QPoint> &itemOrigins, QPoint pointer) {
	if (items.empty()) {
		return;
	}
	/* Nothing in the Macintosh view is a file, so there is nothing to
	 * hand to a drop: an application stands for a package. Such icons
	 * can still be moved about their own window (ICON_MOVE_MIME). */
	const bool anyVirtual = std::any_of(items.begin(), items.end(),
		[](const Item *item) { return item->isVirtual; });
	QRect bounds;
	for (const QPoint &o : itemOrigins) {
		bounds = bounds.united(QRect(o, QSize(PL_ICON_LARGE, PL_ICON_LARGE)));
	}
	Pixels img(bounds.width(), bounds.height());
	for (size_t i = 0; i < items.size(); i++) {
		outlineItem(&img.c, items[i], itemOrigins[i].x() - bounds.x(),
			itemOrigins[i].y() - bounds.y());
	}

	const qreal dpr = source->devicePixelRatioF();
	QPixmap pixmap = QPixmap::fromImage(img.img.scaled(
		qRound(bounds.width() * dpr), qRound(bounds.height() * dpr)));
	pixmap.setDevicePixelRatio(dpr);

	auto *mime = new QMimeData;
	if (anyVirtual) {
		mime->setData(ICON_MOVE_MIME, QByteArray());
	} else {
		QList<QUrl> urls;
		for (Item *item : items) {
			urls << QUrl::fromLocalFile(item->path);
		}
		mime->setUrls(urls);
	}

	auto *drag = new QDrag(source);
	drag->setMimeData(mime);
	drag->setPixmap(pixmap);
	drag->setHotSpot(pointer - bounds.topLeft());
	const Qt::DropAction done = drag->exec(
		anyVirtual ? Qt::MoveAction : Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);

	/* Their own window always takes such a drag back; anywhere else
	 * leaves it nowhere to go, so say why rather than do nothing. */
	if (anyVirtual && done == Qt::IgnoreAction) {
		const Item *first = *std::find_if(items.begin(), items.end(),
			[](const Item *item) { return item->isVirtual; });
		if (vfsIsAppFolder(first->path)) {
			Alert::ask("“" + first->name + "” stays in the Applications folder. "
				"To remove an application, select it and choose Move To Trash.", "OK", QString());
		} else {
			Alert::ask("“" + first->name + "” is part of this computer and can't be moved "
				"out of its window.", "OK", QString());
		}
	}
}

/* A program installed next to the Finder (build tree), else on $PATH. */
static QString sibling(const QString &program) {
	const QString local = QCoreApplication::applicationDirPath() + "/" + program;
	return QFileInfo(local).isExecutable() ? local : program;
}

/* Files dropped onto Applications: install them rather than move them.
 * Windows ones go to the Windows Installer, the rest to the App Installer,
 * which also says why it can't install any that aren't installers (an
 * alert of the Finder's own, opened from a drag that began on the desktop,
 * gets no input - see PROJECT_STATUS.md). */
static void installIntoApplications(const QStringList &paths) {
	QStringList rest;
	for (const QString &p : paths) {
		const QString name = QFileInfo(p).fileName().toLower();
		if (name.endsWith(".exe") || name.endsWith(".msi")) {
			platinumStartApplication(sibling("zacos9-wininstall"), { p });
		} else {
			rest << p;
		}
	}
	if (!rest.isEmpty()) {
		platinumStartApplication(sibling("zacos9-appinstall"), rest);
	}
}

void dropItems(QDropEvent *e, const Item *target, const QString &folder) {
	const QStringList paths = draggedPaths(e->mimeData());
	if (paths.isEmpty()) {
		return;
	}
	Finder &finder = Finder::instance();
	const bool toTrash = target &&
		(target->kind == PL_ICON_TRASH_EMPTY || target->kind == PL_ICON_TRASH_FULL);
	if (toTrash) {
		QStringList changed;
		for (const QString &p : paths) {
			if (QFile::moveToTrash(p)) {
				changed << QFileInfo(p).absolutePath();
			}
		}
		changed.removeDuplicates();
		if (!changed.isEmpty()) {
			pl_sound_event("trash-move");
		}
		for (const QString &d : changed) {
			finder.folderChanged(d);
		}
		finder.folderChanged(trashFilesPath());
		e->acceptProposedAction();
		return;
	}
	QString dest = target ? target->path : folder;
	if (vfsIsApplications(dest)) {
		installIntoApplications(paths);
		/* Copy: the install file stays where it was. */
		e->setDropAction(Qt::CopyAction);
		e->accept();
		return;
	}
	if (vfsIsVirtual(dest)) {
		/* Documents and the other folders standing for real directories
		 * take files; the curated ones have nowhere to put them. */
		dest = vfsOpensAs(dest);
		if (dest.isEmpty()) {
			e->ignore();
			return;
		}
	}
	const bool option = e->modifiers() & Qt::AltModifier;
	for (const QString &d : transferItems(paths, dest, option)) {
		finder.folderChanged(d);
	}
	e->setDropAction(option ? Qt::CopyAction : Qt::MoveAction);
	e->accept();
}
