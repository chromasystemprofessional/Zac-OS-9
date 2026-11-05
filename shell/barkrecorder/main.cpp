#include <QApplication>

#include "barkrecorder.h"
#include "platinumshell.h"
#include "settings.h"
#include "tr.h"

int main(int argc, char **argv) {
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	zacos9_i18n_init();
	QApplication::setApplicationName(tr9("Bark Recorder"));
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("zacos9-barkrecorder");
	platinumShellInit();
	BarkRecorder window;
	window.show();
	return app.exec();
}
