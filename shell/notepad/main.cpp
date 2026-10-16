#include <QApplication>
#include <QCommandLineParser>

#include "notepadwindow.h"
#include "platinumshell.h"
#include "settings.h"

int main(int argc, char **argv) {
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Notepad");
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("zacos9-notepad");
	QCommandLineParser parser;
	parser.addHelpOption();
	QCommandLineOption pageOption("page", "Open a page from 1 to 8.", "number", "1");
	parser.addOption(pageOption);
	parser.process(app);
	bool ok = false;
	const int page = parser.value(pageOption).toInt(&ok);
	if (!ok || page < 1 || page > 8) {
		parser.showHelp(2);
	}
	platinumShellInit();
	NotepadWindow window(page - 1);
	window.show();
	return app.exec();
}
