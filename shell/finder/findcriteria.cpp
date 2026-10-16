#include "findcriteria.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QSaveFile>
#include <QStandardPaths>
#include <sys/xattr.h>

#include "appdb.h"
#include "clippings.h"
#include "icons.h"
#include "items.h"

QString FindQuery::root() const {
	switch (scope) {
	case FindScope::Home: return QDir::homePath();
	case FindScope::Folder: return folder;
	case FindScope::AllDisks: break;
	}
	return "/";
}

bool FindQuery::hasContents() const {
	for (const FindCriterion &c : criteria) {
		if (c.field == FindField::Contents) {
			return true;
		}
	}
	return false;
}

bool FindQuery::hasVisibilityCriterion() const {
	for (const FindCriterion &c : criteria) {
		if (c.field == FindField::Invisible) {
			return true;
		}
	}
	return false;
}

bool FindQuery::hasVersionCriterion() const {
	for (const FindCriterion &c : criteria) {
		if (c.field == FindField::Version) {
			return true;
		}
	}
	return false;
}

QStringList FindQuery::contentWords() const {
	QStringList words;
	for (const FindCriterion &c : criteria) {
		if (c.field == FindField::Contents) {
			words += c.text.simplified().split(' ', Qt::SkipEmptyParts);
		}
	}
	return words;
}

static bool validDate(const QString &text) {
	return QDate::fromString(text.trimmed(), Qt::ISODate).isValid();
}

bool FindQuery::ready(QString *why) const {
	auto fail = [why](const QString &reason) {
		if (why) {
			*why = reason;
		}
		return false;
	};
	if (criteria.isEmpty()) {
		return fail("Nothing to look for.");
	}
	for (const FindCriterion &c : criteria) {
		if (!findOpTakesText(c.field, c.op)) {
			continue;
		}
		if (c.text.trimmed().isEmpty()) {
			return fail("Type what to look for.");
		}
		if ((c.op == FindOp::Before || c.op == FindOp::After) && !validDate(c.text)) {
			return fail("Type the date as year-month-day, such as 2026-10-14.");
		}
		bool ok = false;
		if ((c.op == FindOp::LargerThan || c.op == FindOp::SmallerThan) &&
				(c.text.trimmed().toLongLong(&ok) < 0 || !ok)) {
			return fail("Type the size as a number of K.");
		}
	}
	if (scope == FindScope::Folder && !QFileInfo(folder).isDir()) {
		return fail("The folder to look in isn't there any more.");
	}
	return true;
}

QString FindQuery::describe() const {
	QStringList parts;
	for (const FindCriterion &c : criteria) {
		QString part = findFieldName(c.field) + " " + findOpName(c.op);
		if (c.field == FindField::Kind) {
			part += " " + findKindNames().value(c.choice);
		} else if (c.field == FindField::Label) {
			part += " " + QString::fromUtf8(pl_labels[std::clamp(c.choice, 0, PL_LABEL_COUNT - 1)].name);
		} else if (findOpTakesText(c.field, c.op)) {
			part += QString(" “%1”").arg(c.text.trimmed());
			if (c.field == FindField::Size) {
				part += " K";
			}
		}
		parts << part;
	}
	return parts.join(" and ");
}

QList<FindOp> findOpsFor(FindField field) {
	switch (field) {
	case FindField::Name:
		return { FindOp::Contains, FindOp::StartsWith, FindOp::EndsWith, FindOp::Is, FindOp::IsNot,
			FindOp::DoesNotContain };
	case FindField::Contents: return { FindOp::Includes };
	case FindField::Modified:
	case FindField::Created:
		return { FindOp::Today, FindOp::PastWeek, FindOp::PastMonth, FindOp::Before, FindOp::After };
	case FindField::Size: return { FindOp::LargerThan, FindOp::SmallerThan };
	case FindField::Kind:
	case FindField::Label: return { FindOp::Is, FindOp::IsNot };
	case FindField::Locked: return { FindOp::IsLocked, FindOp::IsUnlocked };
	case FindField::Folder: return { FindOp::IsEmpty, FindOp::IsShared };
	case FindField::CustomIcon: return { FindOp::Is, FindOp::IsNot };
	case FindField::Comment:
		return { FindOp::Contains, FindOp::Is, FindOp::IsNot, FindOp::DoesNotContain };
	case FindField::Invisible: return { FindOp::Is, FindOp::IsNot };
	case FindField::Version:
		return { FindOp::Contains, FindOp::Is, FindOp::IsNot, FindOp::DoesNotContain };
	}
	return {};
}

QString findOpName(FindOp op) {
	switch (op) {
	case FindOp::Contains: return "contains";
	case FindOp::StartsWith: return "starts with";
	case FindOp::EndsWith: return "ends with";
	case FindOp::Is: return "is";
	case FindOp::IsNot: return "is not";
	case FindOp::DoesNotContain: return "doesn't contain";
	case FindOp::Today: return "is today";
	case FindOp::PastWeek: return "is within the past week";
	case FindOp::PastMonth: return "is within the past month";
	case FindOp::Before: return "is before";
	case FindOp::After: return "is after";
	case FindOp::LargerThan: return "is larger than";
	case FindOp::SmallerThan: return "is smaller than";
	case FindOp::IsLocked: return "is locked";
	case FindOp::IsUnlocked: return "is unlocked";
	case FindOp::IsEmpty: return "is empty";
	case FindOp::IsShared: return "is shared";
	case FindOp::Includes: return "include";
	}
	return {};
}

QString findFieldName(FindField field) {
	switch (field) {
	case FindField::Name: return "name";
	case FindField::Contents: return "contents";
	case FindField::Modified: return "date modified";
	case FindField::Created: return "date created";
	case FindField::Size: return "size";
	case FindField::Kind: return "kind";
	case FindField::Label: return "label";
	case FindField::Locked: return "lock";
	case FindField::Folder: return "folder";
	case FindField::CustomIcon: return "custom icon";
	case FindField::Comment: return "comments";
	case FindField::Invisible: return "invisible";
	case FindField::Version: return "version";
	}
	return {};
}

bool findOpTakesText(FindField field, FindOp op) {
	switch (field) {
	case FindField::Name:
	case FindField::Contents:
	case FindField::Comment:
	case FindField::Version:
	case FindField::Size: return true;
	case FindField::Modified:
	case FindField::Created: return op == FindOp::Before || op == FindOp::After;
	default: return false;
	}
}

QStringList findKindNames() {
	return { "folder", "document", "application", "alias", "picture", "music", "movie", "text document",
		"clipping", "Internet location", "archive", "disk image" };
}

enum Kinds { KFolder, KDocument, KApplication, KAlias, KPicture, KMusic, KMovie, KText, KClipping,
	KLocation, KArchive, KDiskImage };

int findKindOf(const QFileInfo &info) {
	if (info.isSymLink()) {
		return KAlias;
	}
	switch (clippingKind(info.absoluteFilePath())) {
	case ClippingKind::Text:
	case ClippingKind::Picture: return KClipping;
	case ClippingKind::Location: return KLocation;
	case ClippingKind::None: break;
	}
	if (info.isDir()) {
		return isMacDiskImage(info.absoluteFilePath()) ? KDiskImage : KFolder;
	}
	const pl_icon_kind icon = iconKindFor(info.absoluteFilePath());
	if (icon == PL_ICON_DISK_IMAGE) {
		return KDiskImage;
	}
	if (icon == PL_ICON_APPLICATION || icon == PL_ICON_WINDOWS || icon == PL_ICON_CLASSIC) {
		return KApplication;
	}
	static const QMimeDatabase db;
	const QMimeType mime = db.mimeTypeForFile(info);
	const QString name = mime.name();
	if (name.startsWith("image/")) {
		return KPicture;
	}
	if (name.startsWith("audio/")) {
		return KMusic;
	}
	if (name.startsWith("video/")) {
		return KMovie;
	}
	for (const char *archive : { "application/zip", "application/x-7z-compressed", "application/x-tar",
			"application/x-compressed-tar", "application/x-bzip-compressed-tar", "application/x-xz-compressed-tar",
			"application/x-stuffit", "application/gzip", "application/x-rar" }) {
		if (mime.inherits(archive)) {
			return KArchive;
		}
	}
	if (mime.inherits("text/plain")) {
		return KText;
	}
	return KDocument;
}

static bool matchesName(const FindCriterion &c, const QString &name) {
	const QString t = c.text.trimmed();
	const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
	switch (c.op) {
	case FindOp::Contains: return name.contains(t, cs);
	case FindOp::StartsWith: return name.startsWith(t, cs);
	case FindOp::EndsWith: return name.endsWith(t, cs);
	case FindOp::Is: return name.compare(t, cs) == 0;
	case FindOp::IsNot: return name.compare(t, cs) != 0;
	case FindOp::DoesNotContain: return !name.contains(t, cs);
	default: return false;
	}
}

static bool matchesDate(const FindCriterion &c, const QDate &date, const QDate &today) {
	if (!date.isValid()) {
		return false;
	}
	switch (c.op) {
	case FindOp::Today: return date == today;
	case FindOp::PastWeek: return date > today.addDays(-7) && date <= today;
	case FindOp::PastMonth: return date > today.addMonths(-1) && date <= today;
	case FindOp::Before: return date < QDate::fromString(c.text.trimmed(), Qt::ISODate);
	case FindOp::After: return date > QDate::fromString(c.text.trimmed(), Qt::ISODate);
	default: return false;
	}
}

/* Locked as the Mac meant it: you can't change it. */
static bool locked(const QFileInfo &info) {
	return !info.isWritable();
}

static QByteArray comment(const QFileInfo &info) {
	const QByteArray path = QFile::encodeName(info.absoluteFilePath());
	const ssize_t size = getxattr(path.constData(), "user.xdg.comment", nullptr, 0);
	if (size <= 0 || size > 4096) {
		return {};
	}
	QByteArray value(static_cast<int>(size), Qt::Uninitialized);
	if (getxattr(path.constData(), "user.xdg.comment", value.data(),
			static_cast<size_t>(value.size())) != size) {
		return {};
	}
	return value;
}

bool findMatches(const FindQuery &query, const QFileInfo &info, const QDate &today) {
	for (const FindCriterion &c : query.criteria) {
		bool ok = true;
		switch (c.field) {
		case FindField::Name:
			ok = matchesName(c, info.fileName());
			break;
		case FindField::Contents:
			break; /* the index's answer */
		case FindField::Modified:
			ok = matchesDate(c, info.lastModified().date(), today);
			break;
		case FindField::Created: {
			const QDateTime born = info.birthTime();
			ok = matchesDate(c, (born.isValid() ? born : info.metadataChangeTime()).date(), today);
			break;
		}
		case FindField::Size: {
			if (info.isDir()) {
				ok = false;
				break;
			}
			const qint64 k = c.text.trimmed().toLongLong() * 1024;
			ok = c.op == FindOp::LargerThan ? info.size() > k : info.size() < k;
			break;
		}
		case FindField::Kind:
			ok = (findKindOf(info) == c.choice) == (c.op == FindOp::Is);
			break;
		case FindField::Label:
			ok = (readLabel(info.absoluteFilePath()) == c.choice) == (c.op == FindOp::Is);
			break;
		case FindField::Locked:
			ok = locked(info) == (c.op == FindOp::IsLocked);
			break;
		case FindField::Folder:
			if (!info.isDir() || info.isSymLink()) {
				ok = false;
			} else if (c.op == FindOp::IsEmpty) {
				ok = QDir(info.absoluteFilePath()).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
			} else {
				ok = query.sharedFolders.contains(info.canonicalFilePath());
			}
			break;
		case FindField::CustomIcon:
			ok = (!customIconImage(info.absoluteFilePath()).isNull()) == (c.op == FindOp::Is);
			break;
		case FindField::Comment: {
			const QString value = QString::fromUtf8(comment(info));
			const QString text = c.text.trimmed();
			switch (c.op) {
			case FindOp::Contains: ok = value.contains(text, Qt::CaseInsensitive); break;
			case FindOp::Is: ok = value.compare(text, Qt::CaseInsensitive) == 0; break;
			case FindOp::IsNot: ok = value.compare(text, Qt::CaseInsensitive) != 0; break;
			case FindOp::DoesNotContain: ok = !value.contains(text, Qt::CaseInsensitive); break;
			default: ok = false; break;
			}
			break;
		}
		case FindField::Invisible:
			ok = info.isHidden() == (c.op == FindOp::Is);
			break;
		case FindField::Version: {
			const QString value = query.applicationVersions.value(info.canonicalFilePath());
			const QString text = c.text.trimmed();
			switch (c.op) {
			case FindOp::Contains: ok = !value.isEmpty() && value.contains(text, Qt::CaseInsensitive); break;
			case FindOp::Is: ok = !value.isEmpty() && value.compare(text, Qt::CaseInsensitive) == 0; break;
			case FindOp::IsNot: ok = !value.isEmpty() && value.compare(text, Qt::CaseInsensitive) != 0; break;
			case FindOp::DoesNotContain:
				ok = !value.isEmpty() && !value.contains(text, Qt::CaseInsensitive);
				break;
			default: ok = false; break;
			}
			break;
		}
		}
		if (!ok) {
			return false;
		}
	}
	return true;
}

static QString savedFindQueriesPath() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		"/zacos9/find-searches.json";
}

static bool readSavedFindQueries(QJsonObject *root, QString *error) {
	const QString path = savedFindQueriesPath();
	QFile file(path);
	if (!file.exists()) {
		*root = QJsonObject{{"version", 1}, {"searches", QJsonArray{}}};
		return true;
	}
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) *error = "Could not read saved searches: " + file.errorString();
		return false;
	}
	QJsonParseError parse;
	const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse);
	if (parse.error != QJsonParseError::NoError || !doc.isObject() ||
			doc.object().value("version").toInt() != 1 ||
			!doc.object().value("searches").isArray()) {
		if (error) *error = "Saved searches are damaged or use an unsupported format.";
		return false;
	}
	*root = doc.object();
	return true;
}

static bool writeSavedFindQueries(const QJsonObject &root, QString *error) {
	const QString path = savedFindQueriesPath();
	if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
		if (error) *error = "Could not create the saved searches folder.";
		return false;
	}
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		if (error) *error = "Could not write saved searches: " + file.errorString();
		return false;
	}
	const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
	if (file.write(data) != data.size() || !file.commit()) {
		if (error) *error = "Could not save Find searches: " + file.errorString();
		return false;
	}
	return true;
}

QStringList savedFindQueryNames(QString *error) {
	if (error) error->clear();
	QJsonObject root;
	if (!readSavedFindQueries(&root, error)) {
		return {};
	}
	QStringList names;
	for (const QJsonValue &value : root.value("searches").toArray()) {
		if (!value.isObject() || !value.toObject().value("name").isString() ||
				value.toObject().value("name").toString().isEmpty()) {
			if (error) *error = "Saved searches are damaged or use an unsupported format.";
			return {};
		}
		names << value.toObject().value("name").toString();
	}
	names.removeDuplicates();
	names.sort(Qt::CaseInsensitive);
	return names;
}

bool saveFindQuery(const QString &name, const FindQuery &query, QString *error) {
	if (error) error->clear();
	const QString cleanName = name.trimmed();
	if (cleanName.isEmpty() || cleanName.size() > 80 || cleanName.contains(QChar::Null)) {
		if (error) *error = "Enter a saved-search name of 1 to 80 characters.";
		return false;
	}
	QString why;
	if (!query.ready(&why)) {
		if (error) *error = why;
		return false;
	}
	QJsonObject root;
	if (!readSavedFindQueries(&root, error)) {
		return false;
	}
	QJsonArray searches;
	for (const QJsonValue &value : root.value("searches").toArray()) {
		if (!value.isObject() ||
				value.toObject().value("name").toString().compare(cleanName, Qt::CaseInsensitive) != 0) {
			searches.append(value);
		}
	}
	QJsonArray criteria;
	for (const FindCriterion &criterion : query.criteria) {
		criteria.append(QJsonObject{
			{"field", int(criterion.field)}, {"op", int(criterion.op)},
			{"text", criterion.text}, {"choice", criterion.choice},
		});
	}
	searches.append(QJsonObject{
		{"name", cleanName}, {"scope", int(query.scope)}, {"folder", query.folder},
		{"criteria", criteria},
	});
	root.insert("searches", searches);
	return writeSavedFindQueries(root, error);
}

bool loadFindQuery(const QString &name, FindQuery *query, QString *error) {
	if (error) error->clear();
	if (!query) {
		if (error) *error = "No query destination was provided.";
		return false;
	}
	QJsonObject root;
	if (!readSavedFindQueries(&root, error)) {
		return false;
	}
	for (const QJsonValue &value : root.value("searches").toArray()) {
		if (!value.isObject()) {
			continue;
		}
		const QJsonObject saved = value.toObject();
		if (saved.value("name").toString().compare(name, Qt::CaseInsensitive) != 0) {
			continue;
		}
		const int scope = saved.value("scope").toInt(-1);
		const QJsonArray rows = saved.value("criteria").toArray();
		if (scope < int(FindScope::AllDisks) || scope > int(FindScope::Folder) ||
				rows.isEmpty() || rows.size() > 6) {
			break;
		}
		FindQuery loaded;
		loaded.scope = FindScope(scope);
		loaded.folder = saved.value("folder").toString();
		bool valid = true;
		for (const QJsonValue &rowValue : rows) {
			if (!rowValue.isObject()) {
				valid = false;
				break;
			}
			const QJsonObject row = rowValue.toObject();
			const int field = row.value("field").toInt(-1);
			const int op = row.value("op").toInt(-1);
			if (field < int(FindField::Name) || field > int(FindField::Version) ||
					op < int(FindOp::Contains) || op > int(FindOp::IsShared) ||
					!row.value("text").isString() || !row.value("choice").isDouble()) {
				valid = false;
				break;
			}
			const auto fieldOps = findOpsFor(FindField(field));
			if (!fieldOps.contains(FindOp(op))) {
				valid = false;
				break;
			}
			loaded.criteria << FindCriterion{
				FindField(field), FindOp(op), row.value("text").toString(),
				row.value("choice").toInt(),
			};
		}
		if (!valid || loaded.criteria.size() != rows.size()) {
			break;
		}
		QString why;
		if (!loaded.ready(&why)) {
			if (error) *error = "The saved search has invalid criteria: " + why;
			return false;
		}
		*query = loaded;
		return true;
	}
	if (error) *error = "The saved search could not be found or has invalid criteria.";
	return false;
}
