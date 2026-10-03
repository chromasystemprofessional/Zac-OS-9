#include <QApplication>
#include <QGuiApplication>

#include "installer.h"

int main(int argc, char *argv[]) {
	/* The compositor draws every window frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Installer");
	QGuiApplication::setDesktopFileName("zacos9-installer");

	InstallerWindow w;
	w.show();
	return app.exec();
}
