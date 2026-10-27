/*
 * Sounds and its User collection: a WAVE recording dragged into either is
 * installed as an alert sound, the way the Appearance panel's Add Recording
 * installs one, and a file that is not a recording, a name already taken or
 * the installed set are refused with a reason. Driven through the windows'
 * own drag and drop, in a private XDG environment, so nothing of the real
 * desktop's is read or written.
 */
#include <QApplication>
#include <QDir>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include "alert.h"
#include "customthemes.h"
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

static Item *itemNamed(FolderWindow *w, const QString &name) {
	for (const auto &item : w->m_items) {
		if (item->name == name) {
			return item.get();
		}
	}
	return nullptr;
}

static bool dropAt(FolderWindow *w, QPoint p, QMimeData *mime) {
	QDropEvent drop(QPointF(p), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
	w->dropEvent(&drop);
	return drop.isAccepted();
}

int main(int argc, char **argv) {
	if (argc != 2) {
		return 1;
	}
	QTemporaryDir root;
	if (!root.isValid()) {
		return 1;
	}
	/* A private desktop: the user's sounds folder is the one under test,
	 * and the installed set comes from the source tree's assets. */
	const QString dataHome = root.path() + "/data";
	const QString soundsFolder = dataHome + "/zacos9/sounds";
	qputenv("HOME", root.path().toUtf8());
	qputenv("XDG_DATA_HOME", dataHome.toUtf8());
	qputenv("XDG_DATA_DIRS", (root.path() + "/system").toUtf8());
	qputenv("XDG_CONFIG_HOME", (root.path() + "/config").toUtf8());
	qputenv("XDG_CURRENT_DESKTOP", "ZacOS9");
	QDir().mkpath(soundsFolder);

	QApplication app(argc, argv);
	vfsRefresh();

	FolderWindow *sounds = FolderWindow::open(vfsPathFor("system-folder/sounds"));
	sounds->resize(520, 360);
	sounds->relayout();
	settle();

	Item *user = itemNamed(sounds, "User");
	Item *computer = itemNamed(sounds, "Computer");
	check(user != nullptr && computer != nullptr,
		"Sounds lists the user's own folder and the installed set");
	check(vfsIsSounds(sounds->m_path) && user && vfsIsSounds(user->path),
		"and both are where a dragged sound is installed");
	if (!user || !computer) {
		return 1;
	}
	const QString userPath = user->path;
	FolderWindow *userWindow = FolderWindow::open(userPath);
	userWindow->resize(520, 360);
	userWindow->relayout();
	settle();
	check(userWindow->m_items.empty(), "the user's folder starts out empty");

	/* A recording dragged in: installed under its own name. */
	const QString recording = root.path() + "/Dropped Chime.wav";
	check(QFile::copy(QString::fromUtf8(argv[1]), recording),
		"a recording is ready to drag in");
	QMimeData mime;
	mime.setUrls({ QUrl::fromLocalFile(recording) });
	check(dropAt(sounds, QPoint(460, 330), &mime), "Sounds takes the drop");
	check(QFile::exists(soundsFolder + "/Dropped_Chime.wav"),
		"the recording is installed in the user's folder");
	check(alertSoundIds().contains("Dropped_Chime"),
		"and the list the Sound and Appearance panels offer has it");
	check(waitFor([&] { return itemNamed(userWindow, "Dropped_Chime.wav") != nullptr; }),
		"a user's folder window open at the time lists it");

	/* Dropped on the User collection's own item, likewise. */
	const QString second = root.path() + "/Tile Drop.wav";
	check(QFile::copy(QString::fromUtf8(argv[1]), second), "a second recording is ready");
	Item *userAgain = itemNamed(sounds, "User");
	QMimeData tileMime;
	tileMime.setUrls({ QUrl::fromLocalFile(second) });
	check(userAgain && dropAt(sounds, sounds->toWindow(userAgain->pos + QPoint(10, 10)),
			&tileMime), "the User folder takes the drop");
	check(waitFor([&] { return itemNamed(userWindow, "Tile_Drop.wav") != nullptr; }),
		"a recording dropped on the User folder is installed there");

	/* A name already taken, and a file that is not a recording: both say
	 * why, and neither writes anything. */
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
	QMimeData again;
	again.setUrls({ QUrl::fromLocalFile(recording) });
	dropAt(sounds, QPoint(460, 330), &again);
	check(waitFor([&] { return alerts > 0; }), "the same recording dropped again says why");
	check(QDir(soundsFolder).entryList({ "Dropped_Chime*" }, QDir::Files).size() == 1,
		"and does not make a second copy");
	const QString notASound = root.path() + "/Not A Sound.wav";
	QFile text(notASound);
	check(text.open(QIODevice::WriteOnly) && text.write("not a wave file") > 0,
		"a file that is not a recording is ready");
	text.close();
	QMimeData bad;
	bad.setUrls({ QUrl::fromLocalFile(notASound) });
	dropAt(sounds, QPoint(460, 330), &bad);
	check(waitFor([&] { return alerts > 1; }),
		"a file that is not a recording is refused with a reason");
	check(!QFile::exists(soundsFolder + "/Not_A_Sound.wav"), "and nothing is written for it");
	dismiss.stop();

	/* The installed set is read-only: it is not a drop target, and the
	 * drop is refused rather than falling through to the folder behind
	 * the icon. */
	Item *computerAgain = itemNamed(sounds, "Computer");
	QMimeData onComputer;
	onComputer.setUrls({ QUrl::fromLocalFile(second) });
	QDropEvent refused(QPointF(computerAgain ? sounds->toWindow(computerAgain->pos + QPoint(10, 10))
			: QPoint(460, 330)),
		Qt::CopyAction, &onComputer, Qt::LeftButton, Qt::NoModifier);
	sounds->dropEvent(&refused);
	settle();
	check(computerAgain && !refused.isAccepted(),
		"the installed set refuses a drop instead of taking it for the User folder");
	check(QDir(soundsFolder).entryList({ "*.wav" }, QDir::Files).size() == 2,
		"and the user's folder is unchanged");

	return fails ? 1 : 0;
}
