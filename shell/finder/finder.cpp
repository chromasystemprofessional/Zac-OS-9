#include "finder.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QWidget>

#include <QDirIterator>
#include <QUrl>

#include "fileops.h"

#include "alert.h"
#include "desktop.h"
#include "folderwindow.h"
#include "infowindow.h"

Finder &Finder::instance() {
	static Finder finder;
	return finder;
}

QString Finder::socketPath() {
	QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR", "/tmp");
	QString display = qEnvironmentVariable("WAYLAND_DISPLAY", "wayland-0");
	return runtime + "/platinum-finder." + display + ".sock";
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
	int list = fw && fw->viewMode() == FolderWindow::ViewMode::List ? 1 : 0;
	return QStringLiteral("state selection=%1 window=%2 trash=%3 view=%4")
		.arg(selection).arg(window ? 1 : 0).arg(full ? 1 : 0).arg(list);
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
	} else if (name == "duplicate") {
		duplicate();
	} else if (name == "make-alias") {
		makeAlias();
	} else if (name == "put-away") {
		putAway();
	} else if (name == "show-original") {
		showOriginal();
	} else if (name == "about") {
		AboutWindow::open();
	} else if (name == "view-icons" || name == "view-list") {
		if (auto *fw = dynamic_cast<FolderWindow *>(front())) {
			fw->setViewMode(name == "view-list" ? FolderWindow::ViewMode::List
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
		if (item->kind == PL_ICON_FOLDER || item->kind == PL_ICON_DISK ||
				item->kind == PL_ICON_TRASH_EMPTY || item->kind == PL_ICON_TRASH_FULL) {
			QDir().mkpath(item->path);
			FolderWindow::open(item->path);
		} else {
			QProcess::startDetached("xdg-open", { item->path });
		}
	}
}

static bool isSpecial(const Item *item) {
	return item->kind == PL_ICON_DISK || item->kind == PL_ICON_TRASH_EMPTY ||
		item->kind == PL_ICON_TRASH_FULL;
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

/* An alias is a symbolic link named "<name> alias".
 * TODO: the Mac shows alias names in italics (needs an italic views font). */
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

/* Info on each selected item, or on the front window's folder. */
void Finder::getInfo() {
	FinderView *v = front();
	std::vector<Item *> items = v->selectedItems();
	if (items.empty() && v != m_desktop) {
		InfoWindow::open(v->folderPath(), PL_ICON_FOLDER, displayName(v->folderPath()));
	}
	for (Item *item : items) {
		InfoWindow::open(item->path, item->kind, item->name);
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
		/* The disk and the Trash itself can't go in the Trash. */
		if (item->kind == PL_ICON_DISK || item->kind == PL_ICON_TRASH_EMPTY ||
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
	if (m_desktop) {
		m_desktop->folderChanged(abs);
	}
	notifyState();
}
