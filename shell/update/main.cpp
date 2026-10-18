/*
 * zacos9-update: Software Update, from the Apple menu (see update.h).
 */
#include <QApplication>

#include "platinumshell.h"
#include "update.h"
#include "settings.h"

#include <cstring>

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Software Update");
	QGuiApplication::setDesktopFileName("zacos9-update");
	platinumShellInit();

	UpdateWindow w;
	if (app.arguments().contains("--background")) {
		char v[16];
		if (pl_setting("update-check", v, sizeof(v)) && strcmp(v, "never") == 0) {
			return 0; /* Check daily is off */
		}
		w.background = true;
	} else {
		w.show();
		platinumSetFrameStyle(&w, FrameStyle::MovableModal);
	}
	w.start();
	return app.exec();
}
