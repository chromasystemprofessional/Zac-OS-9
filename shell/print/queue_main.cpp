/*
 * zacos9-printqueue: a printer's queue window, as a desktop printer opens
 * (Mac OS 9's desktop printer window and PrintMonitor).
 *
 *   zacos9-printqueue [QUEUE] [FILE...]   FILEs are printed there first
 *
 * Without a queue, your default printer's.
 */
#include <QApplication>
#include <QFileInfo>

#include "alert.h"
#include "platinumshell.h"
#include "printers.h"
#include "queuewindow.h"
#include "settings.h"

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Print Queue");
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("zacos9-printqueue"); /* Wayland app_id */
	platinumShellInit();
	QStringList args = app.arguments().mid(1);
	QString queue = !args.isEmpty() && !QFileInfo::exists(args.first()) ? args.takeFirst() : defaultPrinter();
	if (queue.isEmpty()) {
		Alert::ask("No printer has been chosen. Choose one in Printer Picker.", "OK", QString());
		return 1;
	}
	QueueWindow window(queue);
	window.show();
	if (!args.isEmpty()) {
		window.printDropped(args);
	}
	return app.exec();
}
