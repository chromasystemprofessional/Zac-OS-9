#include <QApplication>
#include <QGuiApplication>

#include "setup.h"

int main(int argc, char *argv[]) {
	/* The compositor draws every window frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Setup Assistant");
	QGuiApplication::setDesktopFileName("zacos9-setup");

	SetupWindow w;
	w.show();
	return app.exec();
}
