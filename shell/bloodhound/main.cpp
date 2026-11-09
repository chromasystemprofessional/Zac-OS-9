/*
 * zacos9-bloodhound: the search app. Sherlock 2's Files channel, first:
 * one window with a search field, a scope and a result list, using the
 * Sniffer's Find engine (libzacos9-files).
 */
#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>

#include "bloodhoundwindow.h"
#include "platinumshell.h"
#include "settings.h"
#include "tr.h"

int main(int argc, char **argv) {
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	zacos9_i18n_init();
	QApplication::setApplicationName(tr9("Bloodhound (Search)"));
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("zacos9-bloodhound");

	QCommandLineParser parser;
	parser.addHelpOption();
	parser.addPositionalArgument("folder", "Search this folder first.", "[folder]");
	parser.process(app);

	QString folder = parser.positionalArguments().value(0);
	if (!folder.isEmpty()) {
		const QFileInfo info(folder);
		folder = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
	}

	platinumShellInit();
	BloodhoundWindow window(folder);
	window.show();
	return app.exec();
}
