/*
 * platinum-netbrowser: the Network Browser, from the Apple menu.
 */
#include <QApplication>

#include "netbrowser.h"
#include "platinumshell.h"
#include "settings.h"

int main(int argc, char *argv[]) {
	/* platinum-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Network Browser");
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("platinum-netbrowser"); /* Wayland app_id */
	platinumShellInit(); /* dialogs are movable modals */

	NetBrowserWindow window;
	window.show();
	return app.exec();
}
