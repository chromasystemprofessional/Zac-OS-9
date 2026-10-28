/*
 * Extensions and Extensions (Disabled): an extension dragged from one to the
 * other is turned off (or on again) through zacos9-modules-helper, as Mac OS
 * 9's Extensions Manager turned an extension off by moving its file. Driven
 * through the windows' own drag and drop, with a pkexec that just runs what
 * it is given and scratch modprobe.d/modules-load.d files, so nothing of the
 * real computer's is read or written.
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
#include <QTimer>
#include <QUrl>

#include "alert.h"
#include "fileops.h"
#include "items.h"
#include "vfs.h"

/* The helper from the sources, passed by meson: rootHelperPath() would find
 * it too, but not if these tests are ever run installed. */
#ifndef ZACOS9_MODULES_HELPER_TEST
#error "meson must pass the helper's path"
#endif

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

static bool waitFor(const std::function<bool()> &done, int ms = 8000) {
	QElapsedTimer timer;
	timer.start();
	while (timer.elapsed() < ms) {
		if (done()) {
			return true;
		}
		QCoreApplication::processEvents();
		QThread::msleep(5);
	}
	return done();
}

static QString read(const QString &path) {
	QFile f(path);
	return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

static bool writeFile(const QString &path, const QString &body, bool executable = false) {
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	f.write(body.toUtf8());
	f.close();
	if (executable) {
		f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
	}
	return true;
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
	/* A private desktop, and the helper's two files somewhere harmless. */
	const QString dataHome = root.path() + "/data";
	const QString dataDirs = root.path() + "/system";
	qputenv("HOME", root.path().toUtf8());
	qputenv("XDG_DATA_HOME", dataHome.toUtf8());
	qputenv("XDG_DATA_DIRS", (dataDirs + ":" + dataHome + "/flatpak/exports/share").toUtf8());
	qputenv("XDG_CONFIG_HOME", (root.path() + "/config").toUtf8());
	qputenv("XDG_CURRENT_DESKTOP", "ZacOS9");
	const QString blacklistFile = root.path() + "/modprobe.d/zacos9-extensions.conf";
	const QString loadFile = root.path() + "/modules-load.d/zacos9-extensions.conf";
	qputenv("ZACOS9_MODPROBE_CONF", blacklistFile.toUtf8());
	qputenv("ZACOS9_MODULES_LOAD_CONF", loadFile.toUtf8());
	qputenv("ZACOS9_MODULES_HELPER", QByteArray(ZACOS9_MODULES_HELPER_TEST));

	/* pkexec as an administrator at the computer gets it: no prompt, just
	 * the helper. */
	check(writeFile(root.path() + "/bin/pkexec", "#!/bin/sh\nexec \"$@\"\n", true),
		"a pkexec that runs its arguments is in place");
	qputenv("PATH", (root.path() + "/bin:" + qgetenv("PATH")).toUtf8());

	QApplication app(argc, argv);
	vfsRefresh();

	FolderWindow *extensions = FolderWindow::open(vfsPathFor("system-folder/extensions"));
	FolderWindow *off = FolderWindow::open(vfsPathFor("system-folder/extensions-disabled"));
	extensions->resize(1100, 800);
	off->resize(1100, 800);
	extensions->relayout();
	off->relayout();
	settle();

	Item *bluetooth = itemNamed(extensions, "Bluetooth");
	check(bluetooth != nullptr, "Extensions lists Bluetooth");
	check(itemNamed(off, "Bluetooth") == nullptr,
		"and Extensions (Disabled) does not, while it is on");
	if (!bluetooth) {
		return 1;
	}

	/* The drag carries the catalog key, and nothing else. */
	std::unique_ptr<QMimeData> mime(itemDragMime({ bluetooth }));
	const QJsonArray carried =
		QJsonDocument::fromJson(mime->data(MODULE_ITEMS_MIME)).array();
	check(carried == QJsonArray{ "bluetooth" }, "an extension dragged carries its catalog key");

	/* Let go over the empty part of the (Disabled) folder. */
	QDropEvent drop(QPointF(1000, 700), Qt::MoveAction, mime.get(), Qt::LeftButton, Qt::NoModifier);
	off->dropEvent(&drop);
	check(drop.isAccepted(), "dropping an extension there is taken");
	check(waitFor([&] { return itemNamed(off, "Bluetooth") != nullptr; }),
		"the helper runs and the extension appears in Extensions (Disabled)");
	check(itemNamed(extensions, "Bluetooth") == nullptr, "and is gone from Extensions");
	const QString blacklist = read(blacklistFile);
	check(blacklist.contains("blacklist btusb") && blacklist.contains("blacklist bluetooth"),
		"the blacklist names its modules for the next startup");
	check(!read(loadFile).contains("btusb"),
		"and they are no longer asked for at startup");

	/* Dragged back into Extensions, it is turned on again. */
	Item *offBluetooth = itemNamed(off, "Bluetooth");
	std::unique_ptr<QMimeData> back(offBluetooth ? itemDragMime({ offBluetooth }) : nullptr);
	check(offBluetooth && back, "the extension can be dragged back out of (Disabled)");
	if (back) {
		QDropEvent on(QPointF(1000, 700), Qt::MoveAction, back.get(), Qt::LeftButton,
			Qt::NoModifier);
		extensions->dropEvent(&on);
		check(on.isAccepted(), "which Extensions takes");
		check(waitFor([&] { return itemNamed(extensions, "Bluetooth") != nullptr; }),
			"and the extension is in Extensions again");
		check(itemNamed(off, "Bluetooth") == nullptr, "gone from Extensions (Disabled)");
		check(!read(blacklistFile).contains("blacklist btusb"),
			"with its blacklist lines taken away");
	}

	/* A helper that refuses (polkit saying no, say): the drop is taken, the
	 * extension stays where it was, and the alert says why. */
	qputenv("ZACOS9_MODULES_HELPER",
		(root.path() + "/bin/refusing-helper").toUtf8());
	check(writeFile(root.path() + "/bin/refusing-helper",
		"#!/bin/sh\necho 'Not authorized to perform operation.' >&2\nexit 1\n", true),
		"a helper that refuses is in place");
	int alerts = 0;
	QTimer dismiss;
	dismiss.setInterval(1);
	QObject::connect(&dismiss, &QTimer::timeout, [&] {
		for (QWidget *widget : QApplication::topLevelWidgets()) {
			if (auto *alert = dynamic_cast<Alert *>(widget); alert && alert->isVisible()) {
				alerts++;
				alert->accept();
			}
		}
	});
	dismiss.start();
	Item *again = itemNamed(extensions, "Bluetooth");
	std::unique_ptr<QMimeData> refused(again ? itemDragMime({ again }) : nullptr);
	if (refused) {
		QDropEvent no(QPointF(1000, 700), Qt::MoveAction, refused.get(), Qt::LeftButton,
			Qt::NoModifier);
		off->dropEvent(&no);
		check(waitFor([&] { return alerts > 0; }), "a refused turn-off says why");
	}
	dismiss.stop();
	check(itemNamed(extensions, "Bluetooth") != nullptr &&
		itemNamed(off, "Bluetooth") == nullptr,
		"and the extension stays in Extensions");
	check(!read(blacklistFile).contains("blacklist btusb"), "with nothing written");

	/* A file dropped into (Disabled) is refused. */
	writeFile(root.path() + "/stray.txt", "not an extension");
	QMimeData fileMime;
	fileMime.setUrls({ QUrl::fromLocalFile(root.path() + "/stray.txt") });
	QDropEvent stray(QPointF(1000, 700), Qt::CopyAction, &fileMime, Qt::LeftButton,
		Qt::NoModifier);
	off->dropEvent(&stray);
	settle();
	check(!stray.isAccepted(), "a file dropped into Extensions (Disabled) is refused");

	return fails ? 1 : 0;
}
