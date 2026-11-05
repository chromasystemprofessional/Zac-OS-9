/*
 * zacos9-appearance: the Appearance control panel.
 *
 *   zacos9-appearance [themes | appearance | fonts | desktop | sound | options]
 *
 * The argument picks the tab it opens on (Mac OS 9's order). Older names
 * still work: color means appearance, wallpaper means desktop, and
 * sound-themes means sound.
 */
#include <QApplication>

#include "appearance.h"
#include "tr.h"

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	zacos9_i18n_init();
	QApplication::setApplicationName(tr9("Appearance"));
	QGuiApplication::setDesktopFileName("zacos9-appearance"); /* Wayland app_id */

	AppearancePanel panel;
	const QString tab = argc > 1 ? QString::fromUtf8(argv[1]) : QString();
	panel.showTab(tab == "appearance" || tab == "color" ? 1 : tab == "fonts" ? 2
		: tab == "desktop" || tab == "wallpaper" ? 3 : tab == "sound" || tab == "sound-themes" ? 4
		: tab == "options" ? 5 : 0);
	panel.show();
	return app.exec();
}
