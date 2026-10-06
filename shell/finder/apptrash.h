#pragma once

#include <QString>
#include <vector>

struct TrashedApplication {
	QString path, id, name, origin;
};

bool appTrashMarker(const QString &path);
bool appTrashRead(const QString &path, TrashedApplication *entry, QString *error);
std::vector<TrashedApplication> appTrashEntries(QString *error);
bool appTrashQueue(const QString &id, QString *error);
bool appTrashRestore(const QString &path, QString *error);
bool appTrashRemove(const QString &path, QString *error);
