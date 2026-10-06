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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QWidget>
#include <QThread>
#include <QTimer>
#include <QScopedValueRollback>
#include <QSet>
#include <algorithm>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

#include "alert.h"
#include "finder.h"
#include "vfs.h"
#include "transferdialog.h"

static bool sameDisk(const QString &a, const QString &b) {
	return QStorageInfo(a).device() == QStorageInfo(b).device();
}

struct TransferState {
	std::atomic_bool stopped{false};
	std::mutex mutex;
	QString operation = "Copying", file;
	quint64 completed = 0, total = 0;
	bool preparing = true;
	QStringList errors, changed;

	void current(const QString &path, const QString &action) {
		std::lock_guard<std::mutex> lock(mutex);
		file = QFileInfo(path).fileName();
		operation = action;
	}
	void advance(quint64 amount) {
		std::lock_guard<std::mutex> lock(mutex);
		completed += amount;
	}
};

static bool countTransfer(const QString &path, TransferState &state, quint64 &units) {
	if (state.stopped.load()) {
		return false;
	}
	QFileInfo info(path);
	state.current(path, "Copying");
	if (info.isSymLink()) {
		units++;
		return true;
	}
	if (info.isFile()) {
		units += static_cast<quint64>(info.size()) + 1;
		return true;
	}
	if (!info.isDir() || !info.isReadable()) {
		state.errors << "“" + info.fileName() + "” cannot be read or is not an ordinary file or folder.";
		return false;
	}
	units++;
	const QDir dir(path);
	for (const auto &entry : dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot |
			QDir::Hidden | QDir::System)) {
		if (!countTransfer(dir.filePath(entry), state, units)) {
			return false;
		}
	}
	return true;
}

/* Roll back only paths this copy created; never traverse directory links. */
static bool removeCopy(const QString &path) {
	const QFileInfo info(path);
	if (info.isSymLink() || !info.isDir()) {
		return QFile::remove(path);
	}
	if (!QFile::setPermissions(path, info.permissions() |
			QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
		return false;
	}
	const QDir dir(path);
	bool removed = true;
	for (const auto &entry : dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot |
			QDir::Hidden | QDir::System)) {
		removed = removeCopy(dir.filePath(entry)) && removed;
	}
	return QDir().rmdir(path) && removed;
}

static bool copyRecursively(const QString &src, const QString &dst, TransferState &state) {
	if (state.stopped.load()) {
		return false;
	}
	state.current(src, "Copying");
	const QFileInfo info(src);
	bool created = false, ok = false;
	if (info.isSymLink()) {
		QByteArray link(4096, '\0');
		const ssize_t length = readlink(QFile::encodeName(src).constData(), link.data(), link.size() - 1);
		if (length < 0 || length == link.size() - 1) {
			return false;
		}
		link.resize(length);
		ok = symlink(link.constData(), QFile::encodeName(dst).constData()) == 0;
		if (ok) {
			state.advance(1);
		}
		return ok;
	}
	if (info.isDir()) {
		created = QDir().mkdir(dst);
		if (!created) {
			return false;
		}
		ok = true;
		state.advance(1);
		const QDir dir(src);
		for (const auto &entry : dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot |
				QDir::Hidden | QDir::System)) {
			if (!copyRecursively(dir.filePath(entry), dst + "/" + entry, state)) {
				ok = false;
				break;
			}
		}
	} else if (info.isFile()) {
		QFile input(src), output(dst);
		if (!input.open(QIODevice::ReadOnly) ||
				!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
			return false;
		}
		created = true;
		ok = true;
		QByteArray buffer(256 * 1024, '\0');
		while (!input.atEnd()) {
			if (state.stopped.load()) {
				ok = false;
				break;
			}
			const qint64 bytes = input.read(buffer.data(), buffer.size());
			if (bytes <= 0) {
				ok = bytes == 0 && input.error() == QFileDevice::NoError;
				break;
			}
			qint64 written = 0;
			while (written < bytes) {
				const qint64 part = output.write(buffer.constData() + written, bytes - written);
				if (part <= 0) {
					ok = false;
					break;
				}
				written += part;
			}
			if (!ok) {
				break;
			}
			state.advance(bytes);
		}
		ok = output.flush() && ok;
		output.close();
		if (ok) {
			state.advance(1);
		}
	}
	ok = ok && !state.stopped.load();
	if (!ok && created && !removeCopy(dst)) {
		state.errors << "An incomplete copy could not be removed: " + dst + ". The source is unchanged.";
		state.changed << QFileInfo(dst).absolutePath();
	}
	if (ok && !QFile::setPermissions(dst, info.permissions())) {
		state.errors << "The copy was saved, but its permissions could not be preserved: " + dst;
	}
	return ok;
}

struct TransferTask {
	QString src, target, resource, resourceTarget;
	bool copy, resourceFork;
	quint64 units = 0;
};

QString createAlias(const QString &target, const QString &destDir, const QString &name) {
	const QFileInfo original(target);
	if (!original.exists() || name.isEmpty() || name.contains('/') || name == "." || name == ".." ||
			!QFileInfo(destDir).isDir()) {
		Alert::ask("The alias cannot be made because its original or destination is unavailable.",
			"OK", QString());
		return {};
	}
	QDir destination(destDir);
	QString candidate = name + " alias";
	for (int i = 2; QFileInfo(destination.filePath(candidate)).exists() ||
			QFileInfo(destination.filePath(candidate)).isSymLink(); i++) {
		candidate = name + " alias " + QString::number(i);
	}
	if (!QFile::link(original.absoluteFilePath(), destination.filePath(candidate))) {
		Alert::ask("The alias could not be created in " + destDir + ". Check folder permissions.",
			"OK", QString());
		return {};
	}
	return candidate;
}

/* "Report copy", "Report copy 2", ... (Mac OS 8 naming). */
static QString copyName(const QDir &dir, const QString &name, const QSet<QString> &reserved) {
	QString base = name + " copy";
	if (!dir.exists(base) && !QFileInfo(dir.filePath(base)).isSymLink() &&
			!reserved.contains(dir.filePath(base))) {
		return base;
	}
	for (int i = 2;; i++) {
		QString candidate = base + " " + QString::number(i);
		if (!dir.exists(candidate) && !QFileInfo(dir.filePath(candidate)).isSymLink() &&
				!reserved.contains(dir.filePath(candidate))) {
			return candidate;
		}
	}
}

QStringList transferItems(const QStringList &paths, const QString &destDir, bool forceCopy,
		bool *finished) {
	if (finished) {
		*finished = false;
	}
	static bool transferring = false;
	if (transferring) {
		Alert::ask("Another file transfer is in progress. Finish or stop it first.", "OK", QString());
		return {};
	}
	QScopedValueRollback<bool> guard(transferring, true);
	std::vector<TransferTask> tasks;
	bool skipped = false;
	QSet<QString> reserved;
	const QDir dest(destDir);
	const QString destAbs = dest.absolutePath();
	const QString realDest = QFileInfo(destAbs).canonicalFilePath();
	if (realDest.isEmpty() || !QFileInfo(destAbs).isDir()) {
		Alert::ask("The destination folder is unavailable. Nothing was transferred.", "OK", QString());
		return {};
	}
	for (const QString &src : paths) {
		QFileInfo info(src);
		const QString srcAbs = info.absoluteFilePath();
		/* A folder can't go inside itself. */
		const QString realSrc = info.canonicalFilePath();
		if (!info.isSymLink() && info.isDir() &&
				(realSrc == "/" || realDest == realSrc || realDest.startsWith(realSrc + "/"))) {
			Alert::ask("A folder cannot be transferred into itself, including through an alias.",
				"OK", QString());
			skipped = true;
			continue;
		}
		const bool sameFolder = QFileInfo(info.absolutePath()).canonicalFilePath() == realDest;
		const bool copy = forceCopy || !sameDisk(srcAbs, destAbs);
		if (sameFolder && !copy) {
			continue; /* dropped where it already is */
		}
		QString name = info.fileName();
		if (sameFolder) {
			name = copyName(dest, name, reserved);
		} else if (dest.exists(name) || QFileInfo(dest.filePath(name)).isSymLink()) {
			Alert::ask("“" + name + "” already exists in the destination and was not replaced.",
				"OK", QString());
			skipped = true;
			continue;
		}
		const QString target = dest.filePath(name);
		const QString resource = info.absolutePath() + "/._" + info.fileName();
		const QString resourceTarget = dest.filePath("._" + name);
		if (reserved.contains(target)) {
			Alert::ask("More than one selected item has the same destination name: “" + name +
				"”. The duplicate was not transferred.", "OK", QString());
			skipped = true;
			continue;
		}
		const bool resourceFork = info.isFile() && !info.isSymLink() &&
			!info.fileName().startsWith("._") && QFileInfo::exists(resource);
		if (resourceFork && (QFileInfo(resource).isSymLink() || !QFileInfo(resource).isFile() ||
				QFileInfo::exists(resourceTarget))) {
			Alert::ask("The resource fork for “" + info.fileName() +
				"” cannot be transferred safely; the file has not been moved.", "OK", QString());
			skipped = true;
			continue;
		}
		tasks.push_back({srcAbs, target, resource, resourceTarget, copy, resourceFork});
		reserved.insert(target);
	}
	if (tasks.empty()) {
		if (finished) {
			*finished = !skipped;
		}
		return {};
	}
	TransferState state;
	state.operation = std::all_of(tasks.begin(), tasks.end(),
		[](const TransferTask &task) { return !task.copy; }) ? "Moving" : "Copying";
	TransferDialog dialog(state.stopped);
	QTimer refresh;
	auto update = [&] {
		std::lock_guard<std::mutex> lock(state.mutex);
		dialog.setProgress(state.operation, state.file, destAbs,
			state.completed, state.total, state.preparing);
	};
	QObject::connect(&refresh, &QTimer::timeout, &dialog, update);
	refresh.start(40);
	update();
	auto *worker = QThread::create([&] {
		for (auto &task : tasks) {
			if (state.stopped.load()) {
				return;
			}
			if (task.copy) {
				if (!countTransfer(task.src, state, task.units) ||
						(task.resourceFork && !countTransfer(task.resource, state, task.units))) {
					return;
				}
			} else {
				task.units = task.resourceFork ? 2 : 1;
			}
			std::lock_guard<std::mutex> lock(state.mutex);
			state.total += task.units;
		}
		{
			std::lock_guard<std::mutex> lock(state.mutex);
			state.preparing = false;
		}
		for (const auto &task : tasks) {
			if (state.stopped.load()) {
				break;
			}
			state.current(task.src, task.copy ? "Copying" : "Moving");
			bool ok = task.copy ? copyRecursively(task.src, task.target, state) :
				QDir().rename(task.src, task.target);
			if (ok && task.resourceFork) {
				ok = task.copy ? copyRecursively(task.resource, task.resourceTarget, state) :
					QDir().rename(task.resource, task.resourceTarget);
				if (!ok) {
					const bool restored = task.copy ? removeCopy(task.target) :
						QDir().rename(task.target, task.src);
					if (!restored) {
						state.errors << "The resource fork transfer failed and the file could not be restored. "
							"Check " + task.src + " and " + task.target;
						state.changed << destAbs << QFileInfo(task.src).absolutePath();
					}
				}
			}
			if (ok) {
				state.changed << destAbs;
				if (!task.copy) {
					state.advance(task.units);
					state.changed << QFileInfo(task.src).absolutePath();
				}
			} else if (!state.stopped.load()) {
				state.errors << "“" + QFileInfo(task.src).fileName() +
					"” could not be transferred. Check the source and destination folders.";
				break;
			}
		}
	});
	QObject::connect(worker, &QThread::finished, &dialog, [&] {
		update();
		dialog.done(QDialog::Accepted);
	});
	worker->start();
	dialog.exec();
	worker->wait();
	delete worker;
	state.changed.removeDuplicates();
	if (finished) {
		*finished = !skipped && !state.stopped.load() && state.errors.isEmpty();
	}
	for (const auto &error : state.errors) {
		Alert::ask(error, "OK", QString());
	}
	return state.changed;
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

QMimeData *itemDragMime(const std::vector<Item *> &items) {
	const bool anyVirtual = std::any_of(items.begin(), items.end(),
		[](const Item *item) { return item->isVirtual; });
	auto *mime = new QMimeData;
	if (anyVirtual) {
		mime->setData(ICON_MOVE_MIME, QByteArray());
		QJsonArray aliases;
		for (const Item *item : items) {
			const VNode *node = item->isVirtual ? vfsNode(item->path) : nullptr;
			if (node && !node->actionId.isEmpty()) {
				aliases = {};
				break;
			}
			const QString target = item->isVirtual ? vfsRealCounterpart(item->path) : item->path;
			if (target.isEmpty()) {
				aliases = {};
				break;
			}
			aliases.append(QJsonObject{{"name", item->name}, {"target", target}});
		}
		if (!aliases.isEmpty()) {
			mime->setData(ALIAS_ITEMS_MIME, QJsonDocument(aliases).toJson(QJsonDocument::Compact));
		}
	} else {
		QList<QUrl> urls;
		for (Item *item : items) {
			urls << QUrl::fromLocalFile(item->path);
		}
		mime->setUrls(urls);
	}
	return mime;
}

void startItemDrag(QWidget *source, const std::vector<Item *> &items,
		const std::vector<QPoint> &itemOrigins, QPoint pointer) {
	if (items.empty()) {
		return;
	}
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

	auto *drag = new QDrag(source);
	drag->setMimeData(itemDragMime(items));
	drag->setPixmap(pixmap);
	drag->setHotSpot(pointer - bounds.topLeft());
	const Qt::DropAction done = drag->exec(
		anyVirtual ? Qt::MoveAction | Qt::LinkAction :
			Qt::MoveAction | Qt::CopyAction | Qt::LinkAction, Qt::MoveAction);

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
	const bool aliasDrag = e->mimeData()->hasFormat(ALIAS_ITEMS_MIME);
	const bool link = aliasDrag || e->proposedAction() == Qt::LinkAction ||
		(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) ==
			(Qt::ControlModifier | Qt::AltModifier);
	if (link) {
		if (!(e->possibleActions() & Qt::LinkAction)) {
			Alert::ask("This drag source does not support aliases. Use File > Make Alias instead.",
				"OK", QString());
			e->ignore();
			return;
		}
		QString destination = target ? target->path : folder;
		if (vfsIsVirtual(destination)) {
			destination = vfsOpensAs(destination);
		}
		if (destination.isEmpty() || !QFileInfo(destination).isDir() ||
				(target && (target->kind == PL_ICON_TRASH_EMPTY || target->kind == PL_ICON_TRASH_FULL))) {
			Alert::ask("Aliases must be placed in a real folder or on the Desktop.", "OK", QString());
			e->ignore();
			return;
		}
		QJsonArray aliases;
		if (aliasDrag) {
			const auto document = QJsonDocument::fromJson(e->mimeData()->data(ALIAS_ITEMS_MIME));
			if (!document.isArray() || document.array().isEmpty() || document.array().size() > 256) {
				Alert::ask("The dragged alias information is invalid.", "OK", QString());
				e->ignore();
				return;
			}
			aliases = document.array();
			for (const auto &value : aliases) {
				const auto item = value.toObject();
				if (!value.isObject() || !item["name"].isString() || !item["target"].isString() ||
						!QDir::isAbsolutePath(item["target"].toString())) {
					Alert::ask("The dragged alias information is invalid.", "OK", QString());
					e->ignore();
					return;
				}
			}
		} else {
			for (const auto &path : draggedPaths(e->mimeData())) {
				aliases.append(QJsonObject{{"name", QFileInfo(path).fileName()}, {"target", path}});
			}
		}
		bool made = false;
		for (const auto &value : aliases) {
			const auto item = value.toObject();
			made = !createAlias(item["target"].toString(), destination,
				item["name"].toString()).isEmpty() || made;
		}
		if (made) {
			Finder::instance().folderChanged(destination);
			e->setDropAction(Qt::LinkAction);
			e->accept();
		} else {
			e->ignore();
		}
		return;
	}
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
	const bool copied = option || std::any_of(paths.begin(), paths.end(),
		[&dest](const QString &path) { return !sameDisk(path, dest); });
	if (e->source()) {
		/* Release Finder's drag grab before opening the modal Stop dialog.
		 * Our drag source never deletes files based on the returned action. */
		QTimer::singleShot(0, QCoreApplication::instance(), [paths, dest, option] {
			for (const auto &folder : transferItems(paths, dest, option)) {
				Finder::instance().folderChanged(folder);
			}
		});
		e->setDropAction(copied ? Qt::CopyAction : Qt::MoveAction);
		e->accept();
		return;
	}
	bool finished = false;
	for (const QString &d : transferItems(paths, dest, option, &finished)) {
		finder.folderChanged(d);
	}
	if (!finished) {
		e->ignore();
		return;
	}
	e->setDropAction(copied ? Qt::CopyAction : Qt::MoveAction);
	e->accept();
}
