/*
 * zacos9-pedigree: Pedigree, ZacOS 9's Apple System Profiler (Missing
 * Manual ch. 13): what this computer is, what's in it and plugged into
 * it, and what it runs. Nothing is changed and no password is asked for.
 */
#include <QApplication>

#include "pedigreewindow.h"
#include "platinumshell.h"
#include "settings.h"

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Pedigree");
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("zacos9-pedigree"); /* Wayland app_id */
	platinumShellInit();
	PedigreeWindow window;
	window.show();
	return app.exec();
}
