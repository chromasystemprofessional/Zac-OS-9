#include "kdeconnect.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <algorithm>

static const QString Root = "/modules/kdeconnect";
static const QString DaemonInterface = "org.kde.kdeconnect.daemon";
static const QString DeviceInterface = "org.kde.kdeconnect.device";
static const QString Properties = "org.freedesktop.DBus.Properties";

static QString devicePath(const QString &id) {
	return Root + "/devices/" + id;
}

static QVariant unwrap(const QVariant &value) {
	return value.canConvert<QDBusVariant>() ? value.value<QDBusVariant>().variant() : value;
}

static QVariantMap propertyMap(const QDBusMessage &reply) {
	if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
		return {};
	}
	return qdbus_cast<QVariantMap>(reply.arguments().first());
}

/* One refresh in flight: the devices found so far and the calls still out. */
struct KdeConnect::Scan {
	int generation = 0, pending = 0;
	QList<PhoneDevice> devices, requests;
};

QString KdeConnect::defaultService() {
	const QByteArray service = qgetenv("ZACOS9_KDECONNECT_SERVICE");
	return service.isEmpty() ? QString("org.kde.kdeconnect") : QString::fromUtf8(service);
}

KdeConnect::KdeConnect(QObject *parent, const QString &service)
	: QObject(parent), m_service(service), m_debounce(new QTimer(this)) {
	m_debounce->setSingleShot(true);
	m_debounce->setInterval(150);
	connect(m_debounce, &QTimer::timeout, this, [this] { refresh(); });
	auto bus = QDBusConnection::sessionBus();
	// Any change kdeconnectd announces just schedules a fresh scan; an empty path matches every device.
	const QList<QPair<QString, QString>> watched = {
		{ DaemonInterface, "deviceAdded" }, { DaemonInterface, "deviceRemoved" },
		{ DaemonInterface, "deviceVisibilityChanged" }, { DaemonInterface, "deviceListChanged" },
		{ DaemonInterface, "pairingRequestsChanged" }, { DeviceInterface, "pluginsChanged" },
		{ DeviceInterface, "reachableChanged" }, { DeviceInterface, "pairStateChanged" },
		{ DeviceInterface, "nameChanged" }, { DeviceInterface, "typeChanged" },
		{ DeviceInterface + ".battery", "refreshed" },
		{ DeviceInterface + ".connectivity_report", "refreshed" },
		{ DeviceInterface + ".remotecommands", "commandsChanged" },
	};
	for (const auto &[interface, name] : watched) {
		bus.connect(m_service, QString(), interface, name, m_debounce, SLOT(start()));
	}
	auto *watcher = new QDBusServiceWatcher(m_service, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
	connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, m_debounce, [this] { m_debounce->start(); });
}

void KdeConnect::call(const QString &path, const QString &interface, const QString &method,
		const QVariantList &args, std::function<void(const QDBusMessage &)> done) {
	QDBusMessage message = QDBusMessage::createMethodCall(m_service, path, interface, method);
	message.setArguments(args);
	auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 10000), this);
	connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, done = std::move(done)] {
		watcher->deleteLater();
		if (done) {
			done(watcher->reply());
		}
	});
}

void KdeConnect::refresh() {
	if (m_refreshing) {
		m_again = true;
		return;
	}
	m_refreshing = true;
	auto scan = std::make_shared<Scan>();
	scan->generation = ++m_generation;
	auto bus = QDBusConnection::sessionBus();
	QDBusMessage owner = QDBusMessage::createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus",
		"org.freedesktop.DBus", "NameHasOwner");
	owner << m_service;
	auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(owner, 5000), this);
	connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, scan] {
		watcher->deleteLater();
		const QDBusMessage reply = watcher->reply();
		if (reply.type() == QDBusMessage::ReplyMessage && reply.arguments().value(0).toBool()) {
			installed = true;
			scanDevices(scan);
			return;
		}
		// Not running: installed if the bus can start it (kdeconnectd's D-Bus service file).
		const QDBusMessage list = QDBusMessage::createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus",
			"org.freedesktop.DBus", "ListActivatableNames");
		auto *names = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(list, 5000), this);
		connect(names, &QDBusPendingCallWatcher::finished, this, [this, names, scan] {
			names->deleteLater();
			const QDBusMessage reply = names->reply();
			installed = reply.type() == QDBusMessage::ReplyMessage &&
				reply.arguments().value(0).toStringList().contains(m_service);
			if (installed) {
				scanDevices(scan);
			} else {
				finish(scan);
			}
		});
	});
}

void KdeConnect::scanDevices(const std::shared_ptr<Scan> &scan) {
	scan->pending += 2;
	call(Root, DaemonInterface, "devices", { true, true }, [this, scan](const QDBusMessage &reply) {
		for (const QString &id : reply.arguments().value(0).toStringList()) {
			const int index = scan->devices.size();
			PhoneDevice found;
			found.id = id;
			scan->devices.append(found);
			const QString path = devicePath(id);
			scan->pending += 2;
			call(path, Properties, "GetAll", { DeviceInterface }, [this, scan, index](const QDBusMessage &reply) {
				const QVariantMap props = propertyMap(reply);
				PhoneDevice &d = scan->devices[index];
				d.name = props.value("name").toString();
				d.type = props.value("type").toString();
				finish(scan);
			});
			call(path, DeviceInterface, "loadedPlugins", {}, [this, scan, index, path](const QDBusMessage &reply) {
				PhoneDevice &d = scan->devices[index];
				d.plugins = reply.arguments().value(0).toStringList();
				if (d.has("battery")) {
					++scan->pending;
					call(path + "/battery", Properties, "GetAll", { DeviceInterface + ".battery" },
						[this, scan, index](const QDBusMessage &reply) {
							const QVariantMap props = propertyMap(reply);
							scan->devices[index].charge = props.value("charge", -1).toInt();
							scan->devices[index].charging = props.value("isCharging").toBool();
							finish(scan);
						});
				}
				if (d.has("connectivity_report")) {
					++scan->pending;
					call(path + "/connectivity_report", Properties, "GetAll",
						{ DeviceInterface + ".connectivity_report" }, [this, scan, index](const QDBusMessage &reply) {
							const QVariantMap props = propertyMap(reply);
							scan->devices[index].network = props.value("cellularNetworkType").toString();
							scan->devices[index].signal = props.value("cellularNetworkStrength", -1).toInt();
							finish(scan);
						});
				}
				if (d.has("remotecommands")) {
					++scan->pending;
					call(path + "/remotecommands", Properties, "Get", { DeviceInterface + ".remotecommands", "commands" },
						[this, scan, index](const QDBusMessage &reply) {
							const QJsonObject commands =
								QJsonDocument::fromJson(unwrap(reply.arguments().value(0)).toByteArray()).object();
							auto &list = scan->devices[index].commands;
							for (auto it = commands.begin(); it != commands.end(); ++it) {
								list.append({ it.key(), it.value().toObject().value("name").toString() });
							}
							std::sort(list.begin(), list.end(),
								[](const auto &a, const auto &b) { return a.second.localeAwareCompare(b.second) < 0; });
							finish(scan);
						});
				}
				finish(scan);
			});
		}
		if (reply.type() == QDBusMessage::ErrorMessage) {
			qWarning("KDE Connect didn't list its devices: %s", qPrintable(reply.errorMessage()));
		}
		finish(scan);
	});
	call(Root, Properties, "Get", { DaemonInterface, "pairingRequests" }, [this, scan](const QDBusMessage &reply) {
		for (const QString &id : unwrap(reply.arguments().value(0)).toStringList()) {
			const int index = scan->requests.size();
			PhoneDevice found;
			found.id = id;
			scan->requests.append(found);
			++scan->pending;
			call(devicePath(id), Properties, "Get", { DeviceInterface, "name" }, [this, scan, index](const QDBusMessage &reply) {
				scan->requests[index].name = unwrap(reply.arguments().value(0)).toString();
				finish(scan);
			});
		}
		finish(scan);
	});
}

void KdeConnect::finish(const std::shared_ptr<Scan> &scan) {
	if (scan->pending > 0 && --scan->pending > 0) {
		return;
	}
	if (scan->generation != m_generation) {
		return;
	}
	m_refreshing = false;
	for (PhoneDevice &d : scan->devices) {
		if (d.name.isEmpty()) {
			d.name = "Device";
		}
	}
	for (PhoneDevice &d : scan->requests) {
		if (d.name.isEmpty()) {
			d.name = "A device";
		}
	}
	auto byName = [](const PhoneDevice &a, const PhoneDevice &b) {
		const int order = a.name.localeAwareCompare(b.name);
		return order ? order < 0 : a.id < b.id;
	};
	std::sort(scan->devices.begin(), scan->devices.end(), byName);
	std::sort(scan->requests.begin(), scan->requests.end(), byName);
	const bool same = scan->devices == devices && scan->requests == requests && installed == m_reported;
	m_reported = installed;
	devices = scan->devices;
	requests = scan->requests;
	if (m_again) {
		m_again = false;
		refresh();
	}
	if (!same && changed) {
		changed();
	}
}

const PhoneDevice *KdeConnect::device(const QString &id) const {
	for (const PhoneDevice &d : devices) {
		if (d.id == id) {
			return &d;
		}
	}
	return nullptr;
}

void KdeConnect::action(const QString &id, const QString &plugin, const QString &method, const QVariantList &args,
		const QString &what) {
	const QString path = plugin.isEmpty() ? devicePath(id) : devicePath(id) + "/" + plugin;
	const QString interface = plugin.isEmpty() ? DeviceInterface : DeviceInterface + "." + plugin;
	call(path, interface, method, args, [this, what](const QDBusMessage &reply) {
		if (reply.type() == QDBusMessage::ErrorMessage) {
			if (failed) {
				failed(QString("KDE Connect couldn't %1. %2").arg(what, reply.errorMessage()));
			}
		} else if (reply.arguments().size() == 1 && reply.arguments().first().typeId() == QMetaType::Bool &&
				!reply.arguments().first().toBool() && failed) {
			failed(QString("KDE Connect couldn't %1.").arg(what));
		}
		m_debounce->start();
	});
}

void KdeConnect::browse(const QString &id) { action(id, "sftp", "startBrowsing", {}, "browse the device"); }
void KdeConnect::sendClipboard(const QString &id) { action(id, "clipboard", "sendClipboard", {}, "send the clipboard"); }
void KdeConnect::ring(const QString &id) { action(id, "findmyphone", "ring", {}, "ring the device"); }
void KdeConnect::ping(const QString &id) { action(id, "ping", "sendPing", {}, "send a ping"); }
void KdeConnect::shareFiles(const QString &id, const QStringList &urls) {
	action(id, "share", "shareUrls", { urls }, "send the files");
}
void KdeConnect::runCommand(const QString &id, const QString &key) {
	action(id, "remotecommands", "triggerCommand", { key }, "run the command");
}
void KdeConnect::editCommands(const QString &id) {
	action(id, "remotecommands", "editCommands", {}, "open the command editor");
}
void KdeConnect::acceptPairing(const QString &id) { action(id, {}, "acceptPairing", {}, "pair with the device"); }
void KdeConnect::rejectPairing(const QString &id) { action(id, {}, "cancelPairing", {}, "reject the pairing request"); }
