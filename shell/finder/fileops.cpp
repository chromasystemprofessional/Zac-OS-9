#include "fileops.h"

#include <QDir>
#include <QDrag>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QPixmap>
#include <QStorageInfo>
#include <QUrl>
#include <QWidget>

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
		bool ok = copy ? copyRecursively(srcAbs, target) : QDir().rename(srcAbs, target);
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
	 * hand to the drop: an application folder stands for a package. */
	for (const Item *item : items) {
		if (item->isVirtual) {
			return;
		}
	}
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
	QList<QUrl> urls;
	for (Item *item : items) {
		urls << QUrl::fromLocalFile(item->path);
	}
	mime->setUrls(urls);

	auto *drag = new QDrag(source);
	drag->setMimeData(mime);
	drag->setPixmap(pixmap);
	drag->setHotSpot(pointer - bounds.topLeft());
	drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);
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
		for (const QString &d : changed) {
			finder.folderChanged(d);
		}
		finder.folderChanged(trashFilesPath());
		e->acceptProposedAction();
		return;
	}
	QString dest = target ? target->path : folder;
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
