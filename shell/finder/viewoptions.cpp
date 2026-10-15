#include "viewoptions.h"

#include <QLocale>
#include <QVariant>
#include <cstring>
#include <QSettings>
#include <QStandardPaths>

#include "appdb.h"
#include "pixels.h"
#include "settings.h"
#include "vfs.h"

#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <sys/xattr.h>

QString finderDate(const QDateTime &t); /* infowindow.cpp */
QString finderSize(qint64 bytes);

static const ColumnInfo COLUMNS[COL_COUNT] = {
	{ "Name", 220, "by Name", "name" },
	{ "Date Modified", 170, "by Date Modified", "modified" },
	{ "Size", 64, "by Size", "size" },
	{ "Kind", 140, "by Kind", "kind" },
	{ "Date Created", 170, "by Date Created", "created" },
	{ "Label", 90, "by Label", "label" },
	{ "Comments", 200, "by Comments", "comments" },
	{ "Version", 90, "by Version", "version" },
};

const int COLUMN_ORDER[COL_COUNT] = {
	COL_NAME, COL_MODIFIED, COL_CREATED, COL_SIZE, COL_KIND, COL_LABEL, COL_COMMENTS, COL_VERSION,
};

int columnFromKey(const QString &key) {
	for (int c = 0; c < COL_COUNT; c++) {
		if (key == QLatin1String(COLUMNS[c].key)) {
			return c;
		}
	}
	return -1;
}

const ColumnInfo &columnInfo(int column) {
	return COLUMNS[column >= 0 && column < COL_COUNT ? column : 0];
}

static const struct {
	Arrange how;
	const char *key;
} ARRANGE_KEYS[] = {
	{ Arrange::Name, "name" }, { Arrange::Modified, "modified" }, { Arrange::Created, "created" },
	{ Arrange::Size, "size" }, { Arrange::Kind, "kind" }, { Arrange::Label, "label" },
};

QString arrangeKey(Arrange how) {
	for (const auto &a : ARRANGE_KEYS) {
		if (a.how == how) {
			return a.key;
		}
	}
	return "name";
}

Arrange arrangeFromKey(const QString &key, Arrange fallback) {
	for (const auto &a : ARRANGE_KEYS) {
		if (key == QLatin1String(a.key)) {
			return a.how;
		}
	}
	return fallback;
}

static const char *const SIZE_KEYS[] = { "standard", "small", "tiny" };
static const char *const ARRANGEMENT_KEYS[] = { "none", "snap", "keep" };

template <typename E, size_t N>
static E fromKey(const QVariant &v, const char *const (&keys)[N], E fallback) {
	const QString s = v.toString();
	for (size_t i = 0; i < N; i++) {
		if (s == QLatin1String(keys[i])) {
			return static_cast<E>(i);
		}
	}
	return fallback;
}

static IconViewOptions loadIcons(QSettings &st, const QString &prefix, const IconViewOptions &d) {
	IconViewOptions o;
	o.size = fromKey(st.value(prefix + "-size"), SIZE_KEYS, d.size);
	if (o.size == IconSize::Tiny) {
		o.size = IconSize::Small;
	}
	o.arrangement = fromKey(st.value(prefix + "-arrange"), ARRANGEMENT_KEYS, d.arrangement);
	o.keepBy = arrangeFromKey(st.value(prefix + "-by").toString(), d.keepBy);
	return o;
}

static void saveIcons(QSettings &st, const QString &prefix, const IconViewOptions &o) {
	st.setValue(prefix + "-size", SIZE_KEYS[static_cast<int>(o.size)]);
	st.setValue(prefix + "-arrange", ARRANGEMENT_KEYS[static_cast<int>(o.arrangement)]);
	st.setValue(prefix + "-by", arrangeKey(o.keepBy));
}

ViewOptions ViewOptions::load(QSettings &st, const ViewOptions &d) {
	ViewOptions o;
	o.icons = loadIcons(st, "icons", d.icons);
	o.buttons = loadIcons(st, "buttons", d.buttons);
	o.list.size = fromKey(st.value("list-size"), SIZE_KEYS, d.list.size);
	o.list.relativeDate = st.value("list-relative-date", d.list.relativeDate).toBool();
	o.list.folderSizes = st.value("list-folder-sizes", d.list.folderSizes).toBool();
	if (st.contains("list-columns")) {
		o.list.columns = columnBit(COL_NAME);
		for (const QString &key : st.value("list-columns").toString().split(',', Qt::SkipEmptyParts)) {
			for (int c = 0; c < COL_COUNT; c++) {
				if (key == QLatin1String(COLUMNS[c].key)) {
					o.list.columns |= columnBit(c);
				}
			}
		}
	} else {
		o.list.columns = d.list.columns;
	}
	return o;
}

void ViewOptions::save(QSettings &st) const {
	saveIcons(st, "icons", icons);
	saveIcons(st, "buttons", buttons);
	st.setValue("list-size", SIZE_KEYS[static_cast<int>(list.size)]);
	st.setValue("list-relative-date", list.relativeDate);
	st.setValue("list-folder-sizes", list.folderSizes);
	QStringList keys;
	for (int c = 1; c < COL_COUNT; c++) {
		if (list.columns & columnBit(c)) {
			keys << COLUMNS[c].key;
		}
	}
	/* An empty value would read back as unset: "name" stands for none. */
	st.setValue("list-columns", keys.isEmpty() ? QStringLiteral("name") : keys.join(','));
}

static QString standardViewsFile() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
		"/zacos9/finder/standard-views.ini";
}

ViewOptions standardViews() {
	QSettings st(standardViewsFile(), QSettings::IniFormat);
	st.beginGroup("views");
	return ViewOptions::load(st, ViewOptions{});
}

void setStandardViews(const ViewOptions &options) {
	QSettings st(standardViewsFile(), QSettings::IniFormat);
	st.beginGroup("views");
	options.save(st);
	st.endGroup();
	st.sync();
}

bool springLoadedFolders() {
	char v[16];
	return !(pl_setting("spring-loaded", v, sizeof(v)) && strcmp(v, "false") == 0);
}

bool trashWarns() {
	char v[16];
	return !(pl_setting("trash-warn", v, sizeof(v)) && strcmp(v, "false") == 0);
}

void setTrashWarns(bool on) {
	pl_setting_set("trash-warn", on ? nullptr : "false");
}

int springDelayMs() {
	char v[16];
	const int ms = pl_setting("spring-delay", v, sizeof(v)) ? atoi(v) : 0;
	return ms >= 100 && ms <= 3000 ? ms : 900;
}

bool tightGrid() {
	char v[16];
	return pl_setting("grid-spacing", v, sizeof(v)) && strcmp(v, "tight") == 0;
}

const QDateTime &itemCreated(Item &item) {
	if (!item.createdResolved) {
		item.createdResolved = true;
		if (!item.isVirtual) {
			item.created = QFileInfo(item.path).birthTime();
		}
	}
	return item.created;
}

const QString &itemComment(Item &item) {
	if (!item.commentResolved) {
		item.commentResolved = true;
		if (!item.isVirtual) {
			char buf[1024];
			const ssize_t n = getxattr(QFile::encodeName(item.path).constData(), "user.xdg.comment",
				buf, sizeof(buf));
			if (n > 0) {
				item.comment = QString::fromUtf8(buf, static_cast<int>(n)).simplified();
			}
		}
	}
	return item.comment;
}

/* Applications have versions: their package's (the Mac showed an
 * application's own); documents and folders none. */
const QString &itemVersion(Item &item) {
	if (!item.versionResolved) {
		item.versionResolved = true;
		const AppEntry *app = nullptr;
		if (item.isVirtual) {
			const VNode *node = vfsNode(item.path);
			app = node && !node->appId.isEmpty() ? appById(node->appId) : nullptr;
		} else if (item.kind == PL_ICON_APPLICATION) {
			app = appByFile(QFileInfo(item.path).canonicalFilePath());
		}
		item.version = app ? app->version : QString();
	}
	return item.version;
}

static int textWidth(const QString &s) {
	Text t(s, 100000, PL_FONT_VIEWS);
	return t.inkWidth();
}

QString fitListDate(const QDateTime &t, bool relative, int width, const QDateTime &now) {
	if (!t.isValid()) {
		return QStringLiteral("--");
	}
	const QLocale en(QLocale::English);
	QStringList forms;
	const qint64 days = now.date().daysTo(t.date());
	const char *word = !relative ? nullptr : days == 0 ? "Today" : days == -1 ? "Yesterday" :
		days == 1 ? "Tomorrow" : nullptr;
	if (word) {
		forms << QString(word) + ", " + en.toString(t, "h:mm AP") << QString(word);
	} else {
		forms << en.toString(t, "dddd, MMMM d, yyyy, h:mm AP") << en.toString(t, "ddd, MMM d, yyyy, h:mm AP")
			<< en.toString(t, "M/d/yy, h:mm AP") << en.toString(t, "M/d/yy");
	}
	for (const QString &form : forms) {
		if (textWidth(form) <= width) {
			return form;
		}
	}
	return forms.last();
}

/* "12.3 MB", or "12 MB" when that doesn't fit. */
static QString fitSize(qint64 bytes, int width) {
	const QString size = finderSize(bytes);
	if (textWidth(size) <= width || !size.contains('.')) {
		return size;
	}
	return QString::number(qRound(size.section(' ', 0, 0).toDouble())) + " " + size.section(' ', 1);
}

QString columnText(Item &item, int column, const ListViewOptions &options, int width,
		const QDateTime &now) {
	switch (column) {
	case COL_MODIFIED:
		return fitListDate(item.modified, options.relativeDate, width, now);
	case COL_CREATED:
		return fitListDate(itemCreated(item), options.relativeDate, width, now);
	case COL_SIZE:
		if (!item.isDir) {
			return fitSize(item.size, width);
		}
		return options.folderSizes && item.folderSize >= 0 ? fitSize(item.folderSize, width) :
			QStringLiteral("--");
	case COL_KIND:
		return item.kindName();
	case COL_LABEL:
		return item.labelIndex ? QString::fromUtf8(pl_label_name(item.labelIndex)) :
			QStringLiteral("-");
	case COL_COMMENTS:
		return itemComment(item);
	case COL_VERSION:
		return itemVersion(item).isEmpty() ? QStringLiteral("n/a") : itemVersion(item);
	default:
		return item.visibleName();
	}
}

void sortByColumn(std::vector<Item *> &items, int column) {
	const auto byName = [](Item *a, Item *b) {
		return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
	};
	/* Text A-Z, empty last, then by name. */
	const auto text = [&](const QString &ta, const QString &tb, Item *a, Item *b) {
		if (ta.isEmpty() != tb.isEmpty()) {
			return tb.isEmpty();
		}
		const int c = ta.compare(tb, Qt::CaseInsensitive);
		return c != 0 ? c < 0 : byName(a, b);
	};
	const auto newest = [&](const QDateTime &da, const QDateTime &db, Item *a, Item *b) {
		if (da.isValid() != db.isValid()) {
			return da.isValid();
		}
		return da != db ? da > db : byName(a, b);
	};
	switch (column) {
	case COL_MODIFIED:
		std::stable_sort(items.begin(), items.end(), [&](Item *a, Item *b) {
			return newest(a->modified, b->modified, a, b);
		});
		break;
	case COL_CREATED:
		std::stable_sort(items.begin(), items.end(), [&](Item *a, Item *b) {
			return newest(itemCreated(*a), itemCreated(*b), a, b);
		});
		break;
	case COL_SIZE: /* largest first; folders by their contents if added up, else last */
		std::stable_sort(items.begin(), items.end(), [&](Item *a, Item *b) {
			const qint64 sa = a->isDir ? a->folderSize : a->size;
			const qint64 sb = b->isDir ? b->folderSize : b->size;
			return sa != sb ? sa > sb : byName(a, b);
		});
		break;
	case COL_KIND:
		std::stable_sort(items.begin(), items.end(), [&](Item *a, Item *b) {
			return text(a->kindName(), b->kindName(), a, b);
		});
		break;
	case COL_LABEL:
		std::stable_sort(items.begin(), items.end(), [&](Item *a, Item *b) {
			const int ra = a->labelIndex ? a->labelIndex : PL_LABEL_COUNT;
			const int rb = b->labelIndex ? b->labelIndex : PL_LABEL_COUNT;
			return ra != rb ? ra < rb : byName(a, b);
		});
		break;
	case COL_COMMENTS:
		std::stable_sort(items.begin(), items.end(), [&](Item *a, Item *b) {
			return text(itemComment(*a), itemComment(*b), a, b);
		});
		break;
	case COL_VERSION:
		std::stable_sort(items.begin(), items.end(), [&](Item *a, Item *b) {
			return text(itemVersion(*a), itemVersion(*b), a, b);
		});
		break;
	default:
		std::stable_sort(items.begin(), items.end(), byName);
		break;
	}
}

QString listDate(const QDateTime &t, bool relative, const QDateTime &now) {
	if (!t.isValid() || !relative) {
		return finderDate(t);
	}
	const qint64 days = now.date().daysTo(t.date());
	const char *word = days == 0 ? "Today" : days == -1 ? "Yesterday" : days == 1 ? "Tomorrow" : nullptr;
	if (!word) {
		return finderDate(t);
	}
	return QString(word) + ", " + QLocale(QLocale::English).toString(t, "h:mm AP");
}
