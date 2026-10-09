/* The Collar's KDE Connect modules against a fake kdeconnectd on the session bus. */
#include "collar.h"
#include "kdeconnect.h"
#include "settings.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QElapsedTimer>
#include <QFile>
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

static const QString Service = "org.zacos9.test.KdeConnect";

struct FakeDevice {
	QString id, name;
	QStringList plugins;
	int charge = -1;
	bool charging = false;
};

class FakeKdeConnect : public QDBusVirtualObject {
public:
	QList<FakeDevice> devices;
	QStringList pairing;
	QMap<QString, QString> names;
	QStringList calls;
	QString introspect(const QString &) const override { return {}; }

	bool handleMessage(const QDBusMessage &message, const QDBusConnection &bus) override {
		const QString path = message.path(), member = message.member();
		const QVariantList args = message.arguments();
		const QStringList parts = path.split('/', Qt::SkipEmptyParts);
		const QString id = parts.value(3), plugin = parts.value(4);
		auto variant = [](const QVariant &value) { return QVariant::fromValue(QDBusVariant(value)); };
		if (path == "/modules/kdeconnect" && member == "devices") {
			QStringList ids;
			for (const FakeDevice &d : devices) {
				ids << d.id;
			}
			bus.send(message.createReply(ids));
		} else if (path == "/modules/kdeconnect" && member == "Get") {
			bus.send(message.createReply(variant(pairing)));
		} else if (plugin.isEmpty() && member == "GetAll") {
			const FakeDevice *d = find(id);
			bus.send(message.createReply(QVariant(QVariantMap{
				{ "name", d ? d->name : names.value(id) }, { "type", "smartphone" } })));
		} else if (plugin.isEmpty() && member == "Get") {
			bus.send(message.createReply(variant(names.value(id))));
		} else if (plugin.isEmpty() && member == "loadedPlugins") {
			const FakeDevice *d = find(id);
			bus.send(message.createReply(d ? d->plugins : QStringList()));
		} else if (plugin == "battery" && member == "GetAll") {
			const FakeDevice *d = find(id);
			bus.send(message.createReply(QVariant(QVariantMap{
				{ "charge", d ? d->charge : -1 }, { "isCharging", d && d->charging } })));
		} else if (plugin == "connectivity_report" && member == "GetAll") {
			bus.send(message.createReply(QVariant(QVariantMap{
				{ "cellularNetworkType", "5G" }, { "cellularNetworkStrength", 3 } })));
		} else if (plugin == "remotecommands" && member == "Get") {
			bus.send(message.createReply(variant(QByteArray(
				R"({"zz":{"name":"Lock Screen","command":"loginctl lock-session"},)"
				R"("aa":{"name":"Suspend","command":"systemctl suspend"}})"))));
		} else {
			QStringList printed;
			for (const QVariant &arg : args) {
				printed << (arg.typeId() == QMetaType::QStringList ? arg.toStringList().join(",") : arg.toString());
			}
			calls << QString("%1 %2 %3").arg(id, member, printed.join(" ")).trimmed();
			bus.send(member == "startBrowsing" ? message.createReply(true) : message.createReply());
		}
		return true;
	}

private:
	const FakeDevice *find(const QString &id) const {
		for (const FakeDevice &d : devices) {
			if (d.id == id) {
				return &d;
			}
		}
		return nullptr;
	}
};

static void emitSignal(QDBusConnection &bus, const QString &interface, const QString &name, const QString &path) {
	bus.send(QDBusMessage::createSignal(path, interface, name));
}

int main(int argc, char **argv) {
	QTemporaryDir config;
	if (!config.isValid()) {
		return 1;
	}
	qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
	qputenv("XDG_RUNTIME_DIR", config.path().toUtf8());
	qputenv("XDG_DATA_HOME", (config.path() + "/data").toUtf8());
	qputenv("WAYLAND_DISPLAY", "test-collar-kdeconnect");
	qputenv("ZACOS9_KDECONNECT_SERVICE", Service.toUtf8());
	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);
	pl_setting_set("sound-theme", "none");

	Collar collar(false);
	collar.show();
	KdeConnect &kde = collar.kdeConnect();
	bool changed = false;
	auto previous = kde.changed;
	kde.changed = [&] { changed = true; previous(); };
	kde.refresh();
	settle(300);
	check(!kde.installed && collar.moduleIndex(Collar::Phone) < 0, "no KDE Connect, no phone module");

	FakeKdeConnect fake;
	fake.devices = {
		{ "pixel", "Pixel", { "kdeconnect_battery", "kdeconnect_connectivity_report", "kdeconnect_sftp",
			"kdeconnect_clipboard", "kdeconnect_findmyphone", "kdeconnect_share", "kdeconnect_ping",
			"kdeconnect_remotecommands" }, 80, true },
		{ "aaa_tablet", "Galaxy Tab", { "kdeconnect_battery", "kdeconnect_ping" }, 10, false },
	};
	QDBusConnection daemon = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "fake-kdeconnect");
	check(daemon.registerVirtualObject("/modules/kdeconnect", &fake, QDBusConnection::SubPath) &&
		daemon.registerService(Service), "fake kdeconnectd registered");
	check(until([&] { return kde.installed && kde.devices.size() == 2; }),
		"KDE Connect noticed as soon as its daemon appears");
	const int first = collar.moduleIndex(Collar::Phone);
	check(first == 4 && collar.modules().value(5).kind == Collar::Phone &&
		collar.modules().value(4).device == "aaa_tablet" && collar.modules().value(5).device == "pixel" &&
		collar.modules().value(6).kind == Collar::Resolution,
		"one module per connected device, by name, after Bluetooth");
	check(collar.menuLabel(4) == "Galaxy Tab" && collar.menuLabel(5) == "Pixel",
		"each device's menu is labelled with its name");
	const PhoneDevice *pixel = kde.device("pixel");
	check(pixel && pixel->charge == 80 && pixel->charging && pixel->network == "5G" && pixel->signal == 3 &&
		pixel->commands.size() == 2 && pixel->commands.first().second == "Lock Screen",
		"battery, signal and remote commands read from the device's plugins");

	QImage image = collar.grab().toImage();
	const QRect cell = collar.moduleRect(5);
	// The phone's screen (x 6-9, y 3-10 of its 16 px icon) fills from the bottom with its charge.
	const int ix = cell.left() + 2, iy = 4;
	check(image.pixelColor(ix + 7, iy + 10) == QColor(0x669955) && image.pixelColor(ix + 7, iy + 5) == QColor(0x669955) &&
		image.pixelColor(ix + 7, iy + 4) == QColor(Qt::white), "Pixel's icon shows 80% charge (6 of 8 rows)");
	const QRect tabletCell = collar.moduleRect(4);
	check(image.pixelColor(tabletCell.left() + 9, iy + 10) == QColor(0xAA2222),
		"a nearly empty battery shows red");

	// Pixel's menu: Browse, Send Clipboard, Ring, Send Files, Send Ping, then the commands (Lock Screen, Suspend).
	collar.setFocus();
	auto choose = [&](int index, int downs) {
		collar.setVisibleModules(collar.modules().size());
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
		for (int i = 0; i < downs; ++i) {
			key(*menu, Qt::Key_Down);
		}
		key(*menu, Qt::Key_Return);
		return until([&] { return !fake.calls.isEmpty(); });
	};
	check(choose(5, 3) && fake.calls.value(0) == "pixel ring", "Ring Device rings the phone");
	fake.calls.clear();
	check(choose(5, 6) && fake.calls.value(0) == "pixel triggerCommand zz", "a remote command runs by its key");
	fake.calls.clear();
	check(choose(5, 1) && fake.calls.value(0) == "pixel startBrowsing", "Browse Device opens the phone's files");
	fake.calls.clear();
	check(choose(4, 1) && fake.calls.value(0) == "aaa_tablet sendPing",
		"a device without the other plugins only offers what it supports");
	fake.calls.clear();

	fake.names.insert("newphone", "New Phone");
	fake.pairing = { "newphone" };
	emitSignal(daemon, "org.kde.kdeconnect.daemon", "pairingRequestsChanged", "/modules/kdeconnect");
	check(until([&] { return kde.requests.size() == 1 && kde.requests.first().name == "New Phone"; }),
		"pairing requests follow the daemon's signal");
	// Galaxy Tab's menu: Send Ping, then Pair with New Phone.
	check(choose(4, 2) && fake.calls.value(0) == "newphone acceptPairing", "a pairing request can be accepted");
	fake.calls.clear();

	fake.devices.clear();
	fake.pairing.clear();
	emitSignal(daemon, "org.kde.kdeconnect.daemon", "deviceListChanged", "/modules/kdeconnect");
	check(until([&] { return kde.devices.isEmpty(); }) && collar.moduleIndex(Collar::Phone) == 4 &&
		collar.modules().value(4).device.isEmpty() && collar.menuLabel(4) == "KDE Connect" &&
		collar.modules().value(5).kind == Collar::Resolution,
		"with no device in reach, one KDE Connect module remains");

	daemon.unregisterService(Service);
	check(until([&] { return !kde.installed; }) && collar.moduleIndex(Collar::Phone) < 0,
		"the module goes away with KDE Connect");

	// The Collar control panel's settings.
	pl_setting_set("collar-visibility", "hide");
	collar.applySettings();
	check(!collar.isVisible(), "Hide Collar hides it");
	pl_setting_set("collar-visibility", "hotkey");
	collar.applySettings();
	check(collar.isVisible(), "hot-key mode starts shown");
	collar.toggle();
	check(!collar.isVisible() && collar.hiddenByHotKey(), "the hot key hides it");
	{
		Collar saved(false);
		check(saved.hiddenByHotKey(), "hidden by the hot key persists");
	}
	collar.toggle();
	check(collar.isVisible(), "and shows it again");
	pl_setting_set("collar-visibility", "show");
	collar.applySettings();
	collar.toggle();
	check(collar.isVisible(), "the hot key does nothing unless the panel chose it");
	check(Collar::menuFont() == PL_FONT_SYSTEM, "module menus use the system font by default");
	pl_setting_set("collar-menu-font", "views");
	check(Collar::menuFont() == PL_FONT_VIEWS, "the panel can choose the views font for module menus");
	return failures ? 1 : 0;
}
