/*
 * zacos9-finder: the Finder. Owns the desktop (a layer-shell surface)
 * and the spatial folder windows. Like the Mac's, it never quits.
 */
#include <QApplication>
#include <QCoreApplication>
#include <QScreen>
#include <QTimer>
#include <cstdio>

#include "desktop.h"
#include "filemanager1.h"
#include "finder.h"
#include "platinumshell.h"
#include "secondarydesktop.h"
#include "settings.h"

int main(int argc, char *argv[]) {
	/* zacos9-finder --open URI...: a folder opened from another program
	 * (xdg-open, zacos9-finder.desktop). The running Finder shows it. */
	if (argc >= 2 && QByteArray(argv[1]) == "--open") {
		QCoreApplication app(argc, argv);
		const QStringList uris = app.arguments().mid(2);
		if (fileManager1ShowFolders(uris)) {
			return 0;
		}
		fprintf(stderr, "zacos9-finder: Sniffer isn't running to open %s\n",
			qPrintable(uris.join(' ')));
		return 1;
	}

	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");

	QApplication app(argc, argv);
	QApplication::setApplicationName("Sniffer");
	QGuiApplication::setDesktopFileName("zacos9-finder"); /* Wayland app_id */
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
	/* Other programs' "Show in Folder" (Firefox's downloads, ...). */
	fileManager1Start();
	/* More than one screen: the pattern on the others. */
	SecondaryDesktop::keepInStep();
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
