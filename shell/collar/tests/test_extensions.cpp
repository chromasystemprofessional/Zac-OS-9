/* The Collar's modules from other programs, against a fake extension on the session bus. */
#include "collar.h"
#include "extensions.h"
#include "settings.h"

#include <QApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVirtualObject>
#include <QElapsedTimer>
#include <QImage>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>

static int failures;
static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	failures += !ok;
}

static bool until(const std::function<bool()> &done, int timeout = 5000) {
	QElapsedTimer clock;
	clock.start();
	while (!done() && clock.elapsed() < timeout) {
		QCoreApplication::processEvents();
		QThread::msleep(1);
	}
	return done();
}

static void settle(int duration = 80) {
	until([] { return false; }, duration);
}

static void key(QWidget &widget, int code) {
	QKeyEvent event(QEvent::KeyPress, code, Qt::NoModifier);
	QApplication::sendEvent(&widget, &event);
	settle();
}

struct MenuEntry {
	QString id, label;
	uint flags;
};
Q_DECLARE_METATYPE(MenuEntry)
QDBusArgument &operator<<(QDBusArgument &arg, const MenuEntry &e) {
	arg.beginStructure();
	arg << e.id << e.label << e.flags;
	arg.endStructure();
	return arg;
}
const QDBusArgument &operator>>(const QDBusArgument &arg, MenuEntry &e) {
	arg.beginStructure();
	arg >> e.id >> e.label >> e.flags;
	arg.endStructure();
	return arg;
}

static const QString Prefix = "org.zacos9.test.CollarExtension";
static const QString Service = Prefix + ".Lamps";

class FakeExtension : public QDBusVirtualObject {
public:
	QString lampLabel = "Lamp";
	bool on = true;
	QStringList calls;
	QDBusConnection *bus = nullptr;
	QString introspect(const QString &) const override { return {}; }

	static QByteArray solid(QRgb color) {
		QByteArray bytes;
		for (int i = 0; i < 256; ++i) {
			bytes += char(qAlpha(color));
			bytes += char(qRed(color));
			bytes += char(qGreen(color));
			bytes += char(qBlue(color));
		}
		return bytes;
	}

	bool handleMessage(const QDBusMessage &message, const QDBusConnection &bus) override {
		const QString member = message.member();
		if (message.interface() != CollarExtensions::Interface) {
			return false;
		}
		if (member == "GetModules") {
			const QList<MenuEntry> lamp = { { "on", "On", 1u | (on ? 2u : 0u) }, { "off", "Off", 1u | (on ? 0u : 2u) },
				{ "", "", 4 }, { "info", "Bridge 1.2.3", 0 }, { "fail", "Break", 1 } };
			const QList<QVariantMap> modules = {
				{ { "id", "lamp" }, { "label", lampLabel }, { "description", on ? "Lamp is on" : "Lamp is off" },
					{ "active", on }, { "icon", solid(qRgb(0xEE, 0x22, 0x22)) },
					{ "menu", QVariant::fromValue(lamp) } },
				{ { "id", "spare" }, { "label", "Spare" }, { "active", false } },
			};
			bus.send(message.createReply(QVariant::fromValue(modules)));
		} else if (member == "Activate") {
			const QStringList args = { message.arguments().value(0).toString(), message.arguments().value(1).toString() };
			calls << args.join(" ");
			if (args.value(1) == "fail") {
				bus.send(message.createErrorReply("org.zacos9.test.Error", "The lamp is broken."));
				return true;
			}
			on = args.value(1) == "on";
			bus.send(message.createReply());
			changed(bus);
		} else {
			bus.send(message.createErrorReply("org.freedesktop.DBus.Error.UnknownMethod", member));
		}
		return true;
	}

	void changed(const QDBusConnection &bus) {
		QDBusConnection(bus).send(QDBusMessage::createSignal(CollarExtensions::Path, CollarExtensions::Interface, "Changed"));
	}
};

int main(int argc, char **argv) {
	QTemporaryDir config;
	if (!config.isValid()) {
		return 1;
	}
	qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
	qputenv("XDG_RUNTIME_DIR", config.path().toUtf8());
	qputenv("XDG_DATA_HOME", (config.path() + "/data").toUtf8());
	qputenv("WAYLAND_DISPLAY", "test-collar-extensions");
	qputenv("ZACOS9_KDECONNECT_SERVICE", "org.zacos9.test.NoKdeConnect");
	qputenv("ZACOS9_SECRETS_SERVICE", "org.zacos9.test.NoSecrets");
	qputenv("ZACOS9_COLLAR_EXTENSION_PREFIX", Prefix.toUtf8());
	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);
	qDBusRegisterMetaType<MenuEntry>();
	qDBusRegisterMetaType<QList<MenuEntry>>();
	qDBusRegisterMetaType<QList<QVariantMap>>();
	pl_setting_set("sound-theme", "none");

	Collar collar(false);
	collar.show();
	CollarExtensions &ext = collar.extensions();
	QStringList alerts;
	ext.failed = [&](const QString &error) { alerts << error; };
	ext.refresh();
	settle(300);
	check(ext.modules.isEmpty() && collar.moduleIndex(Collar::Extension) < 0, "no extensions, no extension modules");

	FakeExtension fake;
	QDBusConnection daemon = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "fake-extension");
	check(daemon.registerVirtualObject(CollarExtensions::Path, &fake) && daemon.registerService(Service),
		"fake extension registered");
	check(until([&] { return ext.modules.size() == 2; }), "its modules appear as soon as it owns its name");
	const int index = collar.moduleIndex(Collar::Extension);
	check(index == 4 && collar.modules().value(5).kind == Collar::Extension &&
		collar.modules().value(6).kind == Collar::Resolution, "both modules, in order, after Bluetooth");
	check(collar.menuLabel(index) == "Lamp" && collar.menuLabel(index + 1) == "Spare", "menus labelled by the extension");

	collar.setVisibleModules(collar.modules().size());
	auto iconPixel = [&](int i) {
		const QImage image = collar.grab().toImage();
		return image.pixelColor(collar.moduleRect(i).left() + 2 + 8, 4 + 8);
	};
	check(iconPixel(index) == QColor(0xEE, 0x22, 0x22), "the extension's icon is drawn in its cell");

	collar.setFocus();
	auto choose = [&](int presses) {
		QKeyEvent left(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
		for (int i = 0; i < collar.modules().size(); ++i) {
			QApplication::sendEvent(&collar, &left);
		}
		QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
		for (int i = 0; i < index; ++i) {
			QApplication::sendEvent(&collar, &right);
		}
		key(collar, Qt::Key_Return);
		QWidget *menu = QApplication::activePopupWidget();
		if (!menu) {
			return false;
		}
		for (int i = 0; i < presses; ++i) {
			key(*menu, Qt::Key_Down);
		}
		key(*menu, Qt::Key_Return);
		return until([&] { return !fake.calls.isEmpty(); });
	};
	// The menu opens on the checked item (On); Off is next.
	check(choose(1) && fake.calls.value(0) == "lamp off", "choosing an item calls Activate(module, item)");
	check(until([&] { return ext.module(collar.modules()[index].device) &&
		!ext.module(collar.modules()[index].device)->active; }), "Changed makes The Collar reread the modules");
	fake.calls.clear();

	// Now Off is checked; the disabled Bridge line is skipped on the way to Break.
	check(choose(1) && fake.calls.value(0) == "lamp fail" && until([&] { return !alerts.isEmpty(); }) &&
		alerts.value(0) == "The lamp is broken.", "an Activate error is shown with its message");
	fake.calls.clear();

	fake.lampLabel = "Desk Lamp";
	fake.changed(daemon);
	check(until([&] { return collar.menuLabel(index) == "Desk Lamp"; }), "a new label follows the Changed signal");

	daemon.unregisterService(Service);
	check(until([&] { return ext.modules.isEmpty(); }) && collar.moduleIndex(Collar::Extension) < 0,
		"the modules go away with the extension");
	return failures ? 1 : 0;
}
