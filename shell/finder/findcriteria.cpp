#include "findcriteria.h"

#include <QDir>
#include <QMimeDatabase>

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
	}
	return {};
}

bool findOpTakesText(FindField field, FindOp op) {
	switch (field) {
	case FindField::Name:
	case FindField::Contents:
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
		}
		if (!ok) {
			return false;
		}
	}
	return true;
}
