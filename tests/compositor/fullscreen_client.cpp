#include <QApplication>
#include <QScreen>
#include <QTextStream>
#include <QTimer>
#include <QWidget>
#include <QWindow>

int main(int argc, char **argv) {
	QApplication app(argc, argv);
	QWidget window;
	window.setWindowTitle("Fullscreen protocol test");
	window.resize(640, 400);
	window.setStyleSheet("background-color: #00ff00;");
	window.show();
	QSize original;
	QPoint originalPosition;
	QTimer::singleShot(500, &window, [&] {
		original = window.size();
		originalPosition = window.pos();
		window.windowHandle()->setScreen(app.screens().last());
		window.showFullScreen();
	});
	QTimer::singleShot(1400, &window, [&] {
		const QSize expected = window.screen()->size();
		if (!window.isFullScreen() || window.size() != expected) {
			QTextStream(stderr) << "Fullscreen size " << window.width() << "x" << window.height()
				<< "; expected " << expected.width() << "x" << expected.height() << "\n";
			app.exit(1);
			return;
		}
		QTextStream(stdout) << "ok: fullscreen protocol acknowledged at "
			<< window.width() << "x" << window.height() << "\n";
		window.showNormal();
	});
	QTimer::singleShot(2300, &window, [&] {
		/* Wayland does not expose a toplevel's global position to its client. */
		const bool positionChanged = app.platformName() == "xcb" && window.pos() != originalPosition;
		if (window.isFullScreen() || window.size() != original || positionChanged) {
			QTextStream(stderr) << "Restored window " << window.width() << "x" << window.height()
				<< " at " << window.x() << "," << window.y() << "; expected "
				<< original.width() << "x" << original.height() << " at "
				<< originalPosition.x() << "," << originalPosition.y() << "\n";
			app.exit(1);
			return;
		}
		QTextStream(stdout) << "ok: previous window size restored\n";
		app.exit(0);
	});
	return app.exec();
}
