#include "systemcontrols.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>

static int failures;
static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	failures += !ok;
}

static bool until(const std::function<bool()> &done) {
	QElapsedTimer clock;
	clock.start();
	while (!done() && clock.elapsed() < 5000) {
		QCoreApplication::processEvents();
		QThread::msleep(1);
	}
	return done();
}

static bool write(const QString &path, const QByteArray &data) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

class Services : public QDBusVirtualObject {
public:
	QString brightnessPath, rejection, capability = "yes";
	bool wifi = true, bluetooth = false, battery = true, adapter = true, malformed = false;
	double percentage = 64.0;
	uint batteryState = 2;
	int sets = 0, sleeps = 0;
	QString lastPath, lastProperty;
	QVariant lastValue;

	QString introspect(const QString &) const override { return {}; }
	bool handleMessage(const QDBusMessage &message, const QDBusConnection &bus) override {
		const auto args = message.arguments();
		if (message.member() == "GetAll") {
			QVariantMap properties;
			if (message.path() == "/org/freedesktop/NetworkManager") {
				if (!malformed) {
					properties = { { "State", uint(70) }, { "WirelessEnabled", wifi } };
				}
			} else {
				properties = { { "IsPresent", battery }, { "Percentage", percentage }, { "State", batteryState } };
			}
			bus.send(message.createReply(QVariantList{ properties }));
		} else if (message.member() == "GetManagedObjects") {
			CollarObjects objects;
			if (adapter) {
				objects.insert(QDBusObjectPath("/org/bluez/hci0"),
					{ { "org.bluez.Adapter1", { { "Powered", bluetooth } } } });
			}
			bus.send(message.createReply(QVariantList{ QVariant::fromValue(objects) }));
		} else if (message.member() == "CanSuspend") {
			bus.send(message.createReply(QVariantList{ capability }));
		} else if (message.member() == "GetSessionByPID") {
			check(args.size() == 1 && args[0].toUInt() > 0, "look up our login session by process ID");
			bus.send(message.createReply(QVariantList{ QVariant::fromValue(QDBusObjectPath("/org/freedesktop/login1/session/test")) }));
		} else if (!rejection.isEmpty()) {
			bus.send(message.createErrorReply("org.freedesktop.DBus.Error.AccessDenied", rejection));
		} else if (message.member() == "Set") {
			++sets;
			lastPath = message.path();
			lastProperty = args.value(1).toString();
			lastValue = qvariant_cast<QDBusVariant>(args.value(2)).variant();
			if (lastProperty == "WirelessEnabled") {
				wifi = lastValue.toBool();
			} else if (lastProperty == "Powered") {
				bluetooth = lastValue.toBool();
			}
			bus.send(message.createReply());
		} else if (message.member() == "SetBrightness") {
			check(message.path() == "/org/freedesktop/login1/session/test", "use the caller's login session for brightness");
			check(args.size() == 3 && args[0] == "backlight" && args[1] == "display0",
				"route brightness to the backlight subsystem and selected device");
			check(write(brightnessPath, QByteArray::number(args.value(2).toUInt())), "fixture applies brightness");
			bus.send(message.createReply());
		} else if (message.member() == "Suspend") {
			++sleeps;
			check(args.size() == 1 && args[0].toBool(), "sleep permits normal authorization");
			bus.send(message.createReply());
		} else {
			return false;
		}
		return true;
	}
};

int main(int argc, char **argv) {
	qputenv("DBUS_SYSTEM_BUS_ADDRESS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));
	QCoreApplication app(argc, argv);
	SystemControls controls;
	auto bus = QDBusConnection::systemBus();
	Services services;
	check(bus.isConnected(), "connect only to the isolated test bus");
	check(bus.registerVirtualObject("/", &services, QDBusConnection::SubPath), "register fake system services");
	for (const QString service : { "org.freedesktop.NetworkManager", "org.bluez",
			"org.freedesktop.UPower", "org.freedesktop.login1" }) {
		check(bus.registerService(service), "own test service " + service);
	}
	QTemporaryDir dir;
	check(dir.isValid(), "create isolated backlight fixture");
	const QString display = dir.path() + "/display0";
	check(QDir().mkpath(display), "create backlight device");
	check(write(display + "/max_brightness", "200\n") && write(display + "/brightness", "100\n"),
		"seed brightness range");
	services.brightnessPath = display + "/brightness";
	SystemControls client(nullptr, dir.path());
	QString error;
	int changes = 0;
	client.changed = [&] { ++changes; };
	client.failed = [&](const QString &message) { error = message; };
	client.refresh();
	check(client.updating(), "refresh is asynchronous");
	check(until([&] { return !client.updating(); }), "finish all four service queries");
	check(changes == 1 && client.state.networkAvailable && client.state.wifi && client.state.networkState == 70,
		"read connected network and Wi-Fi status");
	check(client.state.adapter == "/org/bluez/hci0" && !client.state.bluetooth, "discover the BlueZ adapter");
	check(client.state.brightness == 50 && client.state.maxBrightness == 200, "read and scale the backlight");
	check(client.state.batteryPresent && client.state.batteryPercent == 64 && client.state.batteryState == 2,
		"read real battery percentage and discharge state");
	check(client.state.canSuspend == "yes", "read sleep capability");

	client.setWifi(false);
	check(client.busy && until([&] { return !client.updating(); }), "apply Wi-Fi without blocking");
	check(error.isEmpty() && !client.state.wifi && services.lastPath == "/org/freedesktop/NetworkManager" &&
		services.lastProperty == "WirelessEnabled" && services.lastValue.metaType().id() == QMetaType::Bool,
		"send a typed Wi-Fi property and reread status");
	client.setBluetooth(true);
	check(until([&] { return !client.updating(); }) && client.state.bluetooth &&
		services.lastPath == "/org/bluez/hci0" && services.lastProperty == "Powered",
		"power Bluetooth through BlueZ and verify status");
	client.setBrightness(75);
	check(until([&] { return !client.updating(); }) && client.state.brightness == 75,
		"set 75 percent using logind, not a direct sysfs write");
	client.setBrightness(0);
	check(until([&] { return !client.updating(); }) && client.state.brightness == 1,
		"retain at least one hardware brightness step");
	client.suspend();
	check(until([&] { return !client.updating(); }) && services.sleeps == 1, "request sleep on the fake bus only");

	services.rejection = "Permission refused by test service";
	client.setWifi(true);
	check(until([&] { return !client.updating(); }) && error.contains(services.rejection) && !client.state.wifi,
		"surface authorization failure; do not pretend the setting changed");
	services.rejection.clear();
	error.clear();
	services.adapter = false;
	services.battery = false;
	services.capability = "no";
	client.refresh();
	check(until([&] { return !client.updating(); }) && client.state.adapter.isEmpty() &&
		!client.state.bluetooth && !client.state.bluetoothError.isEmpty(), "clear a removed Bluetooth adapter");
	check(!client.state.batteryPresent && client.state.powerError.isEmpty(), "AC-only desktops have no fake battery");
	client.suspend();
	check(!error.isEmpty() && until([&] { return !client.updating(); }) && services.sleeps == 1,
		"refuse unsupported sleep explicitly");
	error.clear();
	const int sets = services.sets;
	client.setBluetooth(true);
	check(!error.isEmpty() && until([&] { return !client.updating(); }) && services.sets == sets,
		"reject a removed adapter without sending an invalid command");

	services.malformed = true;
	services.battery = true;
	services.percentage = 101;
	services.capability = "invalid";
	client.refresh();
	check(until([&] { return !client.updating(); }) && !client.state.networkAvailable &&
		!client.state.networkError.isEmpty(), "report incomplete network data");
	check(!client.state.batteryPresent && !client.state.powerError.isEmpty(), "reject an impossible battery percentage");
	check(client.state.canSuspend.isEmpty() && !client.state.sleepError.isEmpty(), "reject an invalid sleep response");
	services.percentage = 64;
	services.batteryState = 9;
	client.refresh();
	check(until([&] { return !client.updating(); }) && !client.state.batteryPresent &&
		!client.state.powerError.isEmpty(), "reject an invalid battery state");
	check(write(display + "/brightness", "oops"), "simulate unreadable backlight data");
	client.readBrightness();
	check(client.state.brightness == -1 && client.state.backlight.isEmpty() &&
		!client.state.brightnessError.isEmpty(), "clear invalid backlight state explicitly");
	SystemControls noBacklight(nullptr, dir.path() + "/missing");
	noBacklight.readBrightness();
	check(noBacklight.state.brightness == -1 && !noBacklight.state.brightnessError.isEmpty(),
		"report unavailable brightness on external monitors");
	bus.unregisterService("org.freedesktop.NetworkManager");
	client.refresh();
	check(until([&] { return !client.updating(); }) && !client.state.networkAvailable &&
		client.state.networkError.contains("ServiceUnknown"), "missing services are unavailable, not success");
	return failures ? 1 : 0;
}
