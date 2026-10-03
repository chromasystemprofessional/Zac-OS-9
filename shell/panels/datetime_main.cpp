/*
 * zacos9-datetime: the Date & Time control panel.
 */
#include <QApplication>

#include "datetime.h"
#include "platinumshell.h"

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Date & Time");
	QGuiApplication::setDesktopFileName("zacos9-datetime"); /* Wayland app_id */
	platinumShellInit();

	DateTimePanel panel;
	panel.show();
	return app.exec();
}
