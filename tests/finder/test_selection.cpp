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
#include <unistd.h>

#include "fileops.h"
#include "finder.h"
#include "items.h"
#include "labeleditor.h"
#include "widgets.h"
#include "transferdialog.h"
#include "alert.h"

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

	const QString transferSource = home.path() + "/Transfer Source";
	const QString transferTarget = home.path() + "/Transfer Target";
	QDir().mkdir(transferSource);
	QDir().mkdir(transferTarget);
	touch(transferSource + "/Completed");
	QFile large(transferSource + "/Large");
	check(large.open(QIODevice::WriteOnly) && large.resize(256 * 1024 * 1024),
		"create disposable large file for stoppable copy");
	large.close();
	bool visible = false, stopped = false, responsive = false;
	QTimer stop;
	stop.setInterval(1);
	QObject::connect(&stop, &QTimer::timeout, [&] {
		for (QWidget *widget : QApplication::topLevelWidgets()) {
			if (auto *progress = dynamic_cast<TransferDialog *>(widget); progress && progress->isVisible()) {
				visible = true;
				responsive = true;
				if (QFileInfo(transferTarget + "/Large").size() > 1024 * 1024) {
					progress->reject();
					stopped = true;
					stop.stop();
				}
			}
		}
	});
	stop.start();
	bool finished = true;
	const auto cancelled = transferItems({transferSource + "/Completed", transferSource + "/Large"},
		transferTarget, true, &finished);
	stop.stop();
	check(visible && responsive && stopped, "copy dialog remains responsive and Stop interrupts file I/O");
	check(!finished, "stopped batch does not report complete success to drag source");
	check(cancelled.contains(transferTarget) && QFile::exists(transferTarget + "/Completed") &&
		!QFile::exists(transferTarget + "/Large"), "Stop retains completed copy and removes incomplete file");
	check(QFile::exists(transferSource + "/Completed") &&
		QFileInfo(transferSource + "/Large").size() == 256 * 1024 * 1024,
		"stopped copy preserves all originals");
	touch(transferSource + "/Move me");
	bool moveVisible = false;
	QTimer moveObserver;
	moveObserver.setInterval(0);
	QObject::connect(&moveObserver, &QTimer::timeout, [&] {
		for (QWidget *widget : QApplication::topLevelWidgets()) {
			if (auto *progress = dynamic_cast<TransferDialog *>(widget); progress && progress->isVisible()) {
				moveVisible = true;
			}
		}
	});
	moveObserver.start();
	check(!transferItems({transferSource + "/Move me"}, transferTarget, false).isEmpty() &&
		QFile::exists(transferTarget + "/Move me") && !QFile::exists(transferSource + "/Move me"),
		"move progress preserves same-disk rename behavior");
	moveObserver.stop();
	check(moveVisible, "same-disk moves also show a status dialog");
	std::atomic_bool buttonStopped{false};
	TransferDialog buttons(buttonStopped);
	buttons.setProgress("Copying", "Report", transferTarget, 256, 1024, false);
	buttons.show();
	settle();
	check(buttons.currentFile() == "Report" && buttons.completed() == 256 && buttons.total() == 1024,
		"progress dialog receives exact filename and byte-work fraction");
	const QImage quarter = buttons.grab().toImage();
	buttons.setProgress("Copying", "Report", transferTarget, 768, 1024, false);
	const QImage threeQuarters = buttons.grab().toImage();
	check(quarter.pixel(20, 75) == 0xFF000000u && quarter.pixel(5, 5) == 0xFFDDDDDDu &&
		quarter.pixel(220, 80) != threeQuarters.pixel(220, 80),
		"Platinum grey dialog paints a bordered progress bar reflecting 25% versus 75%");
	QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
	QApplication::sendEvent(&buttons, &escape);
	check(buttonStopped.load() && buttons.isVisible(), "Escape requests Stop without closing before rollback");
	buttons.done(QDialog::Accepted);
	std::atomic_bool clickStopped{false};
	TransferDialog clickDialog(clickStopped);
	clickDialog.show();
	settle();
	mouse(&clickDialog, QEvent::MouseButtonPress, QPoint(375, 145), Qt::LeftButton);
	mouse(&clickDialog, QEvent::MouseButtonRelease, QPoint(375, 145), Qt::NoButton);
	check(clickStopped.load() && clickDialog.isVisible(), "mouse Stop requests cancellation without abandoning worker");
	clickDialog.done(QDialog::Accepted);
	touch(transferSource + "/Keep on Stop");
	QMimeData stoppedMime;
	stoppedMime.setUrls({QUrl::fromLocalFile(transferSource + "/Keep on Stop")});
	QDropEvent stoppedDrop(QPointF(1, 1), Qt::MoveAction | Qt::CopyAction, &stoppedMime,
		Qt::LeftButton, Qt::AltModifier);
	QTimer::singleShot(0, [] {
		for (QWidget *widget : QApplication::topLevelWidgets()) {
			if (auto *progress = dynamic_cast<TransferDialog *>(widget); progress && progress->isVisible()) {
				progress->reject();
			}
		}
	});
	dropItems(&stoppedDrop, nullptr, transferTarget);
	check(!stoppedDrop.isAccepted() && QFile::exists(transferSource + "/Keep on Stop"),
		"stopped drop never asks external drag source to delete uncopied originals");
	const QString tree = transferSource + "/Folder";
	QDir().mkpath(tree + "/Nested");
	touch(tree + "/Nested/Data");
	check(symlink("Nested", QFile::encodeName(tree + "/Relative alias").constData()) == 0 &&
		symlink("Missing", QFile::encodeName(tree + "/Missing alias").constData()) == 0,
		"create relative and dangling aliases inside test folder");
	check(!transferItems({tree}, transferTarget, true).isEmpty() &&
		QFile::exists(transferTarget + "/Folder/Nested/Data") &&
		QFileInfo(transferTarget + "/Folder/Relative alias").isSymLink() &&
		QFileInfo(transferTarget + "/Folder/Missing alias").isSymLink(),
		"recursive copy preserves real files and both folder and dangling aliases");
	QByteArray link(64, '\0');
	const auto linkLength = readlink(QFile::encodeName(transferTarget + "/Folder/Relative alias").constData(),
		link.data(), link.size());
	check(linkLength == 6 && link.left(linkLength) == "Nested",
		"recursive copy keeps relative alias target relative to copied folder");
	const QString broken = transferSource + "/Cannot copy";
	QDir().mkpath(broken + "/A completed folder");
	touch(broken + "/A completed folder/Data");
	touch(broken + "/Z unreadable");
	QFile::setPermissions(broken + "/A completed folder",
		QFileDevice::ReadOwner | QFileDevice::ExeOwner);
	QFile::setPermissions(broken + "/Z unreadable", {});
	int errorAlerts = 0;
	QTimer dismissErrors;
	dismissErrors.setInterval(1);
	QObject::connect(&dismissErrors, &QTimer::timeout, [&] {
		for (QWidget *widget : QApplication::topLevelWidgets()) {
			if (auto *alert = dynamic_cast<Alert *>(widget); alert && alert->isVisible()) {
				errorAlerts++;
				alert->accept();
			}
		}
	});
	dismissErrors.start();
	bool errorFinished = true;
	const auto failed = transferItems({broken}, transferTarget, true, &errorFinished);
	dismissErrors.stop();
	check(!errorFinished && failed.isEmpty() && errorAlerts > 0 &&
		!QFile::exists(transferTarget + "/Cannot copy") &&
		QFile::exists(broken + "/A completed folder/Data"),
		"read failure reports error and rolls back partial tree including read-only copied folders");
	QFile::setPermissions(broken + "/A completed folder",
		QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
	QFile::setPermissions(broken + "/Z unreadable", QFileDevice::ReadOwner | QFileDevice::WriteOwner);

	w->close();
	settle();
	return fails ? 1 : 0;
}
