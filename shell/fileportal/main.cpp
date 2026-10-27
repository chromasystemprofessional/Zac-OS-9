#include "filechooserportal.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char **argv) {
	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);
	if (!registerFileChooserPortal()) {
		return 1;
	}
	QTimer::singleShot(0, &app, fileChooserWarmUp);
	return app.exec();
}
