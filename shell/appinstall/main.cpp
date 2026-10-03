/*
 * zacos9-appinstall FILE...: install application files dropped onto the
 * Applications folder (see appinstall.h).
 */
#include <QApplication>

#include "appinstall.h"
#include "platinumshell.h"

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Installer");
	QGuiApplication::setDesktopFileName("zacos9-appinstall");
	platinumShellInit();

	QStringList files;
	for (int i = 1; i < argc; i++) {
		files << QString::fromLocal8Bit(argv[i]);
	}
	if (files.isEmpty()) {
		return 0;
	}
	AppInstallWindow w(files);
	if (!w.hasWork()) {
		w.reportFailures(); /* nothing to install: just say why */
		return 0;
	}
	w.show();
	platinumSetFrameStyle(&w, FrameStyle::MovableModal);
	w.start();
	return app.exec();
}
