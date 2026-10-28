/*
 * zacos9-photobooth: Photo Booth, the Apple menu's camera.
 */
#include <QApplication>

#include <gst/gst.h>

#include "photoboothwindow.h"
#include "platinumshell.h"
#include "settings.h"

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	gst_init(&argc, &argv);
	QApplication app(argc, argv);
	QApplication::setApplicationName("Photo Booth");
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("zacos9-photobooth"); /* Wayland app_id */
	platinumShellInit();

	PhotoBoothWindow window;
	window.show();
	return app.exec();
}
