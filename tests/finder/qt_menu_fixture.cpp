#include <QApplication>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <cstdio>

int main(int argc, char **argv) {
	QApplication app(argc, argv);
	QMainWindow window;
	window.setWindowTitle("Qt menu regression");
	auto *menu = window.menuBar()->addMenu("&File");
	auto *action = menu->addAction("&Qt action");
	QObject::connect(action, &QAction::triggered, [] {
		puts("ACTIVATED Qt action");
		fflush(stdout);
	});
	window.resize(320, 180);
	window.show();
	return app.exec();
}
