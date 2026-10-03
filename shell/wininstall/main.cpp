#include <QApplication>
#include <QGuiApplication>

#include "wininstall.h"

int main(int argc, char *argv[]) {
	/* The compositor draws every window frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Windows Installer");
	QGuiApplication::setDesktopFileName("zacos9-wininstall");

	WinInstallWindow w(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString());
	w.show();
	return app.exec();
}
