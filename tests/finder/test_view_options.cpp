/*
 * View Options and Preferences (Missing Manual pp. 24-25, 413): saved per
 * window and inherited by new folders; relative dates; the optional list
 * columns and sorting by them; folder sizes added up in the background;
 * small and tiny icons; Always snap to grid and Keep arranged; editable
 * labels shown in the Label column.
 */
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMouseEvent>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>
#include <sys/xattr.h>

#include "settings.h"
#include "viewoptions.h"

#define private public
#define protected public
#include "folderwindow.h"
#undef protected
#undef private

static int fails;

static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

static void settle(int ms = 50) {
	QElapsedTimer timer;
	timer.start();
	while (timer.elapsed() < ms) {
		QApplication::processEvents(QEventLoop::AllEvents, 10);
	}
}

static void mouse(QWidget *w, QEvent::Type type, QPoint p) {
	QMouseEvent e(type, p, w->mapToGlobal(p), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
		type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(w, &e);
}

static void drag(QWidget *w, QPoint from, QPoint to) {
	mouse(w, QEvent::MouseButtonPress, from);
	mouse(w, QEvent::MouseMove, (from + to) / 2);
	mouse(w, QEvent::MouseMove, to);
	mouse(w, QEvent::MouseButtonRelease, to);
}

static void write(const QString &path, int bytes) {
	QFile f(path);
	f.open(QIODevice::WriteOnly);
	f.write(QByteArray(bytes, 'x'));
}

static Item *named(FolderWindow *w, const QString &name) {
	for (auto &item : w->m_items) {
		if (item->name == name) {
			return item.get();
		}
	}
	return nullptr;
}

static QStringList rowNames(FolderWindow *w) {
	QStringList names;
	for (const auto &row : w->m_rows) {
		names << row.item->name;
	}
	return names;
}

int main(int argc, char **argv) {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
	qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
	qputenv("XDG_RUNTIME_DIR", home.path().toUtf8());
	QApplication app(argc, argv);

	/* Relative dates. */
	const QDateTime now(QDate(2026, 10, 13), QTime(15, 0));
	check(listDate(QDateTime(QDate(2026, 10, 13), QTime(9, 5)), true, now) == "Today, 9:05 AM" &&
		listDate(QDateTime(QDate(2026, 10, 12), QTime(21, 30)), true, now) == "Yesterday, 9:30 PM" &&
		listDate(QDateTime(QDate(2026, 10, 14), QTime(8, 0)), true, now) == "Tomorrow, 8:00 AM",
		"relative dates: Today, Yesterday, Tomorrow");
	check(listDate(QDateTime(QDate(2026, 10, 13), QTime(9, 5)), false, now).startsWith("Tue, Oct 13, 2026") &&
		listDate(QDateTime(QDate(2026, 10, 1), QTime(9, 5)), true, now).startsWith("Thu, Oct 1, 2026"),
		"otherwise, and for older dates, the full date");

	/* Saved and read back, what's missing from the fallback. */
	{
		QSettings st(home.path() + "/views.ini", QSettings::IniFormat);
		ViewOptions o;
		o.icons = { IconSize::Small, IconArrangement::KeepArranged, Arrange::Size };
		o.buttons.size = IconSize::Small;
		o.list = { IconSize::Tiny, false, true, columnBit(COL_NAME) | columnBit(COL_LABEL) };
		st.beginGroup("a");
		o.save(st);
		st.endGroup();
		st.beginGroup("a");
		check(ViewOptions::load(st, ViewOptions{}) == o, "view options are saved and read back");
		st.endGroup();
		st.beginGroup("empty");
		check(ViewOptions::load(st, o) == o && ViewOptions::load(st, ViewOptions{}) == ViewOptions{},
			"a window without its own options takes the ones it inherits");
		st.endGroup();
		ViewOptions none;
		none.list.columns = columnBit(COL_NAME);
		st.beginGroup("none");
		none.save(st);
		check(ViewOptions::load(st, ViewOptions{}).list.columns == columnBit(COL_NAME),
			"a list with no optional columns stays that way");
	}

	/* Standard views; new folders take their parent window's view. */
	ViewOptions standard;
	standard.icons.arrangement = IconArrangement::SnapToGrid;
	setStandardViews(standard);
	check(standardViews() == standard, "Preferences' standard views are kept");
	const QString parent = home.path() + "/Parent";
	QDir().mkpath(parent + "/Child");
	QDir().mkpath(home.path() + "/Loner");
	check(FolderState::load(home.path() + "/Loner").options == standard,
		"a new folder with no window around it starts from the standard views");
	{
		FolderState p = FolderState::load(parent);
		p.viewMode = 1;
		p.options.list.columns |= columnBit(COL_COMMENTS);
		p.save(parent);
		const FolderState child = FolderState::load(parent + "/Child");
		check(child.viewMode == 1 && (child.options.list.columns & columnBit(COL_COMMENTS)),
			"a new folder inherits its parent window's view and options");
	}

	/* A list with every column; labels as the user named them. */
	const QString folder = home.path() + "/Work";
	QDir().mkpath(folder + "/dossier/Inner");
	write(folder + "/dossier/one", 1000);
	write(folder + "/dossier/Inner/two", 2000);
	write(folder + "/apple", 10);
	write(folder + "/banana", 20);
	write(folder + "/cherry", 30);
	writeLabel(folder + "/banana", 2);
	writeLabel(folder + "/cherry", 1);
	pl_setting_set("label-names", "Urgent|Soon");
	const bool comments = setxattr(QFile::encodeName(folder + "/cherry").constData(),
		"user.xdg.comment", "Zebra notes", 11, 0) == 0;

	FolderWindow *w = FolderWindow::open(folder);
	w->resize(700, 400);
	w->setViewMode(FolderWindow::ViewMode::List);
	ViewOptions all = w->viewOptions();
	all.list.columns = ~0u & ((1u << COL_COUNT) - 1);
	all.list.folderSizes = true;
	w->setViewOptions(all);
	check(w->columns() == std::vector<int>({ COL_NAME, COL_MODIFIED, COL_CREATED, COL_SIZE, COL_KIND,
			COL_LABEL, COL_COMMENTS, COL_VERSION }),
		"the columns appear in the Mac's order");
	Item *banana = named(w, "banana");
	check(banana && columnText(*banana, COL_LABEL, all.list) == "Soon",
		"the Label column shows the label's name as the user set it");
	check(named(w, "apple") && columnText(*named(w, "apple"), COL_LABEL, all.list) == "-" &&
		columnText(*named(w, "apple"), COL_VERSION, all.list) == "n/a",
		"no label, no version: shown as such");
	w->sortBy(COL_LABEL);
	check(rowNames(w).mid(0, 2) == QStringList({ "cherry", "banana" }),
		"sorting by label puts labelled items first, in label order");
	if (comments) {
		w->sortBy(COL_COMMENTS);
		check(rowNames(w).first() == "cherry" && itemComment(*named(w, "cherry")) == "Zebra notes",
			"the Comments column shows Get Info's comments, and sorts by them");
	}

	/* Calculate folder sizes, off the main thread. */
	QElapsedTimer waited;
	waited.start();
	while (named(w, "dossier")->folderSize < 0 && waited.elapsed() < 5000) {
		settle(20);
	}
	check(named(w, "dossier")->folderSize == 3000 &&
		columnText(*named(w, "dossier"), COL_SIZE, all.list) == "3K",
		"a folder's size is its contents', subfolders too");
	w->sortBy(COL_SIZE);
	check(rowNames(w).first() == "dossier", "and it sorts by that size");
	ViewOptions fewer = w->viewOptions();
	fewer.list.columns = columnBit(COL_NAME) | columnBit(COL_KIND);
	w->setViewOptions(fewer);
	check(w->sortColumn() == COL_NAME, "hiding the sorted column sorts by name");
	w->sortBy(COL_SIZE);
	check(w->sortColumn() == COL_NAME, "and a hidden column can't be sorted by");

	/* Icon sizes in the list. */
	fewer.list.size = IconSize::Tiny;
	w->setViewOptions(fewer);
	check(w->metrics().rowH == 17 && w->metrics().icon == 12, "tiny icons: 12 px, short rows");
	check(!w->grab().isNull(), "the list draws with tiny icons");
	fewer.list.size = IconSize::Standard;
	w->setViewOptions(fewer);
	check(w->metrics().rowH == 36 && w->m_contentHeight == 36 * static_cast<int>(w->m_rows.size()),
		"standard icons: tall rows");

	/* Icon view: small icons, on a short, wide grid. */
	w->setViewMode(FolderWindow::ViewMode::Icons);
	ViewOptions icons = w->viewOptions();
	icons.icons.arrangement = IconArrangement::None;
	icons.icons.size = IconSize::Small;
	w->setViewOptions(icons);
	Item *a = named(w, "apple"), *b = named(w, "banana");
	check(w->iconGrid().cellH == 24 && a->pos.y() == b->pos.y() &&
		std::abs(a->pos.x() - b->pos.x()) == 128,
		"small icons sit in short, wide cells");
	check(w->itemHit(*a, a->pos + QPoint(30, 6)), "a small icon's name is beside it");

	/* Always snap to grid: a moved icon lands on the nearest free slot. */
	icons.icons.size = IconSize::Standard;
	icons.icons.arrangement = IconArrangement::SnapToGrid;
	w->setViewOptions(icons);
	a = named(w, "apple");
	b = named(w, "banana");
	a->pos = b->pos + QPoint(5, 3); /* dropped nearly on banana */
	w->snapToGrid({ a });
	bool onSlot = false;
	for (int s = 0; s < 40; s++) {
		onSlot |= w->iconSlot(s) == a->pos;
	}
	check(onSlot && a->pos != b->pos, "snapped to a free grid slot, not on top of another icon");

	/* Keep arranged: by size, the biggest first. */
	icons.icons.arrangement = IconArrangement::KeepArranged;
	icons.icons.keepBy = Arrange::Size;
	w->setViewOptions(icons);
	check(named(w, "cherry")->pos == w->iconSlot(0) && named(w, "banana")->pos == w->iconSlot(1),
		"kept arranged by size, the largest first");
	w->arrange(Arrange::Name);
	check(w->viewOptions().icons.keepBy == Arrange::Name && named(w, "apple")->pos == w->iconSlot(0),
		"Arrange changes the order kept");

	/* Tight grid spacing (Preferences > General). */
	pl_setting_set("grid-spacing", "tight");
	FolderWindow::preferencesChanged();
	check(w->iconGrid().cellW == 64, "a tight grid packs icons closer");
	pl_setting_set("grid-spacing", nullptr);

	/* Small buttons. */
	w->setViewMode(FolderWindow::ViewMode::Buttons);
	icons.buttons.size = IconSize::Small;
	w->setViewOptions(icons);
	check(w->buttonSide() == 26 && w->buttonRect(named(w, "apple")).width() == 26,
		"small buttons hold small icons");

	/* Narrow columns shorten their dates, as the Mac's did. */
	const QDateTime then(QDate(2026, 10, 1), QTime(15, 42));
	check(fitListDate(then, true, 1000, now) == "Thursday, October 1, 2026, 3:42 PM" &&
		fitListDate(then, true, 150, now) == "Thu, Oct 1, 2026, 3:42 PM" &&
		fitListDate(then, true, 90, now) == "10/1/26, 3:42 PM" &&
		fitListDate(then, true, 10, now) == "10/1/26",
		"dates shorten as the column narrows");
	check(fitListDate(QDateTime(QDate(2026, 10, 13), QTime(9, 5)), true, 20, now) == "Today",
		"and a relative date to its word");

	/* Column headers: drag a divider, drag a header, click to sort, the
	 * direction button; Reset Column Positions. */
	w->setViewMode(FolderWindow::ViewMode::List);
	ViewOptions cols = w->viewOptions();
	cols.list.size = IconSize::Small;
	cols.list.columns = columnBit(COL_NAME) | columnBit(COL_MODIFIED) | columnBit(COL_SIZE) |
		columnBit(COL_KIND);
	w->setViewOptions(cols);
	w->sortBy(COL_NAME);
	w->resize(800, 400);
	w->relayout();
	const int band = 21 + 10; /* the column headers' row */
	const int nameEdge = w->columnWidth(COL_NAME);
	drag(w, QPoint(nameEdge, band), QPoint(nameEdge + 50, band));
	check(w->columnWidth(COL_NAME) == 270 && w->columnsCustomized(),
		"dragging a divider widens the column to its left");
	const int kindX = w->columnX(COL_KIND) + 20;
	drag(w, QPoint(kindX, band), QPoint(w->columnWidth(COL_NAME) + 5, band));
	check(w->columns() == std::vector<int>({ COL_NAME, COL_KIND, COL_MODIFIED, COL_SIZE }),
		"dragging a header moves the column; Name stays first");
	mouse(w, QEvent::MouseButtonPress, QPoint(w->columnX(COL_SIZE) + 20, band));
	mouse(w, QEvent::MouseButtonRelease, QPoint(w->columnX(COL_SIZE) + 20, band));
	check(w->sortColumn() == COL_SIZE, "a click on a header still sorts by it");
	const QStringList bySize = rowNames(w);
	mouse(w, QEvent::MouseButtonPress, w->sortDirectionButton().center());
	mouse(w, QEvent::MouseButtonRelease, w->sortDirectionButton().center());
	QStringList reversed = bySize;
	std::reverse(reversed.begin(), reversed.end());
	check(w->sortReversed() && rowNames(w) == reversed, "the direction button reverses the list");
	w->m_state.save(folder);
	const FolderState kept = FolderState::load(folder);
	check(kept.sortReversed && kept.columnWidths.value(COL_NAME) == 270 &&
		kept.columnOrder.value(0) == COL_KIND, "order, widths and direction are kept for the folder");
	w->resetColumns();
	check(!w->columnsCustomized() && w->columnWidth(COL_NAME) == 220 &&
		w->columns() == std::vector<int>({ COL_NAME, COL_MODIFIED, COL_SIZE, COL_KIND }),
		"Reset Column Positions puts them back");
	check(!w->grab().isNull(), "the list draws its headers and button");

	w->close();
	settle();
	QTextStream(stdout) << fails << " failure(s)\n";
	return fails ? 1 : 0;
}
