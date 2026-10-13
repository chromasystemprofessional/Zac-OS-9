/*
 * zacos9-netbrowser: the Network Browser, from the Apple menu.
 */
#include <QApplication>
#include <QTimer>

#include "netbrowser.h"
#include "platinumshell.h"
#include "settings.h"

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Neighbourhood");
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("zacos9-netbrowser"); /* Wayland app_id */
	platinumShellInit(); /* dialogs are movable modals */

	NetBrowserWindow window;
	window.show();
	/* --connect afp|smb ADDRESS: the Zac menu's Recent Servers. */
	if (argc == 4 && QString(argv[1]) == "--connect") {
		const DiscoveredServer::Kind kind = QString(argv[2]) == "smb"
			? DiscoveredServer::SMB : DiscoveredServer::AFP;
		const QString address = QString::fromUtf8(argv[3]);
		QTimer::singleShot(0, &window, [&window, address, kind] { window.connectTo(address, kind); });
	}
	return app.exec();
}
