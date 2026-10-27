#include "fileops.h"

#include "clippings.h"
#include "desktopprinter.h"
#include "alias.h"
#include "platinumshell.h"
#include "programs.h"
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
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QWidget>
#include <QThread>
#include <QTimer>
#include <QScopedValueRollback>
#include <QSet>
#include <QTemporaryDir>
#include <algorithm>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

#include "alert.h"
#include "finder.h"
#include "vfs.h"
#include "apptrash.h"
#include "appdb.h"
#include "controlpanels.h"
#include "autostart.h"
#include "fileassoc.h"
#include "transferdialog.h"
#include "fonts.h"

static bool sameDisk(const QString &a, const QString &b) {
	return QStorageInfo(a).device() == QStorageInfo(b).device();
}

void refreshUserFontCache(const QString &directory) {
	if (!fontUserDirectory(directory)) {
		return;
	}
	static QSet<QString> running, dirty;
	if (running.contains(directory)) {
		dirty.insert(directory);
		return;
	}
	running.insert(directory);
	QTimer::singleShot(0, QCoreApplication::instance(), [directory] {
		auto *process = new QProcess(QCoreApplication::instance());
		auto *timeout = new QTimer(process);
		const auto finished = [directory] {
			running.remove(directory);
			vfsFontsChanged();
			if (dirty.remove(directory)) {
				refreshUserFontCache(directory);
			}
		};
		timeout->setSingleShot(true);
		QObject::connect(timeout, &QTimer::timeout, process, [process] {
			process->setProperty("font-cache-timeout", true);
			process->kill();
		});
		QObject::connect(process, &QProcess::finished, process,
			[process, timeout, directory, finished](int code, QProcess::ExitStatus status) {
				timeout->stop();
				if (status != QProcess::NormalExit || code != 0) {
					const QString reason = process->property("font-cache-timeout").toBool() ?
						"The refresh timed out." : QString::fromUtf8(process->readAllStandardError());
					Alert::ask("The font files changed, but the font cache could not be updated for " +
						directory + ". " + reason, "OK", QString());
				}
				finished();
				process->deleteLater();
			});
		QObject::connect(process, &QProcess::errorOccurred, process,
			[process, timeout, finished](QProcess::ProcessError error) {
				if (error == QProcess::FailedToStart) {
					timeout->stop();
					Alert::ask("The font files changed, but fc-cache could not be started.", "OK", QString());
					finished();
					process->deleteLater();
				}
			});
		process->start("fc-cache", QDir(directory).exists() ?
			QStringList{ "-f", directory } : QStringList{ "-f" });
		timeout->start(30000);
	});
}

static bool fontRemovalAllowed(const QStringList &files, bool confirm) {
	bool collection = false;
	for (const QString &file : files) {
		if (!fontCanRemove(file)) {
			Alert::ask("This font cannot be removed: " + file +
				". Only your own installed fonts can be moved out or put in Trash. "
				"Option-drag to export a copy instead.", "OK", QString());
			return false;
		}
		QString error;
		const auto faces = fontInspect(file, &error);
		if (faces.empty()) {
			Alert::ask(error, "OK", QString());
			return false;
		}
		collection |= faces.size() > 1;
	}
	return !confirm || !collection || Alert::ask(
		"Moving a font collection removes every font face in that file. Continue?",
		"Continue", "Cancel");
}

static bool fontTransferAllowed(const QStringList &files, const QString &destination, bool copy) {
	QStringList moving;
	for (const QString &file : files) {
		if (!copy && sameDisk(file, destination)) {
			moving << file;
		}
	}
	return fontRemovalAllowed(moving, true);
}

bool trashFontFiles(const QStringList &files) {
	QStringList unique = files;
	unique.removeDuplicates();
	if (!fontRemovalAllowed(unique, true)) {
		return false;
	}
	bool removed = true;
	for (const QString &file : unique) {
		if (!fontCanRemove(file) || !QFile::moveToTrash(file)) {
			Alert::ask("The font could not be moved to Trash: " + file, "OK", QString());
			removed = false;
			continue;
		}
		Finder::instance().folderChanged(QFileInfo(file).absolutePath());
	}
	Finder::instance().folderChanged(trashFilesPath());
	return removed;
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

QString createAlias(const QString &target, const QString &destDir, const QString &name,
		const QString &suffix) {
	const QFileInfo original(target);
	if (!original.exists() || name.isEmpty() || name.contains('/') || name == "." || name == ".." ||
			!QFileInfo(destDir).isDir()) {
		Alert::ask("The alias cannot be made because its original or destination is unavailable.",
			"OK", QString());
		return {};
	}
	QDir destination(destDir);
	QString candidate = name + suffix;
	for (int i = 2; QFileInfo(destination.filePath(candidate)).exists() ||
			QFileInfo(destination.filePath(candidate)).isSymLink(); i++) {
		candidate = name + suffix + " " + QString::number(i);
	}
	if (!QFile::link(original.absoluteFilePath(), destination.filePath(candidate))) {
		Alert::ask("The alias could not be created in " + destDir + ". Check folder permissions.",
			"OK", QString());
		return {};
	}
	if (!aliasRecord(destination.filePath(candidate), original.absoluteFilePath())) {
		QFile::remove(destination.filePath(candidate));
		Alert::ask("The alias was not created because its identity record could not be saved.",
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
	if (fontUserDirectory(realDest)) {
		for (const QString &src : paths) {
			QString error;
			if (!fontCanInstall(src, &error)) {
				Alert::ask(error, "OK", QString());
				return {};
			}
		}
	}
	for (const QString &src : paths) {
		if (appTrashMarker(src)) {
			Alert::ask("Queued applications cannot be copied or moved as files. "
				"Use Put Away or drag back to Applications to cancel removal.", "OK", QString());
			return {};
		}
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
		if (sameFolder && (!copy || fontUserDirectory(realDest))) {
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
			bool ok;
			if (task.copy && fontUserDirectory(realDest)) {
				QTemporaryDir staging(realDest + "/.zacos-font-XXXXXX");
				ok = staging.isValid() &&
					copyRecursively(task.src, staging.filePath("pending"), state) &&
					QDir().rename(staging.filePath("pending"), task.target);
			} else {
				ok = task.copy ? copyRecursively(task.src, task.target, state) :
					QDir().rename(task.src, task.target);
			}
			if (ok) {
				if (!aliasMoveRecord(task.src, task.target, task.copy)) {
					state.errors << "The item was transferred, but its alias identity record could "
						"not be preserved: " + task.target;
				}
			}
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
	const uint32_t *px = pl_icon(item->iconKind(), PL_ICON_LARGE);
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
	const bool allFonts = !items.empty() && std::all_of(items.begin(), items.end(),
		[](const Item *item) { return !item->fontFile.isEmpty(); });
	if (allFonts) {
		QList<QUrl> urls;
		QJsonArray files;
		QSet<QString> seen;
		for (const Item *item : items) {
			if (!seen.contains(item->fontFile)) {
				seen.insert(item->fontFile);
				urls << QUrl::fromLocalFile(item->fontFile);
				files.append(item->fontFile);
			}
		}
		mime->setUrls(urls);
		mime->setData(FONT_ITEMS_MIME, QJsonDocument(files).toJson(QJsonDocument::Compact));
		return mime;
	}
	if (anyVirtual) {
		mime->setData(ICON_MOVE_MIME, QByteArray());
		QJsonArray applications;
		for (const Item *item : items) {
			if (!item->isVirtual || !vfsIsAppFolder(item->path)) {
				applications = {};
				break;
			}
			applications.append(vfsNode(item->path)->appId);
		}
		QJsonArray startup;
		QJsonArray panels;
		for (const Item *item : items) {
			const QString id = item->isVirtual ? vfsStartupItemId(item->path) : QString();
			if (id.isEmpty()) {
				startup = {};
				break;
			}
			startup.append(id);
		}
		if (!startup.isEmpty()) {
			mime->setData(STARTUP_ITEMS_MIME, QJsonDocument(startup).toJson(QJsonDocument::Compact));
		}
		for (const Item *item : items) {
			const QString id = item->isVirtual ? vfsPanelId(item->path) : QString();
			if (id.isEmpty()) {
				panels = {};
				break;
			}
			panels.append(id);
		}
		if (!panels.isEmpty()) {
			mime->setData(PANEL_ITEMS_MIME, QJsonDocument(panels).toJson(QJsonDocument::Compact));
		}
		if (!applications.isEmpty()) {
			mime->setData(APPLICATION_ITEMS_MIME,
				QJsonDocument(applications).toJson(QJsonDocument::Compact));
		}
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
		/* A clipping dragged into a document puts back what it holds. */
		if (items.size() == 1) {
			addClippingContent(mime, items.front()->path);
		}
	}
	return mime;
}

void startItemDrag(QWidget *source, const std::vector<Item *> &items,
		const std::vector<QPoint> &itemOrigins, QPoint pointer, bool placeOnly) {
	if (items.empty()) {
		return;
	}
	const bool anyVirtual = std::any_of(items.begin(), items.end(),
		[](const Item *item) { return item->isVirtual && item->fontFile.isEmpty(); });
	const bool fonts = std::all_of(items.begin(), items.end(),
		[](const Item *item) { return !item->fontFile.isEmpty(); });
	const bool protectedFonts = fonts && std::any_of(items.begin(), items.end(),
		[](const Item *item) { return !fontCanRemove(item->fontFile); });
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
	if (placeOnly) {
		auto *mime = new QMimeData;
		mime->setData(ICON_MOVE_MIME, QByteArray());
		mime->setData(PLACE_ONLY_MIME, QByteArray());
		drag->setMimeData(mime);
		drag->setPixmap(pixmap);
		drag->setHotSpot(pointer - bounds.topLeft());
		drag->exec(Qt::MoveAction, Qt::MoveAction);
		return;
	}
	drag->setMimeData(itemDragMime(items));
	drag->setPixmap(pixmap);
	drag->setHotSpot(pointer - bounds.topLeft());
	const Qt::DropAction done = drag->exec(protectedFonts ? Qt::CopyAction :
		fonts ? Qt::MoveAction | Qt::CopyAction :
		anyVirtual ? Qt::MoveAction | Qt::LinkAction :
			Qt::MoveAction | Qt::CopyAction | Qt::LinkAction,
		protectedFonts ? Qt::CopyAction : Qt::MoveAction);

	/* Their own window always takes such a drag back; anywhere else
	 * leaves it nowhere to go, so say why rather than do nothing. */
	if (anyVirtual && done == Qt::IgnoreAction) {
		const Item *first = *std::find_if(items.begin(), items.end(),
			[](const Item *item) { return item->isVirtual; });
		if (vfsIsAppFolder(first->path)) {
			Alert::ask("“" + first->name + "” stays in the Applications folder. "
				"To remove an application, select it and choose Move To Trash.", "OK", QString());
		} else if (!vfsStartupItemId(first->path).isEmpty()) {
			Alert::ask("To stop “" + first->name + "” opening at login, drag it to the Trash.",
				"OK", QString());
		} else {
			Alert::ask("“" + first->name + "” is part of this computer and can't be moved "
				"out of its window.", "OK", QString());
		}
	}
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
			platinumStartApplication(siblingProgram("zacos9-wininstall"), { p });
		} else {
			rest << p;
		}
	}
	if (!rest.isEmpty()) {
		platinumStartApplication(siblingProgram("zacos9-appinstall"), rest);
	}
}

/* Startup Items: the desktop entry each dropped item stands for. An
 * application from Applications, an alias to one, or a .desktop file. */
static QList<QPair<QString, QString>> droppedStartupApps(const QMimeData *mime,
		QStringList *rejected) {
	QList<QPair<QString, QString>> apps;
	auto addFile = [&](QString path, const QString &name) {
		const AliasResolution alias = aliasResolve(path);
		if (alias.state == AliasState::Ok || alias.state == AliasState::Reconnected) {
			path = alias.target;
		} else if (alias.state != AliasState::NotAlias) {
			*rejected << name;
			return;
		}
		if (const AppEntry *app = appByFile(QFileInfo(path).absoluteFilePath())) {
			apps << qMakePair(app->id, app->file);
		} else if (path.endsWith(".desktop") && QFileInfo(path).isFile()) {
			apps << qMakePair(QFileInfo(path).fileName(), path);
		} else {
			*rejected << name;
		}
	};
	if (mime->hasFormat(APPLICATION_ITEMS_MIME)) {
		const auto document = QJsonDocument::fromJson(mime->data(APPLICATION_ITEMS_MIME));
		for (const auto &value : document.array()) {
			const AppEntry *app = appById(value.toString());
			if (app) {
				apps << qMakePair(app->id, app->file);
			} else {
				*rejected << value.toString();
			}
		}
	} else if (mime->hasFormat(ALIAS_ITEMS_MIME)) {
		const auto document = QJsonDocument::fromJson(mime->data(ALIAS_ITEMS_MIME));
		for (const auto &value : document.array()) {
			const auto item = value.toObject();
			addFile(item["target"].toString(), item["name"].toString());
		}
	} else {
		for (const QString &path : draggedPaths(mime)) {
			addFile(path, nameWithoutExtension(QFileInfo(path).fileName()));
		}
	}
	return apps.size() > 256 ? decltype(apps){} : apps;
}

static void dropIntoStartupItems(QDropEvent *e) {
	QStringList rejected;
	const auto apps = droppedStartupApps(e->mimeData(), &rejected);
	if (apps.isEmpty() && rejected.isEmpty()) {
		e->ignore();
		return;
	}
	// Release the drag grab before any alert is opened.
	QTimer::singleShot(0, QCoreApplication::instance(), [apps, rejected] {
		const QString trash = QDir(trashFilesPath()).absolutePath();
		bool trashChanged = false;
		for (const auto &app : apps) {
			QString error;
			if (!autostartAddApplication(autostartDefaultContext(), app.first, app.second, &error)) {
				Alert::ask(error, "OK", QString());
			} else if (QFileInfo(app.second).absolutePath() == trash) {
				/* A startup item dragged back out of the Trash moves, as on the Mac. */
				const QString name = QFileInfo(app.second).fileName();
				if (QFile::remove(app.second)) {
					QFile::remove(QFileInfo(trash).absolutePath() + "/info/" + name + ".trashinfo");
					trashChanged = true;
				}
			}
		}
		if (trashChanged) {
			Finder::instance().folderChanged(trashFilesPath());
		}
		if (!rejected.isEmpty()) {
			Alert::ask("“" + rejected.first() + "” isn't an application. Only applications "
				"can be startup items.", "OK", QString());
		}
		vfsRefresh();
	});
	/* Link: the application stays where it was. */
	e->setDropAction(Qt::LinkAction);
	e->accept();
}

/* A control panel dragged into Control Panels (Disabled): the entry it
 * stands for is turned off there, as the Mac's Extensions Manager did, and
 * the panel leaves Control Panels. The entries are the drag's own ids, so
 * nothing outside the two folders can be turned off this way. */
static void setPanelsEnabled(QDropEvent *e, bool enabled) {
	const auto document = QJsonDocument::fromJson(e->mimeData()->data(PANEL_ITEMS_MIME));
	QStringList ids;
	bool valid = document.isArray() && !document.array().isEmpty() &&
		document.array().size() <= 256;
	for (const auto &value : document.array()) {
		const QString id = value.toString();
		if (valid && value.isString() && !id.isEmpty()) {
			ids << id;
		} else {
			valid = false;
		}
	}
	if (!valid) {
		Alert::ask("The dragged control panel information is invalid.", "OK", QString());
		e->ignore();
		return;
	}
	QStringList refused;
	for (const QString &id : ids) {
		if (!pl_control_panel_set_enabled(id.toUtf8().constData(), enabled)) {
			refused << id;
		}
	}
	/* Accepted first, so the drag ends and the windows can be reloaded
	 * while it does. */
	e->setDropAction(Qt::MoveAction);
	e->accept();
	QTimer::singleShot(0, QCoreApplication::instance(), [enabled, refused] {
		vfsRefresh();
		Finder::instance().folderChanged(vfsPathFor("system-folder/control-panels"));
		Finder::instance().folderChanged(vfsPathFor("system-folder/control-panels-disabled"));
		if (!refused.isEmpty()) {
			Alert::ask(enabled ? "That control panel could not be turned back on." :
				"That control panel could not be turned off.", "OK", QString());
		}
	});
}

static void removeStartupItems(QDropEvent *e) {
	const auto document = QJsonDocument::fromJson(e->mimeData()->data(STARTUP_ITEMS_MIME));
	if (!document.isArray() || document.array().isEmpty() || document.array().size() > 256) {
		e->ignore();
		return;
	}
	QStringList ids;
	for (const auto &value : document.array()) {
		ids << value.toString();
	}
	QTimer::singleShot(0, QCoreApplication::instance(), [ids] {
		removeFromStartupItems(ids);
	});
	e->setDropAction(Qt::MoveAction);
	e->accept();
}

void removeFromStartupItems(const QStringList &ids) {
	bool removed = false;
	for (const QString &id : ids) {
		QString error;
		if (autostartRemove(autostartDefaultContext(), id, nullptr, &error)) {
			removed = true;
		} else {
			Alert::ask(error, "OK", QString());
		}
	}
	vfsRefresh();
	Finder::instance().folderChanged(trashFilesPath());
	if (removed) {
		pl_sound_event("trash-move");
	}
}

bool isClippingDrop(const QDropEvent *e) {
	return e->source() == nullptr && canMakeClipping(e->mimeData());
}

QStringList dropClippings(QDropEvent *e, const Item *target, const QString &folder,
		const std::function<void(const QString &into, const QStringList &made)> &placed) {
	const QString into = target && target->kind == PL_ICON_FOLDER ? target->path : folder;
	if (vfsIsVirtual(into) || !QFileInfo(into).isDir()) {
		e->ignore();
		return {};
	}
	QString error;
	const QStringList made = makeClippings(e->mimeData(), into, &error);
	e->setDropAction(Qt::CopyAction);
	e->accept();
	if (placed) {
		placed(into, made);
	}
	Finder::instance().folderChanged(into);
	if (made.isEmpty()) {
		QTimer::singleShot(0, QCoreApplication::instance(), [error] {
			Alert::ask("The clipping couldn't be made. " + error, "OK", QString());
		});
	}
	return made;
}

void dropItems(QDropEvent *e, const Item *target, const QString &folder) {
	const bool fontDrag = e->mimeData()->hasFormat(FONT_ITEMS_MIME);
	const QString fontDestination = target ? target->path : folder;
	if (fontDrag) {
		const auto document = QJsonDocument::fromJson(e->mimeData()->data(FONT_ITEMS_MIME));
		QStringList declared;
		bool valid = document.isArray();
		if (document.isArray()) {
			for (const auto &entry : document.array()) {
				if (entry.isString() && QDir::isAbsolutePath(entry.toString())) {
					declared << entry.toString();
				} else {
					valid = false;
				}
			}
		}
		const QStringList paths = draggedPaths(e->mimeData());
		if (!valid || declared.isEmpty() || declared.size() > 4096 || declared != paths) {
			Alert::ask("The dragged font information is invalid.", "OK", QString());
			e->ignore();
			return;
		}
		for (const QString &path : paths) {
			QString error;
			if (fontInspect(path, &error).empty()) {
				Alert::ask(error, "OK", QString());
				e->ignore();
				return;
			}
		}
		if (vfsIsFonts(fontDestination)) {
			e->ignore(); // Already installed; an internal drop only arranges icons.
			return;
		}
	}
	/* Documents dropped on a desktop printer are printed there. */
	if (target && target->kind == PL_ICON_PRINTER) {
		QStringList files;
		for (const QUrl &url : e->mimeData()->urls()) {
			if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile()) {
				files << QFileInfo(url.toLocalFile()).canonicalFilePath();
			}
		}
		e->setDropAction(Qt::CopyAction);
		e->accept();
		const QString queue = desktopPrinterQueue(target->path);
		if (!files.isEmpty()) {
			/* After the drag has finished, so its source isn't kept waiting. */
			QTimer::singleShot(0, [files, queue] { Finder::instance().printDocuments(files, queue); });
		}
		return;
	}
	/* Text, a picture or a link from another program: clippings. */
	if (isClippingDrop(e)) {
		dropClippings(e, target, folder);
		return;
	}
	const bool toTrash = (target &&
		(target->kind == PL_ICON_TRASH_EMPTY || target->kind == PL_ICON_TRASH_FULL)) ||
		(!target && QDir(folder) == QDir(trashFilesPath()));
	if (vfsIsStartupItems(target ? target->path : folder)) {
		if (e->mimeData()->hasFormat(STARTUP_ITEMS_MIME)) {
			e->ignore(); /* already there */
			return;
		}
		dropIntoStartupItems(e);
		return;
	}
	if (e->mimeData()->hasFormat(STARTUP_ITEMS_MIME)) {
		if (toTrash) {
			removeStartupItems(e);
		} else {
			e->ignore();
		}
		return;
	}
	if (e->mimeData()->hasFormat(PANEL_ITEMS_MIME)) {
		/* Only into Control Panels (Disabled): the way back into Control
		 * Panels is the next change. */
		if (vfsIsControlPanelsDisabled(target ? target->path : folder)) {
			setPanelsEnabled(e, false);
		} else {
			e->ignore();
		}
		return;
	}
	if (vfsIsControlPanelsDisabled(target ? target->path : folder)) {
		e->ignore(); /* nothing but a control panel goes in there */
		return;
	}
	if (toTrash && e->mimeData()->hasFormat(APPLICATION_ITEMS_MIME)) {
		const auto document = QJsonDocument::fromJson(e->mimeData()->data(APPLICATION_ITEMS_MIME));
		if (!document.isArray() || document.array().isEmpty() || document.array().size() > 256) {
			Alert::ask("The dragged application information is invalid.", "OK", QString());
			e->ignore();
			return;
		}
		QStringList ids;
		for (const auto &value : document.array()) {
			if (!value.isString() || !appById(value.toString())) {
				Alert::ask("The dragged application is no longer available.", "OK", QString());
				e->ignore();
				return;
			}
			ids << value.toString();
		}
		// Release the drag grab before any error alert is opened.
		QTimer::singleShot(0, QCoreApplication::instance(), [ids] {
			bool queued = false;
			for (const QString &id : ids) {
				QString error;
				if (appTrashQueue(id, &error)) {
					queued = true;
				} else {
					Alert::ask(error, "OK", QString());
				}
			}
			vfsRefresh();
			Finder::instance().folderChanged(trashFilesPath());
			if (queued) {
				pl_sound_event("trash-move");
			}
		});
		e->setDropAction(Qt::MoveAction);
		e->accept();
		return;
	}
	const bool aliasDrag = e->mimeData()->hasFormat(ALIAS_ITEMS_MIME);
	const bool link = !fontDrag && (aliasDrag || e->proposedAction() == Qt::LinkAction ||
		(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) ==
			(Qt::ControlModifier | Qt::AltModifier));
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
	if (toTrash) {
		if (fontDrag) {
			if (e->source()) {
				e->setDropAction(Qt::MoveAction);
				e->accept();
				QTimer::singleShot(0, QCoreApplication::instance(),
					[paths] { trashFontFiles(paths); });
			} else if (trashFontFiles(paths)) {
				e->setDropAction(Qt::MoveAction);
				e->accept();
			} else {
				e->ignore();
			}
			return;
		}
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
	if (vfsIsFonts(dest)) {
		dest = userFontsPath();
		if (dest.isEmpty() || !QDir().mkpath(dest) || !fontUserDirectory(dest)) {
			Alert::ask("The user Fonts folder could not be created: " + dest, "OK", QString());
			e->ignore();
			return;
		}
	}
	if (vfsIsApplications(dest)) {
		QStringList installers;
		bool restored = false;
		for (const QString &path : paths) {
			if (appTrashMarker(path)) {
				QString error;
				if (appTrashRestore(path, &error)) {
					restored = true;
				} else {
					Alert::ask(error, "OK", QString());
				}
			} else {
				installers << path;
			}
		}
		if (restored) {
			vfsRefresh();
			finder.folderChanged(trashFilesPath());
		}
		installIntoApplications(installers);
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
	const bool option = (e->modifiers() & Qt::AltModifier) ||
		((fontDrag || fontUserDirectory(dest)) && !(e->possibleActions() & Qt::MoveAction));
	const bool copied = option || std::any_of(paths.begin(), paths.end(),
		[&dest](const QString &path) { return !sameDisk(path, dest); });
	if (fontDrag && !e->source() && !fontTransferAllowed(paths, dest, option)) {
		e->ignore();
		return;
	}
	if (e->source()) {
		/* Release Finder's drag grab before opening the modal Stop dialog.
		 * Our drag source never deletes files based on the returned action. */
		QTimer::singleShot(0, QCoreApplication::instance(), [paths, dest, option, fontDrag] {
			if (fontDrag && !fontTransferAllowed(paths, dest, option)) {
				return;
			}
			const QStringList changed = transferItems(paths, dest, option);
			for (const auto &folder : changed) {
				Finder::instance().folderChanged(folder);
			}
		});
		e->setDropAction(copied ? Qt::CopyAction : Qt::MoveAction);
		e->accept();
		return;
	}
	bool finished = false;
	const QStringList changed = transferItems(paths, dest, option, &finished);
	for (const QString &d : changed) {
		finder.folderChanged(d);
	}
	if (!finished) {
		e->ignore();
		return;
	}
	e->setDropAction(copied ? Qt::CopyAction : Qt::MoveAction);
	e->accept();
}
