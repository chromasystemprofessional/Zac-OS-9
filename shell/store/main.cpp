/*
 * platinum-store: the Software window (an app catalog over apt), from
 * the Apple menu.
 */
#include <QApplication>

#include "platinumshell.h"
#include "store.h"

int main(int argc, char *argv[]) {
	/* platinum-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Software");
	QGuiApplication::setDesktopFileName("platinum-store"); /* Wayland app_id */
	platinumShellInit(); /* the Remove confirmation is a movable modal alert */

	StoreWindow window;
	window.show();
	return app.exec();
}
