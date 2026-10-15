#pragma once

/*
 * What Find looks for (Mac OS 9's Find, Missing Manual ch. 15): where to
 * look, and rows of criteria that must all match: "name contains notes",
 * "date modified is within the past week", "size is larger than 100 K",
 * "kind is picture", "label is Hot", "contents include budget"...
 * Everything but contents is checked against the file itself; contents
 * come from the search index (findindex.h).
 */

#include <QDate>
#include <QFileInfo>
#include <QList>
#include <QString>
#include <QStringList>

enum class FindField { Name, Contents, Modified, Created, Size, Kind, Label, Locked, Folder };
enum class FindOp {
	/* name */
	Contains, StartsWith, EndsWith, Is, IsNot, DoesNotContain,
	/* dates */
	Today, PastWeek, PastMonth, Before, After,
	/* size */
	LargerThan, SmallerThan,
	/* kind, label (Is, IsNot); locked; folder */
	IsLocked, IsUnlocked, IsEmpty, IsShared,
	/* contents */
	Includes,
};

struct FindCriterion {
	FindField field = FindField::Name;
	FindOp op = FindOp::Contains;
	QString text; /* name or contents words; a size in K; a date (yyyy-MM-dd) */
	int choice = 0; /* a kind (findKindNames) or a label (pl_labels) */
};

enum class FindScope { AllDisks, Home, Folder };

struct FindQuery {
	FindScope scope = FindScope::AllDisks;
	QString folder; /* for FindScope::Folder */
	QList<FindCriterion> criteria;
	/* Folders shared from Get Info, read before the search starts (the
	 * search runs on another thread). */
	QStringList sharedFolders;

	/* Where the search starts. */
	QString root() const;
	bool hasContents() const;
	/* The words every contents row asks for. */
	QStringList contentWords() const;
	/* Enough filled in to search with; else why not. */
	bool ready(QString *why = nullptr) const;
	/* "name contains “notes” and size is larger than 100 K", for the window. */
	QString describe() const;
};

/* The ops each field offers, in menu order, and their names. */
QList<FindOp> findOpsFor(FindField field);
QString findOpName(FindOp op);
QString findFieldName(FindField field);
/* Does the row take typed text, and of what sort? */
bool findOpTakesText(FindField field, FindOp op);

/* The kinds Find knows ("folder", "application", "picture"...). */
QStringList findKindNames();
/* Which of them a file is. */
int findKindOf(const QFileInfo &info);

/* Does a file pass every row but contents? `today` for the date rows. */
bool findMatches(const FindQuery &query, const QFileInfo &info, const QDate &today = QDate::currentDate());
