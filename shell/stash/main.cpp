/*
 * zacos9-stash: Stash, ZacOS 9's Scrapbook. Pages of text and pictures
 * kept between sessions in ~/.local/share/zacos9/Stash.
 */
#include <QApplication>

#include "platinumshell.h"
#include "settings.h"
#include "stashwindow.h"

int main(int argc, char **argv) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Stash");
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("zacos9-stash"); /* Wayland app_id */
	platinumShellInit();
	auto *window = new StashWindow;
	window->show();
	return app.exec();
}
