#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QDebug>
#include <cstring>
#include "collar.h"
#include "platinumshell.h"
#include "settings.h"

static const char *const Service = "org.zacos9.Collar";
static const char *const Path = "/org/zacos9/Collar";

/* org.zacos9.Collar.Toggle: the compositor's show/hide hot key, through zacos9-collar --toggle. */
class CollarBus : public QDBusVirtualObject {
public:
	explicit CollarBus(Collar *collar) : m_collar(collar) {}
	QString introspect(const QString &) const override {
		return "<interface name=\"org.zacos9.Collar\"><method name=\"Toggle\"/></interface>";
	}
	bool handleMessage(const QDBusMessage &message, const QDBusConnection &bus) override {
		if (message.interface() != Service || message.member() != "Toggle") {
			return false;
		}
		m_collar->toggle();
		bus.send(message.createReply());
		return true;
	}

private:
	Collar *m_collar;
};

int main(int argc, char **argv) {
	if (argc > 1 && !strcmp(argv[1], "--toggle")) {
		QCoreApplication app(argc, argv);
		const QDBusMessage reply = QDBusConnection::sessionBus().call(
			QDBusMessage::createMethodCall(Service, Path, Service, "Toggle"), QDBus::Block, 3000);
		if (reply.type() == QDBusMessage::ErrorMessage) {
			qWarning("Couldn't show or hide The Collar: %s", qPrintable(reply.errorMessage()));
			return 1;
		}
		return 0;
	}
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	app.setApplicationName("zacos9-collar");
	app.setDesktopFileName("zacos9-collar");
	app.setDoubleClickInterval(pl_double_click_ms());
	app.setQuitOnLastWindowClosed(false);
	auto bus = QDBusConnection::sessionBus();
	if (!bus.registerService(Service)) {
		qWarning("Couldn't start The Collar: its D-Bus name is unavailable (it may already be running).");
		return 1;
	}
	platinumShellInit();
	Collar collar;
	CollarBus object(&collar);
	if (!bus.registerVirtualObject(Path, &object)) {
		qWarning("The Collar's show/hide hot key won't work: its D-Bus object couldn't be registered.");
	}
	collar.applySettings();
	return app.exec();
}
