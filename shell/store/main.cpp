/*
 * zacos9-store: the Software window (an app catalog over apt), from
 * the Apple menu.
 */
#include <QApplication>

#include "platinumshell.h"
#include "store.h"
#include "tr.h"

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	zacos9_i18n_init();
	QApplication::setApplicationName(tr9("Software"));
	QGuiApplication::setDesktopFileName("zacos9-store"); /* Wayland app_id */
	platinumShellInit(); /* the Remove confirmation is a movable modal alert */

	StoreWindow window;
	window.show();
	return app.exec();
}
