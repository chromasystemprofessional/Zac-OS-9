#pragma once

#include <QString>
#include <QStringList>
#include <vector>

#include "icons.h"

enum class ResourceType {
	FilesystemAlias,
	Collection,
	Information,
};

struct SystemResource {
	QString key;
	QString name;
	ResourceType type = ResourceType::Information;
	QString source;
	QString kind;
	QString description;
	QStringList metadata;
	bool directory = false;
	/* The Finder icon; a directory always shows as a folder. */
	pl_icon_kind icon = PL_ICON_DOCUMENT;
};

struct ResourceDetails {
	QString name;
	QString kind;
	QString location;
	QString sources;
	QString description;
	QString access;
	QString version;
	QString ownership;
	QString status;
	QString startup;
	QStringList metadata;
};

/* Provider IDs are a fixed allowlist; neither provider roots nor commands
 * are read from the Finder registry. Children are discovered on demand. */
bool resourceProviderKnown(const QString &provider);
std::vector<SystemResource> resourceChildren(const QString &provider,
		const QString &key = QString());
bool resourceOpenPath(const QString &provider, const QString &key, QString *path);
ResourceDetails resourceDetails(const QString &provider, const QString &key);
QStringList systemInformation();
QStringList extensionInformation();
QString resourceTestFixtureRoot();
