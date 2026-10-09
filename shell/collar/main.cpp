#include <QApplication>
#include <QDBusConnection>
#include <QDebug>
#include "collar.h"
#include "platinumshell.h"
#include "settings.h"

int main(int argc, char **argv) {
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	app.setApplicationName("zacos9-collar");
	app.setDesktopFileName("zacos9-collar");
	app.setDoubleClickInterval(pl_double_click_ms());
	app.setQuitOnLastWindowClosed(false);
	auto bus = QDBusConnection::sessionBus();
	if (!bus.registerService("org.zacos9.Collar")) {
		qWarning("Couldn't start The Collar: its D-Bus name is unavailable (it may already be running).");
		return 1;
	}
	platinumShellInit();
	Collar collar;
	collar.show();
	return app.exec();
}
