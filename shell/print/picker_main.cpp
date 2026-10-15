/*
 * zacos9-printerpicker: Printer Picker, ZacOS 9's Chooser (Missing Manual
 * ch. 19): choose the printer documents go to, and make desktop printers.
 */
#include <QApplication>

#include "pickerwindow.h"
#include "platinumshell.h"
#include "settings.h"

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Printer Picker");
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("zacos9-printerpicker"); /* Wayland app_id */
	platinumShellInit();
	PickerWindow window;
	window.show();
	return app.exec();
}
