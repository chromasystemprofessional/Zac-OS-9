#include "finder.h"
#include "platinumshell.h"
#include "settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProgressDialog>
#include <QProcess>
#include <QStandardPaths>
#include <QWidget>
#include <QTimer>

#include <QDirIterator>
#include <QUrl>

#include "appdb.h"
#include "apptrash.h"
#include "../store/storeclient.h"
#include <QScopedValueRollback>
#include "fileops.h"
#include "localvolumes.h"
#include "diskinit.h"
#include "netvolumes.h"
#include "vfs.h"

#include "alert.h"
#include "desktop.h"
#include "findwindow.h"
#include "folderwindow.h"
#include "infowindow.h"

Finder &Finder::instance() {
	static Finder finder;
	return finder;
}

QString Finder::socketPath() {
	QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR", "/tmp");
	QString display = qEnvironmentVariable("WAYLAND_DISPLAY", "wayland-0");
	return runtime + "/zacos9-finder." + display + ".sock";
}

/* Mac OS 8's default spring delay is "medium". TODO: measure. */
static constexpr int SPRING_MS = 900;

void Finder::springHover(const QString &folder) {
	if (folder == m_springPath) {
		return;
	}
	m_springPath = folder;
	if (!m_springTimer) {
		m_springTimer = new QTimer;
		m_springTimer->setSingleShot(true);
		QObject::connect(m_springTimer, &QTimer::timeout, [this] {
			if (m_springPath.isEmpty()) {
				return;
			}
			const bool wasOpen = FolderWindow::isOpen(m_springPath);
			FolderWindow *w = FolderWindow::open(m_springPath);
			if (!wasOpen) {
				m_sprung.push_back(w);
			}
			m_springPath.clear();
		});
	}
	if (folder.isEmpty()) {
		m_springTimer->stop();
	} else {
		m_springTimer->start(SPRING_MS);
	}
}

void Finder::dragEnded() {
	springHover(QString());
	for (auto &w : m_sprung) {
		if (w) {
			w->close();
		}
	}
	m_sprung.clear();
}

void Finder::start(Desktop *desktop) {
	m_desktop = desktop;
	m_front = desktop;
	QLocalServer::removeServer(socketPath());
	m_server.listen(socketPath());
	QObject::connect(&m_server, &QLocalServer::newConnection, [this] {
		while (QLocalSocket *s = m_server.nextPendingConnection()) {
			m_clients.push_back(s);
			QObject::connect(s, &QLocalSocket::readyRead, [this, s] {
				while (s->canReadLine()) {
					QString line = QString::fromUtf8(s->readLine()).trimmed();
					if (line.startsWith("cmd ")) {
						command(line.mid(4));
					}
				}
			});
			QObject::connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
			s->write((stateLine() + "\n").toUtf8());
		}
	});
}

void Finder::replaceDesktop(Desktop *desktop) {
	if (m_front == m_desktop) {
		m_front = desktop;
	}
	m_desktop = desktop;
	notifyState();
}

FinderView *Finder::front() {
	return m_front ? m_front : m_desktop;
}

void Finder::setFront(FinderView *view) {
	if (m_front != view) {
		m_front = view;
		notifyState();
	}
}

void Finder::viewClosed(FinderView *view) {
	if (m_front == view) {
		m_front = m_desktop;
		notifyState();
	}
}

QString Finder::stateLine() {
	FinderView *v = front();
	int selection = v ? static_cast<int>(v->selectedItems().size()) : 0;
	bool window = v && v != m_desktop;
	QDir trash(trashFilesPath());
	bool full = trash.exists() && !trash.isEmpty();
	auto *fw = dynamic_cast<FolderWindow *>(v);
	int list = !fw ? 0 : fw->viewMode() == FolderWindow::ViewMode::List ? 1
		: fw->viewMode() == FolderWindow::ViewMode::Buttons ? 2 : 0;
	int label = -1;
	if (v) {
		for (Item *item : v->selectedItems()) {
			if (label == -1) {
				label = item->labelIndex;
			} else if (label != item->labelIndex) {
				label = -1;
				break;
			}
		}
	}
	const auto items = v ? v->selectedItems() : std::vector<Item *>{};
	const bool erase = items.size() == 1 && items[0]->isLocalVolume &&
		diskCanEraseVolume(items[0]->path);
	return QStringLiteral("state selection=%1 window=%2 trash=%3 view=%4 label=%5 erase=%6")
		.arg(selection).arg(window ? 1 : 0).arg(full ? 1 : 0).arg(list).arg(label).arg(erase ? 1 : 0);
}

void Finder::notifyState() {
	QString line = stateLine();
	if (line == m_lastState) {
		return;
	}
	m_lastState = line;
	for (auto &c : m_clients) {
		if (c) {
			c->write((line + "\n").toUtf8());
		}
	}
}

void Finder::command(const QString &name) {
	if (name == "new-folder") {
		newFolder();
	} else if (name == "open") {
		openSelection();
	} else if (name == "close-window") {
		closeWindow();
	} else if (name == "move-to-trash") {
		moveSelectionToTrash();
	} else if (name == "empty-trash") {
		emptyTrash();
	} else if (name == "erase-disk") {
		const auto items = front() ? front()->selectedItems() : std::vector<Item *>{};
		if (items.size() != 1 || !items[0]->isLocalVolume) {
			Alert::ask("Select one USB disk on the desktop to erase.", "OK", QString());
		} else {
			diskEraseVolume(items[0]->path, items[0]->name);
		}
	} else if (name == "get-info") {
		getInfo();
	} else if (name == "get-info-sharing") {
		getInfo(true);
	} else if (name.startsWith("info /")) {
		/* Get Info for a path (scripts and other apps). */
		const QString path = QDir::cleanPath(name.mid(5));
		InfoWindow::open(path, path == "/" ? PL_ICON_DISK
			: QFileInfo(path).isDir() ? PL_ICON_FOLDER : PL_ICON_DOCUMENT, displayName(path));
	} else if (name == "select-all") {
		if (FinderView *v = front()) {
			v->selectAll();
		}
	} else if (name == "duplicate") {
		duplicate();
	} else if (name == "make-alias") {
		makeAlias();
	} else if (name == "put-away") {
		putAway();
	} else if (name == "show-original") {
		showOriginal();
	} else if (name.startsWith("label ")) {
		setLabel(name.mid(6).toInt());
	} else if (name == "classic") {
		QStringList disks;
		for (Item *item : front()->selectedItems()) {
			if (item->kind == PL_ICON_DISK_IMAGE && !item->isDir) {
				disks << item->path;
			}
		}
		launchClassic(disks);
	} else if (name == "refresh") {
		/* The fallback when a desktop entry appears without the
		 * application directories changing in a way we can see. */
		vfsRefresh();
	} else if (name == "show-unix-volume") {
		vfsSetUnixVolumeShown(!vfsUnixVolumeShown());
	} else if (name == "show-hidden-applications") {
		vfsShowAllHidden();
	} else if (name == "find") {
		FindDialog::open();
	} else if (name == "about") {
		AboutWindow::open();
	} else if (name == "clean-up" || name.startsWith("arrange ")) {
		static const QHash<QString, Arrange> by = {
			{ "clean-up", Arrange::CleanUp }, { "arrange name", Arrange::Name },
			{ "arrange modified", Arrange::Modified }, { "arrange created", Arrange::Created },
			{ "arrange size", Arrange::Size }, { "arrange kind", Arrange::Kind },
			{ "arrange label", Arrange::Label },
		};
		if (by.contains(name)) {
			front()->arrange(by.value(name));
		}
	} else if (name == "view-icons" || name == "view-list" || name == "view-buttons") {
		if (auto *fw = dynamic_cast<FolderWindow *>(front())) {
			fw->setViewMode(name == "view-list" ? FolderWindow::ViewMode::List
				: name == "view-buttons" ? FolderWindow::ViewMode::Buttons
				: FolderWindow::ViewMode::Icons);
		}
	}
}

/* "untitled folder", then "untitled folder 2", ... as the Finder names them. */
static QString uniqueName(const QDir &dir, const QString &base) {
	if (!dir.exists(base)) {
		return base;
	}
	for (int i = 2;; i++) {
		QString name = base + " " + QString::number(i);
		if (!dir.exists(name)) {
			return name;
		}
	}
}

void Finder::newFolder() {
	FinderView *v = front();
	if (v->folderPath() == vfsRoot()) {
		/* The startup disk takes folders of the user's own. */
		const QString path = vfsNewFolder();
		if (!path.isEmpty()) {
			v->selectByName(vfsName(path));
			notifyState();
		}
		return;
	}
	if (vfsIsVirtual(v->folderPath())) {
		Alert::ask("New folders can't be made here. Home, and the "
			"folders inside it, hold your own files.", "OK", QString());
		return;
	}
	QDir dir(v->folderPath());
	dir.mkpath(".");
	QString name = uniqueName(dir, "untitled folder");
	if (dir.mkdir(name)) {
		folderChanged(dir.absolutePath());
		v->selectByName(name);
		notifyState();
	} else {
		Alert::ask("The folder could not be created in " + dir.absolutePath() +
			". Check folder permissions.", "OK", QString());
	}
}

static bool isExpandable(const QString &path);

void Finder::openItem(Item *item) {
	if (appTrashMarker(item->path)) {
		Alert::ask("This application is queued for uninstall when Trash is emptied. "
			"Choose Put Away to cancel removal and return it to Applications.", "OK", QString());
		return;
	}
	{
		if (item->isVirtual) {
			const VNode *node = vfsNode(item->path);
			if (node && node->kind == VKind::Launcher) {
				if (!vfsLaunch(item->path)) {
					Alert::ask(item->name + " could not be opened. Its "
						"application may have been removed.", "OK", QString());
				}
				return;
			}
			if (item->isAlias && !QFileInfo(item->path).exists()) {
				Alert::ask("The original for “" + item->name +
					"” is unavailable. Reconnect its disk or recreate the alias.", "OK", QString());
				return;
			}
			const QString real = vfsOpensAs(item->path);
			FolderWindow::open(real.isEmpty() ? item->path : real);
			return;
		}
		if (item->isAlias && QFileInfo(item->path).symLinkTarget().endsWith(".desktop")) {
			/* An application's alias. */
			if (!appLaunchFile(item->path)) {
				Alert::ask("“" + item->name + "” could not be opened. Its "
					"application may have been removed.", "OK", QString());
			}
			return;
		}
		if (item->kind == PL_ICON_FOLDER || item->kind == PL_ICON_DISK ||
				item->kind == PL_ICON_TRASH_EMPTY || item->kind == PL_ICON_TRASH_FULL) {
			QDir().mkpath(item->path);
			FolderWindow::open(item->path);
		} else if (item->kind == PL_ICON_DISK_IMAGE) {
			mountMacImage(item->path);
		} else if (item->kind == PL_ICON_WINDOWS) {
			launchWindows(item->path);
		} else if (item->kind == PL_ICON_CLASSIC) {
			/* TODO: open the application itself inside the Mac. */
			launchClassic();
		} else if (isExpandable(item->path)) {
			expandArchive(item->path);
		} else {
			if (!appOpenFile(item->path)) {
				Alert::ask(item->name + " could not be opened.", "OK", QString());
			}
		}
	}
}

void Finder::mountMacImage(const QString &image) {
	auto *progress = new QProgressDialog("Mounting " + QFileInfo(image).fileName() + " read-only...",
		QString(), 0, 0);
	progress->setWindowTitle("Mounting Disk");
	progress->setCancelButton(nullptr);
	progress->setWindowModality(Qt::ApplicationModal);
	progress->setMinimumDuration(0);
	progress->show();
	platinumSetFrameStyle(progress, FrameStyle::MovableModal);
	localVolumeMountMacImage(image, [progress](const QString &path, const QString &error) {
		progress->hide();
		progress->deleteLater();
		if (!error.isEmpty()) {
			Alert::ask("The disk image could not be mounted. " + error, "OK", QString());
		} else {
			FolderWindow::open(path);
		}
	});
}

/* ---- archives ------------------------------------------------------------------------ */

static bool isExpandable(const QString &path) {
	const QString lower = path.toLower();
	for (const char *s : { ".zip", ".7z", ".sit", ".tar", ".tgz", ".tar.gz", ".tbz2", ".tar.bz2", ".txz", ".tar.xz" }) {
		if (lower.endsWith(QLatin1String(s))) {
			return true;
		}
	}
	return false;
}

void Finder::expandArchive(const QString &archive) {
	/* Beside us: installed in the same bin directory, or the build tree's copy. */
	const QString tool = QCoreApplication::applicationDirPath() + "/zacos9-expand";
	auto *progress = new QProgressDialog("Expanding “" + QFileInfo(archive).fileName() + "”…",
		QString(), 0, 0);
	progress->setWindowTitle("Expanding");
	progress->setCancelButton(nullptr);
	progress->setWindowModality(Qt::ApplicationModal);
	progress->setMinimumDuration(0);
	progress->setAutoClose(false);
	progress->setAutoReset(false);
	progress->show();
	platinumSetFrameStyle(progress, FrameStyle::MovableModal);
	auto *p = new QProcess;
	QObject::connect(p, &QProcess::finished, [this, p, progress, archive](int code, QProcess::ExitStatus status) {
		progress->hide();
		progress->deleteLater();
		p->deleteLater();
		const QString out = QString::fromUtf8(p->readAllStandardOutput()).trimmed();
		const QString why = QString::fromUtf8(p->readAllStandardError()).trimmed().section('\n', -1);
		if (status != QProcess::NormalExit || code != 0 || out.isEmpty()) {
			Alert::ask("“" + QFileInfo(archive).fileName() + "” couldn’t be expanded. " +
				(why.isEmpty() ? QString("The expanding tool stopped.") : why), "OK", QString());
			return;
		}
		const QFileInfo result(out);
		const QString folder = result.absolutePath();
		folderChanged(folder);
		if (QDir(folder) == QDir(m_desktop->folderPath())) {
			m_desktop->selectByName(result.fileName());
		} else if (FolderWindow::isOpen(folder)) {
			FolderWindow::open(folder)->selectByName(result.fileName());
		}
	});
	QObject::connect(p, &QProcess::errorOccurred, [p, progress, archive](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			progress->hide();
			progress->deleteLater();
			p->deleteLater();
			Alert::ask("“" + QFileInfo(archive).fileName() + "” couldn’t be expanded: zacos9-expand "
				"isn't installed.", "OK", QString());
		}
	});
	p->start(tool, { archive });
}

void Finder::openSelection() {
	for (Item *item : front()->selectedItems()) {
		openItem(item);
	}
}

static QString classicLauncher() {
	const QString local = QCoreApplication::applicationDirPath() + "/zacos9-classic";
	return QFileInfo(local).isExecutable() ? local : QStringLiteral("zacos9-classic");
}

/* Windows programs open in the Windows Installer (shell/wininstall), which
 * installs them, or runs one that needs no installing. */
void Finder::launchWindows(const QString &exe) {
	const QString local = QCoreApplication::applicationDirPath() + "/zacos9-wininstall";
	if (!platinumStartApplication(
			QFileInfo(local).isExecutable() ? local : QStringLiteral("zacos9-wininstall"),
			QStringList{ exe })) {
		Alert::ask("The Windows Installer could not be opened.", "OK", QString());
	}
}

void Finder::launchClassic(const QStringList &disks) {
	QStringList args;
	for (const QString &d : disks) {
		args << "--disk" << d;
	}
	const uint32_t cookie = platinumBeginLaunch();
	auto *check = new QProcess(QCoreApplication::instance());
	auto *timeout = new QTimer(check);
	timeout->setSingleShot(true);
	QObject::connect(timeout, &QTimer::timeout, check, [check] {
		check->setProperty("launchTimedOut", true);
		check->kill();
	});
	QObject::connect(check, &QProcess::errorOccurred, check, [check, cookie](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) {
			platinumCancelLaunch(cookie);
			Alert::ask("Classic could not be started: " + check->errorString(), "OK", QString());
			check->deleteLater();
		}
	});
	QObject::connect(check, &QProcess::finished, check,
		[check, timeout, args, cookie](int code, QProcess::ExitStatus status) {
			timeout->stop();
			platinumCancelLaunch(cookie);
			if (status != QProcess::NormalExit || code != 0) {
				QString why = QString::fromUtf8(check->readAllStandardError()).trimmed();
				if (why.isEmpty()) {
					why = check->property("launchTimedOut").toBool()
						? "Classic's startup check timed out." : "Classic could not be started.";
				}
				Alert::ask(why, "OK", QString());
			} else if (!platinumStartApplication(classicLauncher(), args)) {
				Alert::ask("Classic could not be started.", "OK", QString());
			}
			check->deleteLater();
		});
	check->start(classicLauncher(), QStringList{ "--check" } + args);
	timeout->start(15000);
}

static bool isSpecial(const Item *item) {
	/* Virtual items stand for the Finder's own structure or for
	 * applications dpkg owns: duplicating or aliasing them would mean
	 * copying package files about, so the Finder leaves them alone. */
	return item->isVirtual || item->kind == PL_ICON_DISK ||
		item->kind == PL_ICON_TRASH_EMPTY || item->kind == PL_ICON_TRASH_FULL;
}

/* "Report copy" next to "Report" (Mac OS 8 naming). */
void Finder::duplicate() {
	FinderView *v = front();
	QStringList paths;
	for (Item *item : v->selectedItems()) {
		if (!isSpecial(item)) {
			paths << item->path;
		}
	}
	if (paths.isEmpty()) {
		return;
	}
	const QString folder = QFileInfo(paths.first()).absolutePath();
	for (const QString &d : transferItems(paths, folder, true)) {
		folderChanged(d);
	}
}

/* Labels live in an extended attribute on the item itself (an alias's
 * own, not its original's). The disk and Trash can't be labelled. */
void Finder::setLabel(int label) {
	if (label < 0 || label >= PL_LABEL_COUNT) {
		return;
	}
	QSet<QString> folders;
	for (Item *item : front()->selectedItems()) {
		if (item->isVirtual) {
			/* Kept as metadata, so it survives package upgrades. */
			vfsSetLabel(item->path, label);
			continue;
		}
		if (isSpecial(item)) {
			continue;
		}
		if (writeLabel(item->path, label)) {
			folders.insert(QFileInfo(item->path).absolutePath());
		}
	}
	for (const QString &folder : folders) {
		folderChanged(folder);
	}
}

/* An alias is a symbolic link named "<name> alias"; its name shows in
 * italics. */
void Finder::makeAlias() {
	FinderView *v = front();
	QString lastName;
	QString folder;
	const QString desktopDir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
	bool onDesktop = false;
	for (Item *item : v->selectedItems()) {
		if (item->kind == PL_ICON_TRASH_EMPTY || item->kind == PL_ICON_TRASH_FULL) {
			continue;
		}
		if (item->isVirtual) {
			/* Nothing can be put in the Macintosh view's own folders, so -
			 * as the Mac did when it couldn't write an alias beside its
			 * original - the alias goes on the desktop. An application's
			 * links to its desktop file; a folder standing for a real one,
			 * to that folder. */
			const VNode *node = vfsNode(item->path);
			const QString target = node && node->actionId.isEmpty()
				? vfsRealCounterpart(item->path) : QString();
			if (target.isEmpty()) {
				Alert::ask("An alias of “" + item->name + "” can't be made.", "OK", QString());
				continue;
			}
			QDir().mkpath(desktopDir);
			if (!createAlias(target, desktopDir, item->name).isEmpty()) {
				folderChanged(desktopDir);
				onDesktop = true;
			}
			continue;
		}
		QFileInfo info(item->path);
		folder = item->kind == PL_ICON_DISK ? v->folderPath() : info.absolutePath();
		const QString name = createAlias(item->path, folder,
			item->kind == PL_ICON_DISK ? item->name : info.fileName());
		if (!name.isEmpty()) {
			lastName = name;
		}
	}
	if (!lastName.isEmpty()) {
		folderChanged(folder);
		v->selectByName(lastName);
		notifyState();
	} else if (onDesktop) {
		notifyState();
	}
}

/* Put Away: Trash items go back where they came from (freedesktop
 * trashinfo "Path="). */
void Finder::putAway() {
	const QString trash = trashFilesPath();
	const QString info = QFileInfo(trash).absolutePath() + "/info";
	QStringList changed;
	bool restoredApplication = false;
	for (Item *item : front()->selectedItems()) {
		if (item->isLocalVolume) {
			/* Fire async eject/unmount via GIO; GVolumeMonitor signals
			 * mount-removed when done, refreshing the desktop. */
			for (const LocalVolume &v : localVolumes()) {
				if (v.path == item->path) {
					localVolumeEject(v);
					break;
				}
			}
			continue;
		}
		if (item->isNetworkVolume) {
			/* The same eject this gets from dragging it to the Trash
			 * (fileops.cpp's dropItems): nothing here is a file of
			 * ours to put anywhere. */
			QString error;
			for (const NetVolume &v : netVolumes()) {
				if (v.path == item->path && !netVolumeEject(v, &error)) {
					Alert::ask(error, "OK", QString());
				}
			}
			continue;
		}
		if (QFileInfo(item->path).absolutePath() != QDir(trash).absolutePath()) {
			continue; /* only things in the Trash can be put away */
		}
		if (appTrashMarker(item->path)) {
			QString error;
			if (!appTrashRestore(item->path, &error)) {
				Alert::ask(error, "OK", QString());
			}
			restoredApplication = true;
			continue;
		}
		QFile f(info + "/" + item->name + ".trashinfo");
		if (!f.open(QIODevice::ReadOnly)) {
			continue;
		}
		QString original;
		for (const QByteArray &line : f.readAll().split('\n')) {
			if (line.startsWith("Path=")) {
				original = QUrl::fromPercentEncoding(line.mid(5).trimmed());
			}
		}
		f.close();
		if (original.isEmpty() || QFileInfo::exists(original) ||
				!QDir().mkpath(QFileInfo(original).absolutePath())) {
			continue;
		}
		if (QDir().rename(item->path, original)) {
			QFile::remove(f.fileName());
			changed << QFileInfo(original).absolutePath();
		}
	}
	changed.removeDuplicates();
	if (restoredApplication) {
		vfsRefresh();
	}
	for (const QString &d : changed) {
		folderChanged(d);
	}
	folderChanged(trash);
}

/* Show Original: the folder holding an alias's target, with it selected. */
void Finder::showOriginal() {
	for (Item *item : front()->selectedItems()) {
		QFileInfo info(item->path);
		if (!info.isSymLink()) {
			continue;
		}
		QFileInfo target(info.symLinkTarget());
		if (!target.exists()) {
			continue;
		}
		FolderWindow *w = FolderWindow::open(target.absolutePath());
		w->selectByName(target.fileName());
		notifyState();
		return;
	}
}

/* Info on each selected item, or on the front window's folder; `sharing`
 * shows Sharing rather than General Information. */
void Finder::getInfo(bool sharing) {
	FinderView *v = front();
	std::vector<Item *> items = v->selectedItems();
	const int view = sharing ? InfoWindow::Sharing : InfoWindow::General;
	if (items.empty() && v != m_desktop) {
		InfoWindow::open(v->folderPath(), PL_ICON_FOLDER, displayName(v->folderPath()), view);
	}
	for (Item *item : items) {
		if (item->isVirtual) {
			/* Get Info reads a file. Show the one the item stands for:
			 * a folder's real directory, or an application's desktop
			 * entry. The curated folders stand for nothing on disk.
			 * TODO: a Get Info of their own. */
			const QString real = vfsRealCounterpart(item->path);
			if (!real.isEmpty()) {
				InfoWindow::open(real, item->kind, item->name, view);
			}
			continue;
		}
		InfoWindow::open(item->path, item->kind, item->name, view);
	}
}

void Finder::closeWindow() {
	FinderView *v = front();
	if (v && v != m_desktop) {
		v->widget()->close();
	}
}

void Finder::moveSelectionToTrash() {
	FinderView *v = front();
	QStringList changed;
	QStringList paths;
	for (Item *item : v->selectedItems()) {
		if (item->kind != PL_ICON_DISK && item->kind != PL_ICON_TRASH_EMPTY &&
				item->kind != PL_ICON_TRASH_FULL) {
			paths << item->path;
		}
	}
	for (const QString &path : paths) {
		if (vfsIsAppFolder(path)) {
			const QString id = vfsNode(path)->appId;
			QString error;
			if (appTrashQueue(id, &error)) {
				changed << trashFilesPath();
				vfsRefresh();
			} else {
				Alert::ask(error, "OK", QString());
			}
			continue;
		}
		/* A folder the user made on the startup disk: gone if empty. */
		if (vfsIsUserFolder(path)) {
			if (!vfsDeleteUserFolder(path)) {
				Alert::ask(QStringLiteral("“%1” isn't empty. Move what's inside it to the "
					"Trash first.").arg(displayName(path)), "OK", QString());
			}
			continue;
		}
		/* The disk and the Trash itself can't go in the Trash, and
		 * neither can anything else in the Macintosh view: there is no
		 * file to move. */
		if (vfsIsVirtual(path) || path == trashFilesPath() ||
				QDir(path).isRoot()) {
			continue;
		}
		if (QFile::moveToTrash(path)) {
			changed << QFileInfo(path).absolutePath();
		}
	}
	changed.removeDuplicates();
	for (const QString &folder : changed) {
		folderChanged(folder);
	}
	if (!changed.isEmpty()) {
		pl_sound_event("trash-move");
	}
	folderChanged(trashFilesPath());
}

void Finder::emptyTrash() {
	static bool emptying = false;
	if (emptying) {
		return;
	}
	QScopedValueRollback<bool> guard(emptying, true);
	const QString files = trashFilesPath();
	QString queueError;
	const auto applications = appTrashEntries(&queueError);
	if (!queueError.isEmpty()) {
		Alert::ask(queueError, "OK", QString());
		return;
	}
	int count = 0;
	qint64 bytes = 0;
	QDirIterator it(files, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
		QDir::System, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		it.next();
		count++;
		bytes += it.fileInfo().isFile() ? it.fileInfo().size() : 0;
	}
	if (count == 0) {
		return;
	}
	/* The Mac asks first. */
	QString message = QStringLiteral(
		"The Trash contains %1 %2, which %3 %4 of disk space. Are you sure you "
		"want to permanently remove %5?")
		.arg(count).arg(count == 1 ? "item" : "items").arg(count == 1 ? "uses" : "use")
		.arg(finderSize(bytes)).arg(count == 1 ? "this item" : "these items");
	if (!applications.empty()) {
		QStringList names;
		for (const auto &entry : applications) {
			names << entry.name + " (" + entry.origin + ")";
		}
		message += "\n\nThis will uninstall: " + names.join(", ") +
			". Personal application data is kept. Debian removal may also remove dependent packages.";
	}
	if (!Alert::ask(message)) {
		return;
	}
	if (!applications.empty()) {
		QProgressDialog progress("Uninstalling applications...", QString(), 0, 0);
		progress.setWindowTitle("Uninstalling Applications");
		progress.setWindowModality(Qt::ApplicationModal);
		progress.setMinimumDuration(0);
		progress.setCancelButton(nullptr);
		progress.show();
		platinumSetFrameStyle(&progress, FrameStyle::MovableModal);
		quint64 completed = 0;
		for (const auto &entry : applications) {
			progress.setLabelText(QString("Uninstalling %1 (%2 of %3). Personal data is kept.")
				.arg(entry.name).arg(completed + 1).arg(applications.size()));
			QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
			appRefresh();
			const AppEntry *app = appById(entry.id);
			bool ok = true;
			QString error;
			if (app && app->origin != entry.origin) {
				ok = false;
				error = "The application's install source has changed: " + entry.name;
			} else {
				const QString source = entry.origin.section(':', 0, 0);
				const QString id = entry.origin.section(':', 1);
				bool installed = source == "flatpak" ? flatpakInstalled(id, &ok, &error)
					: packagesInstalled({ id }, &ok, &error);
				if (ok && installed) {
					if (source == "flatpak") {
						runFlatpak({ "uninstall", "--noninteractive", "--", id }, &ok, &error);
					} else {
						runAppstoreHelper({ "remove", id }, &ok, &error);
					}
					if (ok) {
						installed = source == "flatpak" ? flatpakInstalled(id, &ok, &error)
							: packagesInstalled({ id }, &ok, &error);
						if (ok && installed) {
							ok = false;
							error = "The uninstall command finished, but the application is still installed: " +
								entry.name;
						}
					}
				}
			}
			if (ok) {
				ok = appTrashRemove(entry.path, &error);
			}
			if (!ok) {
				progress.hide();
				qWarning().noquote() << error;
				Alert::ask(error + "\nThe application remains queued in Trash; Put Away cancels removal.",
					"OK", QString());
				vfsRefresh();
				folderChanged(files);
				return;
			}
			++completed;
			vfsRefresh();
		}
		progress.hide();
	}
	if (!appTrashEntries(&queueError).empty() || !queueError.isEmpty()) {
		Alert::ask(queueError.isEmpty() ? "New applications were queued while emptying Trash. "
			"Empty Trash again to confirm their removal." : queueError, "OK", QString());
		folderChanged(files);
		return;
	}
	if (!QDir(files).removeRecursively() ||
			!QDir(QFileInfo(files).absolutePath() + "/info").removeRecursively()) {
		Alert::ask("The Trash could not be completely emptied.", "OK", QString());
		folderChanged(files);
		return;
	}
	QDir().mkpath(files);
	pl_sound_event("trash-empty");
	folderChanged(files);
}

void Finder::folderChanged(const QString &folder) {
	const QString abs = QDir(folder).absolutePath();
	FolderWindow::reloadAll(abs);
	FoundWindow::reloadAll();
	if (m_desktop) {
		m_desktop->folderChanged(abs);
	}
	notifyState();
}
