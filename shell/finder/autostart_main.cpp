#include <QCoreApplication>
#include <QTextStream>

#include "autostart.h"

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	QString error;
	if (!autostartRunSession(&error)) {
		QTextStream(stderr) << "zacos9-autostart: " << error << '\n';
		return 1;
	}
	return 0;
}
