#pragma once

#include <QDateTime>
#include <QString>

#include "items.h"

class QSettings;

/*
 * Mac OS 9's View Options (View > View Options..., Missing Manual pp. 24-25):
 * each window keeps its own for each of its views; Edit > Preferences >
 * Views holds a standard set that new folders start from, and that Set to
 * Standard Views brings a window back to.
 */
enum class IconSize { Standard, Small, Tiny }; /* Tiny: list view only, 12 px */
enum class IconArrangement { None, SnapToGrid, KeepArranged };

/* List columns. The ids are what folders.ini keeps for a window's sort
 * column, so the first four keep the numbers they always had. */
enum ListColumn {
	COL_NAME, COL_MODIFIED, COL_SIZE, COL_KIND,
	COL_CREATED, COL_LABEL, COL_COMMENTS, COL_VERSION,
	COL_COUNT
};
struct ColumnInfo {
	const char *title;
	int width;
	const char *sortName; /* View > Sort List: "by Name" */
	const char *key;      /* in the menu bar's commands: "sort name" */
};
const ColumnInfo &columnInfo(int column);
/* The order the columns appear in, left to right (the Mac's). */
extern const int COLUMN_ORDER[COL_COUNT];
inline constexpr unsigned columnBit(int column) { return 1u << column; }
/* Name and the Mac's default three: Date Modified, Size, Kind. */
inline constexpr unsigned DEFAULT_COLUMNS =
	columnBit(COL_NAME) | columnBit(COL_MODIFIED) | columnBit(COL_SIZE) | columnBit(COL_KIND);

struct IconViewOptions {
	IconSize size = IconSize::Standard;
	IconArrangement arrangement = IconArrangement::None;
	Arrange keepBy = Arrange::Name; /* for Keep Arranged */
	bool operator==(const IconViewOptions &o) const {
		return size == o.size && arrangement == o.arrangement && keepBy == o.keepBy;
	}
};

struct ListViewOptions {
	IconSize size = IconSize::Small;
	bool relativeDate = true;
	bool folderSizes = false;
	unsigned columns = DEFAULT_COLUMNS; /* columnBit()s; Name always */
	bool operator==(const ListViewOptions &o) const {
		return size == o.size && relativeDate == o.relativeDate && folderSizes == o.folderSizes &&
			columns == o.columns;
	}
};

struct ViewOptions {
	IconViewOptions icons, buttons; /* buttons: size, and the order kept */
	ListViewOptions list;
	bool operator==(const ViewOptions &o) const {
		return icons == o.icons && buttons == o.buttons && list == o.list;
	}
	bool operator!=(const ViewOptions &o) const { return !(*this == o); }

	/* In the current group of `st`; what's missing comes from `fallback`. */
	static ViewOptions load(QSettings &st, const ViewOptions &fallback);
	void save(QSettings &st) const;
};

/* Preferences > Views. */
ViewOptions standardViews();
void setStandardViews(const ViewOptions &options);

/* Preferences > General (desktop.conf). */
bool springLoadedFolders();      /* "spring-loaded", on unless "false" */
int springDelayMs();             /* "spring-delay", 900 by default */
bool tightGrid();                /* "grid-spacing=tight"; Wide by default */

/* Arrange's names in settings and commands: "name", "modified", ... */
QString arrangeKey(Arrange how);
Arrange arrangeFromKey(const QString &key, Arrange fallback = Arrange::Name);

/* A list view's cell for `column` (not Name); dates as listDate(). */
QString columnText(Item &item, int column, const ListViewOptions &options,
	const QDateTime &now = QDateTime::currentDateTime());
/* The list's order for `column`, as the Mac's: names A-Z, dates newest
 * first, sizes largest first, kinds, labels and comments A-Z, the
 * unlabelled and those without a comment or version last. */
void sortByColumn(std::vector<Item *> &items, int column);
/* The lazily read columns. */
const QDateTime &itemCreated(Item &item);
const QString &itemComment(Item &item);
const QString &itemVersion(Item &item);

/* A list view's date: "Today, 3:42 PM" (and Yesterday, Tomorrow) with
 * relative dates on, else the full date. */
QString listDate(const QDateTime &t, bool relative, const QDateTime &now = QDateTime::currentDateTime());
