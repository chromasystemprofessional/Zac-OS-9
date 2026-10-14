/*
 * zacos9-scribble: Scribble, ZacOS 9's SimpleText. zacos9-scribble FILE...
 * opens a window for each; with none, an untitled one.
 */
#include <QApplication>
#include <QUrl>

#include "platinumshell.h"
#include "scribblewindow.h"
#include "settings.h"

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Scribble");
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("zacos9-scribble"); /* Wayland app_id */
	platinumShellInit();
	int opened = 0;
	for (const QString &arg : app.arguments().mid(1)) {
		const QString path = arg.startsWith("file://") ? QUrl(arg).toLocalFile() : arg;
		auto *w = new ScribbleWindow;
		if (w->open(path)) {
			w->show();
			opened++;
		} else {
			delete w;
		}
	}
	if (app.arguments().size() > 1 && !opened) {
		return 1;
	}
	if (!opened) {
		ScribbleWindow::newWindow();
	}
	return app.exec();
}
