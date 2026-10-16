/*
 * zacos9-keytags: Key Tags, ZacOS 9's Key Caps. Shows the keyboard layout
 * in use, and follows ⌘-Space and the keyboard menu to another.
 */
#include <QApplication>

#include "keytagswindow.h"
#include "platinumshell.h"
#include "settings.h"
#include "xkblayouts.h"

int main(int argc, char **argv) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Key Tags");
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("zacos9-keytags"); /* Wayland app_id */
	platinumShellInit();
	auto *window = new KeyTagsWindow;
	window->setAttribute(Qt::WA_DeleteOnClose);
	/* The layout zacos9-wm says is in use; without it, the first. */
	const QStringList inUse = xkbLayoutsInUse();
	if (!window->showLayout(platinumKeyboardLayout()) &&
			!(!inUse.isEmpty() && window->showLayout(inUse.first()))) {
		window->showLayout("us");
	}
	platinumOnKeyboardLayout(window, [window] {
		window->showLayout(platinumKeyboardLayout());
	});
	window->show();
	return app.exec();
}
