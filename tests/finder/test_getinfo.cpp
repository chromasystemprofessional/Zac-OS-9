/* Get Info's icon: Command-C copies it and Command-V pastes it, on an
 * ordinary file and on an item whose details are virtual (an application,
 * whose icon lives on its desktop entry). */
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTextStream>

#include "items.h"

#define private public
#define protected public
#include "infowindow.h"
#undef protected
#undef private

static int failures;

static void check(bool ok, const char *what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	failures += !ok;
}

static InfoWindow *findInfo(const QString &path) {
	for (QWidget *widget : QApplication::topLevelWidgets()) {
		if (auto *window = dynamic_cast<InfoWindow *>(widget); window && window->m_path == path) {
			return window;
		}
	}
	return nullptr;
}

static void sendKey(QWidget *w, int key, Qt::KeyboardModifiers modifiers) {
	QKeyEvent event(QEvent::KeyPress, key, modifiers);
	QApplication::sendEvent(w, &event);
}

static bool writeFile(const QString &path) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write("x") == 1;
}

int main(int argc, char **argv) {
	QTemporaryDir dir;
	check(dir.isValid(), "create a temporary folder");
	/* Private settings and custom-icon storage: nothing of the real
	 * desktop's is read or written. */
	QDir().mkpath(dir.path() + "/data/applications");
	QDir().mkpath(dir.path() + "/system/applications");
	qputenv("HOME", dir.path().toUtf8());
	qputenv("XDG_DATA_HOME", (dir.path() + "/data").toUtf8());
	qputenv("XDG_DATA_DIRS", (dir.path() + "/system").toUtf8());
	qputenv("XDG_CONFIG_HOME", (dir.path() + "/config").toUtf8());

	QApplication app(argc, argv);

	const QString sourcePath = dir.path() + "/icon-source.txt";
	const QString targetPath = dir.path() + "/icon-target.txt";
	check(writeFile(sourcePath) && writeFile(targetPath), "create two files");

	/* A custom icon with transparent corners: copying the rendered icon
	 * box would bring the window's gray face with it. */
	QImage sourceIcon(PL_ICON_LARGE, PL_ICON_LARGE, QImage::Format_ARGB32);
	sourceIcon.fill(Qt::transparent);
	for (int y = 8; y < 24; y++) {
		for (int x = 8; x < 24; x++) {
			sourceIcon.setPixel(x, y, 0xffff0000);
		}
	}
	check(writeCustomIconImage(sourcePath, sourceIcon), "give the source a custom icon");

	InfoWindow::open(sourcePath, PL_ICON_DOCUMENT, "icon-source.txt");
	QApplication::processEvents();
	InfoWindow *source = findInfo(sourcePath);
	check(source != nullptr, "the source's Get Info opens");
	if (source) {
		source->show();
		QApplication::processEvents();
		check(!source->m_iconFocused, "the icon starts unselected");
		const QString hint = source->m_host.hintAt(QPoint(30, 20));
		check(hint.contains(QString::fromUtf8("⌘-C")) &&
			hint.contains(QString::fromUtf8("⌘-V")),
			"the icon's Hint Bubble names the keys that copy and paste it");

		sendKey(source, Qt::Key_C, Qt::ControlModifier);
		const QImage copied = QApplication::clipboard()->image();
		check(copied.size() == QSize(PL_ICON_LARGE, PL_ICON_LARGE),
			"Command-C copies a 32x32 picture");
		check(!copied.isNull() && qAlpha(copied.pixel(0, 0)) == 0 &&
			copied.pixel(16, 16) == 0xffff0000,
			"the copied icon keeps its transparency and colors");
		source->close();
		QApplication::processEvents();
	}

	InfoWindow::open(targetPath, PL_ICON_DOCUMENT, "icon-target.txt");
	QApplication::processEvents();
	InfoWindow *target = findInfo(targetPath);
	check(target != nullptr, "the target's Get Info opens");
	if (target) {
		target->show();
		QApplication::processEvents();
		sendKey(target, Qt::Key_V, Qt::ControlModifier);
		const QImage pasted = customIconImage(targetPath);
		check(!pasted.isNull() && pasted.pixel(16, 16) == 0xffff0000 &&
			qAlpha(pasted.pixel(0, 0)) == 0,
			"Command-V sets the target's icon from the Clipboard");

		/* Clicking the icon selects it, and Delete removes the icon. */
		const QPoint at(30, 20);
		QMouseEvent press(QEvent::MouseButtonPress, at, target->mapToGlobal(at),
			Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
		QApplication::sendEvent(target, &press);
		check(target->m_iconFocused, "clicking the icon selects it");
		sendKey(target, Qt::Key_Delete, Qt::NoModifier);
		check(customIconImage(targetPath).isNull(), "Delete removes the custom icon");
		target->close();
		QApplication::processEvents();
	}

	/* A virtual Get Info (an application) edits the icon of the real file
	 * that stands for it. */
	const QString appPath = dir.path() + "/fixture.desktop";
	const QString virtualPath = "vfs:/applications/fixture.desktop";
	check(writeFile(appPath), "create the application file behind a virtual item");
	ResourceDetails details;
	details.kind = "application program";
	details.location = "ZacOS 9:Applications";
	InfoWindow::openVirtual(virtualPath, PL_ICON_APPLICATION, "Fixture", details, appPath);
	QApplication::processEvents();
	InfoWindow *application = findInfo(virtualPath);
	check(application != nullptr, "the virtual application's Get Info opens");
	if (application) {
		application->show();
		QApplication::processEvents();
		check(application->canChangeIcon(), "a virtual application can change its icon");
		sendKey(application, Qt::Key_V, Qt::ControlModifier);
		const QImage icon = customIconImage(appPath);
		check(!icon.isNull() && icon.pixel(16, 16) == 0xffff0000,
			"pasting into a virtual application sets its desktop entry's icon");
		application->close();
		QApplication::processEvents();
	}

	return failures ? 1 : 0;
}
