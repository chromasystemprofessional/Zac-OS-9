/*
 * zacos9-appearance: the Appearance control panel.
 *
 *   zacos9-appearance [color | desktop | wallpaper | sound]
 *
 * The argument picks the tab it opens on.
 */
#include <QApplication>

#include "appearance.h"

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Appearance");
	QGuiApplication::setDesktopFileName("zacos9-appearance"); /* Wayland app_id */

	AppearancePanel panel;
	const QString tab = argc > 1 ? QString::fromUtf8(argv[1]) : QString();
	panel.showTab(tab == "desktop" ? 1 : tab == "wallpaper" ? 2 : tab == "sound" ? 3
		: tab == "themes" ? 4 : tab == "sound-themes" ? 5 : 0);
	panel.show();
	return app.exec();
}
