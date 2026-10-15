/*
 * zacos9-help: the Help Center (Mac OS 9's Help Viewer, Missing Manual
 * Appendix A).
 *
 *   zacos9-help                   the Help Center: every book
 *   zacos9-help BOOK | PAGE       a book ("zacos9" is ZacOS Help) or a page
 *   zacos9-help --search WORDS    the topics that mention them
 */
#include <QApplication>

#include "helpwindow.h"
#include "platinumshell.h"
#include "settings.h"

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Help Center");
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("zacos9-help"); /* Wayland app_id */
	platinumShellInit();
	const QStringList args = app.arguments().mid(1);
	HelpWindow window;
	if (args.value(0) == "--search" && args.size() > 1) {
		window.search(args.mid(1).join(' '));
	} else if (args.isEmpty() || !window.showPage(args.first())) {
		window.showCenter();
	}
	window.show();
	return app.exec();
}
