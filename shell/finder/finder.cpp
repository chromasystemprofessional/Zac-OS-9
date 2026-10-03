#include "finder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QWidget>

#include <QDirIterator>
#include <QUrl>

#include "fileops.h"
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
	return QStringLiteral("state selection=%1 window=%2 trash=%3 view=%4 label=%5")
		.arg(selection).arg(window ? 1 : 0).arg(full ? 1 : 0).arg(list).arg(label);
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
	} else if (name == "get-info") {
		getInfo();
	} else if (name == "get-info-sharing") {
		getInfo(true);
	} else if (name.startsWith("info /")) {
		/* Get Info for a path (scripts and other apps). */
		const QString path = QDir::cleanPath(name.mid(5));
		InfoWindow::open(path, path == "/" ? PL_ICON_DISK
			: QFileInfo(path).isDir() ? PL_ICON_FOLDER : PL_ICON_DOCUMENT, displayName(path));
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
		launchClassic();
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
	if (vfsIsVirtual(v->folderPath())) {
		Alert::ask("New folders can't be made here. Documents, and the "
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
	}
}

void Finder::openSelection() {
	for (Item *item : front()->selectedItems()) {
		if (item->isVirtual) {
			const VNode *node = vfsNode(item->path);
			if (node && node->kind == VKind::Launcher) {
				if (!vfsLaunch(item->path)) {
					Alert::ask(item->name + " could not be opened. Its "
						"application may have been removed.", "OK", QString());
				}
				continue;
			}
			const QString real = vfsOpensAs(item->path);
			FolderWindow::open(real.isEmpty() ? item->path : real);
			continue;
		}
		if (item->kind == PL_ICON_FOLDER || item->kind == PL_ICON_DISK ||
				item->kind == PL_ICON_TRASH_EMPTY || item->kind == PL_ICON_TRASH_FULL) {
			QDir().mkpath(item->path);
			FolderWindow::open(item->path);
		} else if (item->kind == PL_ICON_DISK_IMAGE) {
			launchClassic({ item->path });
		} else if (item->kind == PL_ICON_CLASSIC) {
			/* TODO: open the application itself inside the Mac. */
			launchClassic();
		} else {
			QProcess::startDetached("xdg-open", { item->path });
		}
	}
}

static QString classicLauncher() {
	const QString local = QCoreApplication::applicationDirPath() + "/zacos9-classic";
	return QFileInfo(local).isExecutable() ? local : QStringLiteral("zacos9-classic");
}

void Finder::launchClassic(const QStringList &disks) {
	QStringList args;
	for (const QString &d : disks) {
		args << "--disk" << d;
	}
	QProcess check;
	check.start(classicLauncher(), QStringList{ "--check" } + args);
	if (!check.waitForFinished(15000) || check.exitStatus() != QProcess::NormalExit ||
			check.exitCode() != 0) {
		QString why = QString::fromUtf8(check.readAllStandardError()).trimmed();
		if (why.isEmpty()) {
			why = "Classic could not be started.";
		}
		Alert::ask(why, "OK", QString());
		return;
	}
	QProcess::startDetached(classicLauncher(), args);
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
	for (Item *item : v->selectedItems()) {
		if (item->kind == PL_ICON_TRASH_EMPTY || item->kind == PL_ICON_TRASH_FULL) {
			continue;
		}
		QFileInfo info(item->path);
		folder = item->kind == PL_ICON_DISK ? v->folderPath() : info.absolutePath();
		QDir dir(folder);
		QString name = (item->kind == PL_ICON_DISK ? item->name : info.fileName()) + " alias";
		for (int i = 2; dir.exists(name); i++) {
			name = info.fileName() + " alias " + QString::number(i);
		}
		if (QFile::link(item->path, dir.filePath(name))) {
			lastName = name;
		}
	}
	if (!lastName.isEmpty()) {
		folderChanged(folder);
		v->selectByName(lastName);
		notifyState();
	}
}

/* Put Away: Trash items go back where they came from (freedesktop
 * trashinfo "Path="). */
void Finder::putAway() {
	const QString trash = trashFilesPath();
	const QString info = QFileInfo(trash).absolutePath() + "/info";
	QStringList changed;
	for (Item *item : front()->selectedItems()) {
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
	for (Item *item : v->selectedItems()) {
		/* An application's folder in Applications: there is no file to
		 * move, and the package stays installed. Trashing it only hides
		 * it from the Finder (vfsHideApplication refreshes any open
		 * window itself, the same way a rename does); ask first, since
		 * unlike an ordinary Trash this can't be undone by fishing the
		 * item back out. */
		if (item->isVirtual && vfsIsAppFolder(item->path)) {
			if (Alert::ask(QStringLiteral("Remove “%1” from the Desktop? "
					"The application itself will not be removed, and this can be "
					"undone with Special > Show All Applications.").arg(item->name),
					"Remove", "Cancel")) {
				vfsHideApplication(item->path);
			}
			continue;
		}
		/* The disk and the Trash itself can't go in the Trash, and
		 * neither can anything else in the Macintosh view: there is no
		 * file to move. */
		if (item->isVirtual || item->kind == PL_ICON_DISK ||
				item->kind == PL_ICON_TRASH_EMPTY ||
				item->kind == PL_ICON_TRASH_FULL) {
			continue;
		}
		if (QFile::moveToTrash(item->path)) {
			changed << QFileInfo(item->path).absolutePath();
		}
	}
	changed.removeDuplicates();
	for (const QString &folder : changed) {
		folderChanged(folder);
	}
	folderChanged(trashFilesPath());
}

void Finder::emptyTrash() {
	const QString files = trashFilesPath();
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
	if (!Alert::ask(message)) {
		return;
	}
	QDir(files).removeRecursively();
	QDir(QFileInfo(files).absolutePath() + "/info").removeRecursively();
	QDir().mkpath(files);
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
