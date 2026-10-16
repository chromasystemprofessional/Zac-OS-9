#include "foldertricks.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

namespace {

/* Names the menu bar can show: one line, no tabs. */
bool showable(const QString &name) {
	return !name.isEmpty() && !name.contains('\t') && !name.contains('\n');
}

/* Downloads and copies still being written, renamed when they're done. */
bool partial(const QString &name) {
	static const char *const suffixes[] = { ".part", ".crdownload", ".download", ".partial" };
	for (const char *suffix : suffixes) {
		if (name.endsWith(QLatin1String(suffix), Qt::CaseInsensitive)) {
			return true;
		}
	}
	return name.startsWith('.');
}

QString canonical(const QString &folder) {
	const QString path = QFileInfo(folder).canonicalFilePath();
	return path.isEmpty() ? QDir::cleanPath(folder) : path;
}

} // namespace

QString FolderTricks::defaultConfig() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		"/zacos9/folder-tricks.json";
}

QString FolderTricks::defaultTricksDir() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		"/zacos9/Folder Tricks";
}

FolderTricks &FolderTricks::instance() {
	static FolderTricks tricks;
	return tricks;
}

FolderTricks::FolderTricks(const QString &config, const QString &tricks)
		: m_config(config), m_tricksDir(tricks), m_watcher(new QFileSystemWatcher) {
	QObject::connect(m_watcher.get(), &QFileSystemWatcher::directoryChanged, m_watcher.get(),
		[this](const QString &path) { changed(path); });
	load();
	for (auto it = m_attached.cbegin(); it != m_attached.cend(); ++it) {
		watch(it.key());
	}
}

FolderTricks::~FolderTricks() {
	qDeleteAll(m_timers);
}

void FolderTricks::load() {
	QFile f(m_config);
	if (!f.open(QIODevice::ReadOnly)) {
		return;
	}
	const QJsonArray folders = QJsonDocument::fromJson(f.readAll()).object()
		.value("folders").toArray();
	for (const QJsonValue &value : folders) {
		const QJsonObject o = value.toObject();
		const QString folder = o.value("folder").toString();
		QStringList tricks;
		for (const QJsonValue &trick : o.value("tricks").toArray()) {
			if (showable(trick.toString()) && !tricks.contains(trick.toString())) {
				tricks << trick.toString();
			}
		}
		if (!folder.isEmpty() && !tricks.isEmpty()) {
			m_attached.insert(folder, tricks);
		}
	}
}

bool FolderTricks::save() const {
	QJsonArray folders;
	for (auto it = m_attached.cbegin(); it != m_attached.cend(); ++it) {
		folders.append(QJsonObject{ { "folder", it.key() },
			{ "tricks", QJsonArray::fromStringList(it.value()) } });
	}
	QDir().mkpath(QFileInfo(m_config).absolutePath());
	QSaveFile f(m_config);
	if (!f.open(QIODevice::WriteOnly)) {
		return false;
	}
	f.write(QJsonDocument(QJsonObject{ { "folders", folders } }).toJson());
	return f.commit();
}

QStringList FolderTricks::available() const {
	QStringList names;
	const QFileInfoList files = QDir(m_tricksDir).entryInfoList(QDir::Files | QDir::Executable,
		QDir::Name | QDir::IgnoreCase);
	for (const QFileInfo &file : files) {
		if (showable(file.fileName())) {
			names << file.fileName();
		}
	}
	return names;
}

QStringList FolderTricks::attached(const QString &folder) const {
	return m_attached.value(canonical(folder));
}

bool FolderTricks::attach(const QString &folder, const QString &trick) {
	const QString key = canonical(folder);
	if (!QFileInfo(key).isDir() || !available().contains(trick)) {
		return false;
	}
	QStringList &tricks = m_attached[key];
	if (!tricks.contains(trick)) {
		tricks << trick;
	}
	watch(key);
	return save();
}

bool FolderTricks::remove(const QString &folder, const QString &trick) {
	const QString key = canonical(folder);
	auto it = m_attached.find(key);
	if (it == m_attached.end() || !it->removeAll(trick)) {
		return false;
	}
	if (it->isEmpty()) {
		m_attached.erase(it);
		m_watcher->removePath(key);
		m_seen.remove(key);
		delete m_timers.take(key);
	}
	return save();
}

QSet<QString> FolderTricks::entries(const QString &folder) const {
	QSet<QString> names;
	const QStringList list = QDir(folder).entryList(QDir::AllEntries | QDir::NoDotAndDotDot |
		QDir::System);
	for (const QString &name : list) {
		if (!partial(name)) {
			names.insert(name);
		}
	}
	return names;
}

void FolderTricks::watch(const QString &folder) {
	if (!QFileInfo(folder).isDir()) {
		return;
	}
	if (!m_watcher->directories().contains(folder)) {
		m_watcher->addPath(folder);
	}
	if (!m_seen.contains(folder)) {
		m_seen.insert(folder, entries(folder));
	}
}

void FolderTricks::changed(const QString &folder) {
	if (!m_attached.contains(folder)) {
		return;
	}
	QTimer *&timer = m_timers[folder];
	if (!timer) {
		timer = new QTimer;
		timer->setSingleShot(true);
		QObject::connect(timer, &QTimer::timeout, timer, [this, folder] { settle(folder); });
	}
	timer->start(settleMs);
}

void FolderTricks::settle(const QString &folder) {
	if (!QFileInfo(folder).isDir()) {
		m_seen.remove(folder); /* gone: watched again if it comes back */
		return;
	}
	watch(folder); /* replaced folders drop out of the watcher */
	const QSet<QString> now = entries(folder);
	QStringList added;
	for (const QString &name : now) {
		if (!m_seen.value(folder).contains(name)) {
			added << QDir(folder).filePath(name);
		}
	}
	m_seen[folder] = now;
	if (added.isEmpty()) {
		return;
	}
	added.sort();
	for (const QString &trick : m_attached.value(folder)) {
		const QString program = QDir(m_tricksDir).filePath(trick);
		if (!QFileInfo(program).isExecutable() ||
				!QProcess::startDetached(program, QStringList{ folder } + added, folder)) {
			qWarning("Folder Tricks: couldn't run %s", qPrintable(program));
			continue;
		}
		if (ran) {
			ran(trick, folder, added);
		}
	}
}
