#include <QApplication>

#include "barkrecorder.h"
#include "platinumshell.h"
#include "settings.h"

int main(int argc, char **argv) {
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Bark Recorder");
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("zacos9-barkrecorder");
	platinumShellInit();
	BarkRecorder window;
	window.show();
	return app.exec();
}
