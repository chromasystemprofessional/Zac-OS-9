/*
 * platinum-appearance: the Appearance control panel.
 *
 *   platinum-appearance [color | desktop | sound]
 *
 * The argument picks the tab it opens on.
 */
#include <QApplication>

#include "appearance.h"

int main(int argc, char *argv[]) {
	/* platinum-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Appearance");
	QGuiApplication::setDesktopFileName("platinum-appearance"); /* Wayland app_id */

	AppearancePanel panel;
	const QString tab = argc > 1 ? QString::fromUtf8(argv[1]) : QString();
	panel.showTab(tab == "desktop" ? 1 : tab == "sound" ? 2 : 0);
	panel.show();
	return app.exec();
}
