/*
 * zacos9-picture: Picture Viewer, the default application for picture files.
 * zacos9-picture FILE... opens a window per picture; with no files it asks for one.
 */
#include <QApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QUrl>

#include "alert.h"
#include "picturewindow.h"
#include "platinumshell.h"
#include "settings.h"

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Picture Viewer");
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("zacos9-picture"); /* Wayland app_id */
	platinumShellInit();
	// Camera pictures can be larger than Qt's default 256 MB decoding limit.
	QImageReader::setAllocationLimit(2048);

	int opened = 0;
	for (const QString &arg : app.arguments().mid(1)) {
		const QString path = arg.startsWith("file://") ? QUrl(arg).toLocalFile() : arg;
		auto *window = new PictureWindow;
		QString error;
		if (!window->open(path, &error)) {
			delete window;
			Alert::ask(QString::fromUtf8("\u201c%1\u201d could not be opened. %2").arg(QFileInfo(path).fileName(), error),
				"OK", QString());
			continue;
		}
		window->show();
		++opened;
	}
	if (app.arguments().size() > 1 && !opened) {
		return 1;
	}
	if (!opened) {
		auto *window = new PictureWindow;
		if (!window->chooseFile()) {
			return 0;
		}
	}
	return app.exec();
}
