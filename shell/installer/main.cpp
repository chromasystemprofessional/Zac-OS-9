#include <QApplication>
#include <QGuiApplication>
#include <cstring>

#include "installer.h"
#include "tr.h"

int main(int argc, char *argv[]) {
	/* The compositor draws every window frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QString locale;
	for (int i = 1; i + 1 < argc; i++) {
		if (strcmp(argv[i], "--language") == 0) {
			locale = QString::fromUtf8(argv[i + 1]);
		}
	}
	QApplication app(argc, argv);
	zacos9_i18n_init();
	QApplication::setApplicationName(tr9("Installer"));
	QGuiApplication::setDesktopFileName("zacos9-installer");

	InstallerWindow w(locale);
	w.show();
	return app.exec();
}
