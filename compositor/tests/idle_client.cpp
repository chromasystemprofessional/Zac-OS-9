/* For test_idle.py and test_inhibit.py:
 *   idle_client             a window filling the last screen until killed;
 *                           prints "fullscreen SCREEN" once it does
 *   idle_client --dbus      an ordinary window that asks
 *                           org.freedesktop.ScreenSaver to stay awake, as a
 *                           browser playing video does; prints "inhibited
 *                           COOKIE" once the window shows */
#include <QApplication>
#include <QDBusInterface>
#include <QDBusReply>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <cstdio>
#include <cstring>

int main(int argc, char **argv) {
	QApplication app(argc, argv);
	const bool dbus = argc > 1 && std::strcmp(argv[1], "--dbus") == 0;
	QWidget window;
	window.setWindowTitle("Idle test film");
	window.setStyleSheet("background-color: #00aa00;");
	window.resize(320, 200);
	window.show();
	if (dbus) {
		QTimer::singleShot(500, &window, [&] {
			QDBusInterface saver("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
				"org.freedesktop.ScreenSaver");
			QDBusReply<uint> cookie = saver.call("Inhibit", "idle-test", "Playing a film");
			std::printf("inhibited %u\n", cookie.isValid() ? cookie.value() : 0u);
			std::fflush(stdout);
		});
		return app.exec();
	}
	window.windowHandle()->setScreen(app.screens().last());
	window.showFullScreen();
	QTimer poll;
	poll.callOnTimeout([&] {
		if (window.isFullScreen() && window.size() == window.screen()->size()) {
			std::printf("fullscreen %s\n", qPrintable(window.screen()->name()));
			std::fflush(stdout);
			poll.stop();
		}
	});
	poll.start(100);
	return app.exec();
}
