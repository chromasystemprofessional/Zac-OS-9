#include "finder.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QWidget>

#include <QDirIterator>

#include "alert.h"
#include "desktop.h"
#include "folderwindow.h"

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
	return QStringLiteral("state selection=%1 window=%2 trash=%3")
		.arg(selection).arg(window ? 1 : 0).arg(full ? 1 : 0);
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

/* "12K", "1.4 MB": Finder-style sizes. */
static QString finderSize(qint64 bytes) {
	if (bytes < 1024 * 1024) {
		return QString::number(std::max<qint64>(1, (bytes + 1023) / 1024)) + "K";
	}
	if (bytes < 1024LL * 1024 * 1024) {
		return QString::number(bytes / (1024.0 * 1024), 'f', 1) + " MB";
	}
	return QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 1) + " GB";
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
