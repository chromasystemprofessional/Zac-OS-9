/*
 * platinum-finder: the Finder. Owns the desktop (a layer-shell surface)
 * and the spatial folder windows. Like the Mac's, it never quits.
 */
#include <QApplication>
#include <QScreen>
#include <QTimer>

#include "desktop.h"
#include "finder.h"
#include "platinumshell.h"
#include "settings.h"

int main(int argc, char *argv[]) {
	/* platinum-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");

	QApplication app(argc, argv);
	QApplication::setApplicationName("Finder");
	QGuiApplication::setDesktopFileName("platinum-finder"); /* Wayland app_id */
	QApplication::setDoubleClickInterval(pl_double_click_ms()); /* the Mouse panel's */
	QApplication::setQuitOnLastWindowClosed(false);
	platinumShellInit();

	QScreen *screen = QGuiApplication::primaryScreen();
	const auto makeDesktop = [screen] {
		auto *d = new Desktop;
		d->resize(screen->size());
		d->becomeLayerSurface();
		d->show();
		return d;
	};
	Desktop *desktop = makeDesktop();
	Finder::instance().start(desktop);
	/* The Monitors panel can change the screen's size or scale. Qt keeps
	 * a widget's backing store at the scale it started with, so the
	 * desktop is replaced by a fresh one drawn at the new scale. */
	QObject::connect(screen, &QScreen::geometryChanged, &app, [&desktop, makeDesktop] {
		QTimer::singleShot(0, [&desktop, makeDesktop] {
			Desktop *old = desktop;
			desktop = makeDesktop();
			Finder::instance().replaceDesktop(desktop);
			old->hide();
			old->deleteLater();
		});
	});
	return app.exec();
}
