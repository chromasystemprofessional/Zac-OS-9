#include <QApplication>
#include <QGuiApplication>

#include "setup.h"
#include "tr.h"

int main(int argc, char *argv[]) {
	/* The compositor draws every window frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	zacos9_i18n_init();
	QApplication::setApplicationName(tr9("Setup Assistant"));
	QGuiApplication::setDesktopFileName("zacos9-setup");

	SetupWindow w;
	w.show();
	return app.exec();
}
