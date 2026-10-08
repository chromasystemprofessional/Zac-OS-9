/*
 * zacos9-fileexchange: the File Exchange control panel.
 */
#include <QApplication>

#include "fileexchange.h"
#include "platinumshell.h"

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("File Exchange");
	QGuiApplication::setDesktopFileName("zacos9-fileexchange"); /* Wayland app_id */
	platinumShellInit();

	FileExchangePanel panel;
	panel.show();
	return app.exec();
}
