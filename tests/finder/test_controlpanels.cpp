/*
 * Control Panels and Control Panels (Disabled): a control panel dragged from
 * one to the other is turned off, as Mac OS 9's Extensions Manager turned a
 * panel off by moving its file, and its entry in the user's own data folder
 * is what does it (lib/controlpanels.h). Driven through the windows' own drag
 * and drop, with the pointers a mouse would make, so the path the Finder
 * takes is the one tested.
 */
#include <QApplication>
#include <QDir>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QUrl>

#include "fileops.h"
#include "items.h"
#include "vfs.h"

/* The windows' own items and drops, as the Finder's drag makes them: the
 * test drives the same entry points rather than a copy of them. */
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

/* Serves the events a drop queues (its `singleShot`), and lets the folder
 * windows settle after a reload. */
static void settle(int ms = 400) {
	QElapsedTimer timer;
	timer.start();
	while (timer.elapsed() < ms) {
		QCoreApplication::processEvents();
		QThread::msleep(5);
	}
}

static void writeEntry(const QString &dir, const QString &file, const QString &body) {
	QDir().mkpath(dir);
	QFile f(dir + "/" + file);
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		f.write(body.toUtf8());
	}
}

static QString read(const QString &path) {
	QFile f(path);
	return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

static Item *itemNamed(FolderWindow *w, const QString &name) {
	for (const auto &item : w->m_items) {
		if (item->name == name) {
			return item.get();
		}
	}
	return nullptr;
}

int main(int argc, char **argv) {
	QTemporaryDir root;
	if (!root.isValid()) {
		return 1;
	}
	/* A private XDG environment: one panel installed by the system, and
	 * nothing of the real desktop's read or written. */
	const QString dataHome = root.path() + "/data";
	const QString dataDirs = root.path() + "/system";
	const QString appsDir = dataDirs + "/applications";
	qputenv("HOME", root.path().toUtf8());
	qputenv("XDG_DATA_HOME", dataHome.toUtf8());
	qputenv("XDG_DATA_DIRS", (dataDirs + ":" + dataHome + "/flatpak/exports/share").toUtf8());
	qputenv("XDG_CONFIG_HOME", (root.path() + "/config").toUtf8());
	qputenv("XDG_CURRENT_DESKTOP", "ZacOS9");
	writeEntry(appsDir, "zacos9-mouse.desktop",
		"[Desktop Entry]\nType=Application\nName=Mouse\n"
		"Exec=zacos9-controlpanel mouse\nCategories=Settings;X-ZacOS9-ControlPanel;\n");
	writeEntry(appsDir, "zacos9-sound.desktop",
		"[Desktop Entry]\nType=Application\nName=Sound\n"
		"Exec=zacos9-controlpanel sound\nCategories=Settings;X-ZacOS9-ControlPanel;\n");
	const QString soundEntry = dataHome + "/applications/zacos9-sound.desktop";
	const QString ownEntry = dataHome + "/applications/zacos9-mouse.desktop";

	QApplication app(argc, argv);
	vfsRefresh();

	FolderWindow *panels = FolderWindow::open(vfsPathFor("system-folder/control-panels"));
	FolderWindow *off = FolderWindow::open(vfsPathFor("system-folder/control-panels-disabled"));
	panels->resize(520, 360);
	off->resize(520, 360);
	panels->relayout();
	off->relayout();
	settle();

	Item *mouse = itemNamed(panels, "Mouse");
	check(mouse != nullptr, "Control Panels lists the installed panel");
	check(itemNamed(off, "Mouse") == nullptr, "and Control Panels (Disabled) is empty");
	if (!mouse) {
		return 1;
	}

	/* The drag carries the panel's desktop entry's id, and nothing else's. */
	std::unique_ptr<QMimeData> mime(itemDragMime({ mouse }));
	const QJsonArray carried =
		QJsonDocument::fromJson(mime->data(PANEL_ITEMS_MIME)).array();
	check(carried == QJsonArray{ "zacos9-mouse.desktop" },
		"a panel dragged carries its desktop entry's id");

	/* Let go over the empty part of the (Disabled) folder. */
	QDropEvent drop(QPointF(460, 320), Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
	off->dropEvent(&drop);
	settle();
	check(drop.isAccepted(), "dropping a panel there is taken");
	check(read(ownEntry).contains("NoDisplay=true"),
		"the user's own copy of the entry turns it off");
	check(itemNamed(off, "Mouse") != nullptr, "the panel appears in Control Panels (Disabled)");
	check(itemNamed(panels, "Mouse") == nullptr, "and is gone from Control Panels");

	/* Dragged back into Control Panels, it is turned on again: the user's
	 * copy of the entry goes and the system's is the panel. */
	Item *offMouse = itemNamed(off, "Mouse");
	std::unique_ptr<QMimeData> back(offMouse ? itemDragMime({ offMouse }) : nullptr);
	check(offMouse && back, "the panel can be dragged back out of (Disabled)");
	if (back) {
		QDropEvent on(QPointF(460, 320), Qt::MoveAction, back.get(), Qt::LeftButton, Qt::NoModifier);
		panels->dropEvent(&on);
		settle();
		check(on.isAccepted(), "which Control Panels takes");
		check(!QFile::exists(ownEntry), "removing the user's copy of its entry");
		check(itemNamed(panels, "Mouse") != nullptr, "the panel is in Control Panels again");
		check(itemNamed(off, "Mouse") == nullptr, "and gone from Control Panels (Disabled)");
	}

	/* Two panels dragged at once are both turned off. */
	Item *backMouse = itemNamed(panels, "Mouse");
	Item *sound = itemNamed(panels, "Sound");
	std::unique_ptr<QMimeData> both(backMouse && sound ?
		itemDragMime({ backMouse, sound }) : nullptr);
	check(both != nullptr, "two panels can be dragged together");
	if (both) {
		QDropEvent pair(QPointF(460, 320), Qt::MoveAction, both.get(), Qt::LeftButton, Qt::NoModifier);
		off->dropEvent(&pair);
		settle();
		check(pair.isAccepted(), "and both go into Control Panels (Disabled)");
		check(itemNamed(off, "Mouse") != nullptr && itemNamed(off, "Sound") != nullptr,
			"which lists the two of them");
		check(itemNamed(panels, "Mouse") == nullptr && itemNamed(panels, "Sound") == nullptr,
			"and Control Panels lists neither");
		check(read(ownEntry).contains("NoDisplay=true") &&
			read(soundEntry).contains("NoDisplay=true"), "both entries turned off");
	}

	/* A file dropped in is refused, and nothing is written for it. */
	writeEntry(root.path(), "stray.txt", "not a panel");
	QMimeData fileMime;
	fileMime.setUrls({ QUrl::fromLocalFile(root.path() + "/stray.txt") });
	QDropEvent stray(QPointF(460, 320), Qt::CopyAction, &fileMime, Qt::LeftButton, Qt::NoModifier);
	off->dropEvent(&stray);
	settle();
	check(!stray.isAccepted() && !QFile::exists(dataHome + "/applications/stray.txt"),
		"a file dropped in is refused, and writes nothing");

	return fails ? 1 : 0;
}
