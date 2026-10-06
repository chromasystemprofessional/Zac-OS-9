#include "apptrash.h"
#include "appdb.h"
#include "items.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

bool appTrashMarker(const QString &path) {
	return QFileInfo(path).absolutePath() == QDir(trashFilesPath()).absolutePath() &&
		QRegularExpression("\\Azacos9-app-[0-9a-f-]{36}\\.json\\z")
			.match(QFileInfo(path).fileName()).hasMatch();
}

bool appTrashRead(const QString &path, TrashedApplication *entry, QString *error) {
	QFile file(path);
	if (!appTrashMarker(path) || QFileInfo(path).isSymLink() ||
			!file.open(QIODevice::ReadOnly) || file.size() > 16384) {
		*error = "The queued application cannot be read: " + path;
		return false;
	}
	const QJsonObject record = QJsonDocument::fromJson(file.readAll()).object();
	const QString id = record.value("id").toString();
	const QString name = record.value("name").toString();
	const QString origin = record.value("origin").toString();
	const QRegularExpression package("\\Adpkg:[a-z0-9][a-z0-9+.-]+\\z");
	const QRegularExpression flatpak("\\Aflatpak:[A-Za-z_][A-Za-z0-9_]*(?:\\.[A-Za-z_][A-Za-z0-9_-]*){2,}\\z");
	if (record.value("type").toString() != "zacos9-application-trash" ||
			record.value("version").toInt() != 1 || id.isEmpty() || id.contains('/') ||
			!id.endsWith(".desktop") || name.isEmpty() ||
			(!package.match(origin).hasMatch() && !flatpak.match(origin).hasMatch())) {
		*error = "Invalid queued application: " + path;
		return false;
	}
	*entry = { path, id, name, origin };
	return true;
}

std::vector<TrashedApplication> appTrashEntries(QString *error) {
	error->clear();
	std::vector<TrashedApplication> entries;
	for (const QString &name : QDir(trashFilesPath()).entryList(
			{ "zacos9-app-*.json" }, QDir::Files | QDir::System)) {
		TrashedApplication entry;
		if (!appTrashRead(QDir(trashFilesPath()).filePath(name), &entry, error)) {
			return {};
		}
		entries.push_back(std::move(entry));
	}
	return entries;
}

bool appTrashQueue(const QString &id, QString *error) {
	error->clear();
	const AppEntry *app = appById(id);
	if (!app || (!app->origin.startsWith("dpkg:") && !app->origin.startsWith("flatpak:"))) {
		*error = "This application has no supported Debian or Flatpak uninstall source. "
			"Remove it using its own installer; it has not been moved to Trash.";
		return false;
	}
	if (app->origin.startsWith("flatpak:") &&
			QFileInfo(app->file).canonicalFilePath().startsWith("/var/lib/flatpak/")) {
		*error = "This is a system-wide Flatpak application. Remove it with the system "
			"Flatpak administrator tools; it has not been moved to Trash.";
		return false;
	}
	const auto entries = appTrashEntries(error);
	if (!error->isEmpty()) {
		return false;
	}
	for (const auto &entry : entries) {
		if (entry.id == id) {
			return true;
		}
	}
	if (!QDir().mkpath(trashFilesPath())) {
		*error = "The Trash folder could not be created.";
		return false;
	}
	const QString path = QDir(trashFilesPath()).filePath(
		"zacos9-app-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".json");
	QSaveFile file(path);
	const QByteArray data = QJsonDocument(QJsonObject{
		{ "type", "zacos9-application-trash" }, { "version", 1 },
		{ "id", app->id }, { "name", app->name }, { "origin", app->origin }
	}).toJson();
	if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
		*error = "Unable to queue the application in Trash: " + file.errorString();
		return false;
	}
	TrashedApplication checked;
	if (!appTrashRead(path, &checked, error)) {
		if (!QFile::remove(path)) {
			*error += "\nThe invalid queue record could not be cleaned up: " + path;
		}
		return false;
	}
	return true;
}

bool appTrashRemove(const QString &path, QString *error) {
	TrashedApplication entry;
	if (!appTrashRead(path, &entry, error)) {
		return false;
	}
	if (!QFile::remove(path)) {
		*error = "The queued application could not be removed from Trash: " + entry.name;
		return false;
	}
	return true;
}

bool appTrashRestore(const QString &path, QString *error) {
	return appTrashRemove(path, error);
}
