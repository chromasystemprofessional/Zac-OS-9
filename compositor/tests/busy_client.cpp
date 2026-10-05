#include <QApplication>
#include <QSocketNotifier>
#include <QWidget>
#include <cstdio>
#include <cstring>

#include "platinumshell.h"

int main(int argc, char **argv) {
	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);
	app.setDesktopFileName("dog-cursor-test");
	QWidget window;
	window.resize(320, 220);
	window.setStyleSheet("background-color: #00ff00;");
	uint32_t cookie = 0;
	QSocketNotifier commands(fileno(stdin), QSocketNotifier::Read);
	QObject::connect(&commands, &QSocketNotifier::activated, [&] {
		char command[64];
		if (!fgets(command, sizeof(command), stdin)) {
			app.quit();
			return;
		}
		if (strcmp(command, "begin\n") == 0) {
			cookie = platinumBeginLaunch("dog-cursor-test");
			if (!cookie) {
				app.exit(1);
				return;
			}
		} else if (strcmp(command, "cancel\n") == 0) {
			platinumCancelLaunch(cookie);
		} else if (strcmp(command, "show\n") == 0) {
			window.show();
		} else if (strcmp(command, "wait\n") == 0) {
			window.setCursor(Qt::WaitCursor);
		} else if (strcmp(command, "progress\n") == 0) {
			window.setCursor(Qt::BusyCursor);
		} else if (strcmp(command, "other\n") == 0) {
			cookie = platinumBeginLaunch("other-application");
		} else if (strcmp(command, "text\n") == 0) {
			window.setCursor(Qt::IBeamCursor);
		} else if (strcmp(command, "arrow\n") == 0) {
			window.unsetCursor();
		} else if (strcmp(command, "failure\n") == 0) {
			if (platinumStartApplication("/nonexistent/zacos9-cursor-test", {})) {
				app.exit(1);
				return;
			}
		} else if (strcmp(command, "quit\n") == 0) {
			app.quit();
		} else {
			app.exit(1);
			return;
		}
		fputs("ok\n", stdout);
		fflush(stdout);
	});
	fputs("ready\n", stdout);
	fflush(stdout);
	return app.exec();
}
