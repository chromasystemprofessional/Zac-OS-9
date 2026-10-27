/* For test_idle.py, test_inhibit.py and test_lock.py:
 *   idle_client             a window filling the last screen until killed;
 *                           prints "fullscreen SCREEN" once it does
 *   idle_client --dbus      an ordinary window that asks
 *                           org.freedesktop.ScreenSaver to stay awake, as a
 *                           browser playing video does; prints "inhibited
 *                           COOKIE" once the window shows
 *   idle_client --keys      an ordinary window that prints "shown", then
 *                           "key TEXT" for every key it gets, "press X Y" and
 *                           "release X Y" for every click
 *   idle_client --field     a window with a text field, focused, and an Off
 *                           button below it that takes the focus: prints
 *                           "shown", then "text TEXT" as the field changes
 *                           (test_onscreen_keyboard.py); built as
 *                           test-field-client, it says so as ZacOS's own
 *                           programs do (platinumShellInit)
 *   ... --app-id ID         with that Wayland app id (test_app_switch.py) */
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QDBusInterface>
#include <QDBusReply>
#include <QMouseEvent>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <cstdio>
#include <cstring>
#ifdef PLATINUM_FIELD
#include "platinumshell.h"
#endif

/* Says which keys reach it. */
class KeyWindow : public QWidget {
protected:
	void keyPressEvent(QKeyEvent *e) override {
		std::printf("key %s\n", qPrintable(e->text()));
		std::fflush(stdout);
	}
	void mousePressEvent(QMouseEvent *e) override {
		std::printf("press %d %d\n", e->position().toPoint().x(), e->position().toPoint().y());
		std::fflush(stdout);
	}
	void mouseReleaseEvent(QMouseEvent *e) override {
		std::printf("release %d %d\n", e->position().toPoint().x(), e->position().toPoint().y());
		std::fflush(stdout);
	}
};

int main(int argc, char **argv) {
	QApplication app(argc, argv);
	for (int i = 1; i + 1 < argc; i++) {
		if (std::strcmp(argv[i], "--app-id") == 0) {
			QGuiApplication::setDesktopFileName(argv[i + 1]);
		}
	}
	const bool dbus = argc > 1 && std::strcmp(argv[1], "--dbus") == 0;
	const bool keys = argc > 1 && std::strcmp(argv[1], "--keys") == 0;
	if (argc > 1 && std::strcmp(argv[1], "--field") == 0) {
#ifdef PLATINUM_FIELD
		platinumShellInit();
#endif
		QWidget form;
		form.setWindowTitle("Field test");
		auto *field = new QLineEdit(&form);
		auto *off = new QPushButton("Off", &form);
		off->setFocusPolicy(Qt::StrongFocus);
		auto *column = new QVBoxLayout(&form);
		column->addWidget(field);
		column->addWidget(off);
		QObject::connect(field, &QLineEdit::textChanged, [](const QString &text) {
			std::printf("text %s\n", qPrintable(text));
			std::fflush(stdout);
		});
		form.resize(320, 120);
		form.show();
		QTimer::singleShot(500, &form, [&] {
			form.activateWindow();
			field->setFocus();
			std::printf("shown\n");
			std::fflush(stdout);
		});
		return app.exec();
	}
	KeyWindow window;
	window.setWindowTitle("Idle test film");
	window.setAttribute(Qt::WA_StyledBackground, true); /* a subclass, so the style sheet paints */
	window.setStyleSheet("background-color: #00aa00;");
	window.resize(320, 200);
	window.show();
	if (keys) {
		QTimer::singleShot(500, &window, [&] {
			window.activateWindow();
			std::printf("shown\n");
			std::fflush(stdout);
		});
		return app.exec();
	}
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
