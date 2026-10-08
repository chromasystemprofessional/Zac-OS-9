#include <QApplication>

bool registerFileChooserPortal();

int main(int argc, char **argv) {
	QApplication app(argc, argv);
	if (!registerFileChooserPortal()) {
		return 1;
	}
	return app.exec();
}
