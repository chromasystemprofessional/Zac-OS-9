/*
 * zacos9-collarpanel: the Collar control panel. Not `zacos9-controlpanel
 * collar`, whose app_id would be the Collar's own.
 */
#include <QApplication>

#include "collarpanel.h"
#include "platinumshell.h"

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Collar");
	QGuiApplication::setDesktopFileName("zacos9-collarpanel"); /* Wayland app_id */
	platinumShellInit();

	CollarPanel panel;
	panel.show();
	return app.exec();
}
