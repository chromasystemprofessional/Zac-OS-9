/*
 * Sniffer's Mac OS selection gestures: selection rectangles in icon and
 * list views, Shift toggling, Select All, and moving several selected
 * items into another folder with one drop.
 */
#include <QApplication>
#include <QDir>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileSystemWatcher>
#include <QImage>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QWidget>
#include <algorithm>
#include <map>

#include "fileops.h"
#include "finder.h"
#include "items.h"
#include "labeleditor.h"
#include "widgets.h"

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

static void touch(const QString &path) {
	QFile f(path);
	f.open(QIODevice::WriteOnly);
	f.write(path.toUtf8());
}

static void mouse(QWidget *w, QEvent::Type type, QPoint p, Qt::MouseButtons buttons,
		Qt::KeyboardModifiers mods = Qt::NoModifier) {
	const Qt::MouseButton button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
	QMouseEvent e(type, p, w->mapToGlobal(p), button, buttons, mods);
	QApplication::sendEvent(w, &e);
}

static void dragSelect(FolderWindow *w, QPoint from, QPoint to,
		Qt::KeyboardModifiers mods = Qt::NoModifier) {
	mouse(w, QEvent::MouseButtonPress, from, Qt::LeftButton, mods);
	mouse(w, QEvent::MouseMove, (from + to) / 2, Qt::LeftButton, mods);
	mouse(w, QEvent::MouseMove, to, Qt::LeftButton, mods);
}

static QSet<QString> selectedNames(FolderWindow *w) {
	QSet<QString> names;
	for (Item *item : w->selectedItems()) {
		names.insert(item->name);
	}
	return names;
}

static std::vector<Item *> iconOrder(FolderWindow *w) {
	std::vector<Item *> items;
	for (auto &item : w->m_items) {
		items.push_back(item.get());
	}
	std::sort(items.begin(), items.end(), [](Item *a, Item *b) {
		return a->pos.y() == b->pos.y() ? a->pos.x() < b->pos.x() : a->pos.y() < b->pos.y();
	});
	return items;
}

static bool hasMarqueeDot(FolderWindow *w, QPoint corner) {
	const QImage img = w->grab().toImage();
	for (int dy = -1; dy <= 1; dy++) {
		for (int dx = -1; dx <= 1; dx++) {
			const QPoint p = corner + QPoint(dx, dy);
			if (img.rect().contains(p) && img.pixel(p) == 0xFF555555u) {
				return true;
			}
		}
	}
	return false;
}

int main(int argc, char **argv) {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
	qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
	qputenv("XDG_RUNTIME_DIR", home.path().toUtf8());
	QApplication app(argc, argv);

	const QString folder = home.path() + "/Source";
	const QString target = home.path() + "/Target";
	QDir().mkpath(folder);
	QDir().mkpath(target);
	for (const char *name : { "Alpha", "Bravo", "Charlie", "Delta" }) {
		touch(folder + "/" + name);
	}

	FolderWindow *w = FolderWindow::open(folder);
	w->resize(520, 360);
	w->setViewMode(FolderWindow::ViewMode::Icons);
	w->relayout();
	settle();
	std::vector<Item *> icons = iconOrder(w);
	check(icons.size() == 4 && icons[0]->pos.y() == icons[3]->pos.y(),
		"icon view lays the four test items out in one row");

	const QPoint blank = w->toWindow(QPoint(1, 1));
	const QPoint secondIcon = w->toWindow(icons[1]->pos + QPoint(16, 16));
	dragSelect(w, blank, secondIcon);
	check(w->m_marquee && hasMarqueeDot(w, blank),
		"dragging from blank icon-view space draws a dotted selection rectangle");
	check(selectedNames(w) == QSet<QString>{ icons[0]->name, icons[1]->name },
		"the selection rectangle selects exactly the icons it covers");
	mouse(w, QEvent::MouseButtonRelease, secondIcon, Qt::NoButton);
	check(!w->m_marquee && selectedNames(w).size() == 2,
		"releasing removes the rectangle and keeps the selection");

	const QPoint betweenRows = w->toWindow(QPoint(icons[1]->pos.x() + 16, 1));
	const QPoint thirdIcon = w->toWindow(icons[2]->pos + QPoint(16, 16));
	dragSelect(w, betweenRows, thirdIcon, Qt::ShiftModifier);
	mouse(w, QEvent::MouseButtonRelease, thirdIcon, Qt::NoButton, Qt::ShiftModifier);
	check(selectedNames(w) == QSet<QString>{ icons[0]->name, icons[2]->name },
		"Shift-dragging toggles covered icons from the existing selection");

	const QPoint fourthIcon = w->toWindow(icons[3]->pos + QPoint(16, 16));
	mouse(w, QEvent::MouseButtonPress, fourthIcon, Qt::LeftButton, Qt::ShiftModifier);
	mouse(w, QEvent::MouseButtonRelease, fourthIcon, Qt::NoButton, Qt::ShiftModifier);
	check(selectedNames(w) == QSet<QString>{ icons[0]->name, icons[2]->name, icons[3]->name },
		"Shift-clicking adds an icon");
	mouse(w, QEvent::MouseButtonPress, fourthIcon, Qt::LeftButton, Qt::ShiftModifier);
	mouse(w, QEvent::MouseButtonRelease, fourthIcon, Qt::NoButton, Qt::ShiftModifier);
	check(selectedNames(w) == QSet<QString>{ icons[0]->name, icons[2]->name },
		"Shift-clicking a selected icon removes it");

	QKeyEvent selectAll(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier, "a");
	QApplication::sendEvent(w, &selectAll);
	check(w->selectedItems().size() == 4, "Command-A selects every icon");
	mouse(w, QEvent::MouseButtonPress, blank, Qt::LeftButton);
	mouse(w, QEvent::MouseButtonRelease, blank, Qt::NoButton);
	check(w->selectedItems().empty(), "clicking blank space deselects everything");
	Finder::instance().command("select-all");
	check(w->selectedItems().size() == 4, "Edit > Select All selects every icon");

	w->setViewMode(FolderWindow::ViewMode::List);
	w->resize(640, 360);
	w->relayout();
	settle();
	const int listTop = w->contentTop();
	const QPoint dateFirst(260, listTop + 8);
	const QPoint dateThird(260, listTop + 2 * 19 + 8);
	dragSelect(w, dateFirst, dateThird);
	mouse(w, QEvent::MouseButtonRelease, dateThird, Qt::NoButton);
	check(selectedNames(w).size() == 0,
		"a list rectangle confined to the date column selects no names");
	const QPoint nameFirst(56, listTop + 8);
	dragSelect(w, dateFirst, nameFirst + QPoint(0, 2 * 19));
	mouse(w, QEvent::MouseButtonRelease, nameFirst + QPoint(0, 2 * 19), Qt::NoButton);
	check(selectedNames(w) == QSet<QString>{ w->m_rows[0].item->name, w->m_rows[1].item->name,
		w->m_rows[2].item->name },
		"a list rectangle from a blank column selects the rows whose names it crosses");
	mouse(w, QEvent::MouseButtonPress, nameFirst, Qt::LeftButton);
	mouse(w, QEvent::MouseButtonRelease, nameFirst, Qt::NoButton);
	check(selectedNames(w).size() == 3, "pressing a selected name keeps all of them for a drag");

	for (int i = 0; i < 40; i++) {
		touch(folder + QString("/More %1").arg(i, 2, 10, QLatin1Char('0')));
	}
	w->reload();
	w->resize(360, 180);
	w->relayout();
	settle();
	const QPoint start(300, listTop + 4);
	mouse(w, QEvent::MouseButtonPress, start, Qt::LeftButton);
	mouse(w, QEvent::MouseMove, QPoint(56, w->height() + 20), Qt::LeftButton);
	settle(400);
	const int scrolled = w->m_scrollY;
	mouse(w, QEvent::MouseButtonRelease, QPoint(56, w->height() + 20), Qt::NoButton);
	check(scrolled > 0 && w->selectedItems().size() > 6,
		"dragging a rectangle below a list window scrolls and extends the selection");

	QStringList paths;
	for (const char *name : { "Alpha", "Bravo", "Charlie" }) {
		paths << folder + "/" + name;
	}
	auto *mime = new QMimeData;
	QList<QUrl> urls;
	for (const QString &path : paths) {
		urls << QUrl::fromLocalFile(path);
	}
	mime->setUrls(urls);
	QDropEvent drop(QPointF(1, 1), Qt::MoveAction | Qt::CopyAction, mime, Qt::LeftButton,
		Qt::NoModifier);
	dropItems(&drop, nullptr, target);
	bool moved = true;
	for (const char *name : { "Alpha", "Bravo", "Charlie" }) {
		moved &= QFile::exists(target + "/" + name) && !QFile::exists(folder + "/" + name);
	}
	check(moved && drop.dropAction() == Qt::MoveAction,
		"one drop moves every selected item into another folder");
	delete mime;

	const QString classic = folder + "/Classic Sound";
	const QString fork = folder + "/._Classic Sound";
	touch(classic);
	touch(fork);
	check(!transferItems({ classic }, folder, true).isEmpty() &&
		QFile::exists(folder + "/Classic Sound copy") &&
		QFile::exists(folder + "/._Classic Sound copy"),
		"Option-copy preserves a classic file's hidden resource fork under its new name");
	check(!transferItems({ classic }, target, false).isEmpty() &&
		QFile::exists(target + "/Classic Sound") && QFile::exists(target + "/._Classic Sound") &&
		!QFile::exists(classic) && !QFile::exists(fork),
		"moving a classic sound file also moves its resource-fork companion");

	w->close();
	settle();
	return fails ? 1 : 0;
}
