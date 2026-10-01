/*
 * platinum-finder: the Finder. Owns the desktop (a layer-shell surface)
 * and the spatial folder windows. Like the Mac's, it never quits.
 */
#include <QApplication>
#include <QScreen>

#include "desktop.h"

int main(int argc, char *argv[]) {
	/* platinum-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");

	QApplication app(argc, argv);
	QApplication::setApplicationName("Finder");
	QGuiApplication::setDesktopFileName("platinum-finder"); /* Wayland app_id */
	QApplication::setDoubleClickInterval(533);              /* Mac OS default */
	QApplication::setQuitOnLastWindowClosed(false);

	Desktop desktop;
	desktop.resize(QGuiApplication::primaryScreen()->size());
	desktop.becomeLayerSurface();
	desktop.show();
	return app.exec();
}
