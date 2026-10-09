#include "systemcontrols.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <algorithm>
#include <cmath>
#include <unistd.h>

static const QString NM = QStringLiteral("org.freedesktop.NetworkManager");
static const QString NM_PATH = QStringLiteral("/org/freedesktop/NetworkManager");
static const QString BLUEZ = QStringLiteral("org.bluez");
static const QString ADAPTER = QStringLiteral("org.bluez.Adapter1");
static const QString LOGIN = QStringLiteral("org.freedesktop.login1");
static const QString LOGIN_PATH = QStringLiteral("/org/freedesktop/login1");
static const QString MANAGER = QStringLiteral("org.freedesktop.login1.Manager");

SystemControls::SystemControls(QObject *parent, const QString &backlightRoot)
	: QObject(parent), m_backlightRoot(backlightRoot) {
	qDBusRegisterMetaType<CollarInterfaces>();
	qDBusRegisterMetaType<CollarObjects>();
}

void SystemControls::call(const QString &service, const QString &path, const QString &interface,
		const QString &method, const QVariantList &args, Done done, int timeout) {
	QDBusMessage message = QDBusMessage::createMethodCall(service, path, interface, method);
	message.setArguments(args);
	auto *watcher = new QDBusPendingCallWatcher(
		QDBusConnection::systemBus().asyncCall(message, timeout), this);
	connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, done] {
		const QDBusMessage reply = watcher->reply();
		watcher->deleteLater();
		done(reply.arguments(), reply.type() == QDBusMessage::ErrorMessage
			? reply.errorName() + ": " + reply.errorMessage() : QString());
	});
}

void SystemControls::properties(const QString &service, const QString &path,
		const QString &interface, std::function<void(const QVariantMap &, const QString &)> done) {
	call(service, path, "org.freedesktop.DBus.Properties", "GetAll", { interface },
		[done](const QVariantList &args, const QString &error) {
			done(error.isEmpty() ? qdbus_cast<QVariantMap>(args.value(0)) : QVariantMap(), error);
		});
}

void SystemControls::refreshed() {
	if (--m_pending != 0) {
		return;
	}
	m_refreshing = false;
	if (changed) {
		changed();
	}
}

void SystemControls::refresh() {
	if (m_refreshing || busy) {
		return;
	}
	m_refreshing = true;
	m_pending = 4;
	readBrightness();
	properties(NM, NM_PATH, NM, [this](const QVariantMap &p, const QString &error) {
		QString problem = error;
		bool ok = false;
		const uint networkState = p.value("State").toUInt(&ok);
		if (problem.isEmpty() && (!ok || networkState > 70 || networkState % 10 != 0 ||
				p.value("WirelessEnabled").metaType().id() != QMetaType::Bool)) {
			problem = "NetworkManager returned incomplete status.";
		}
		if (problem != state.networkError && !problem.isEmpty()) {
			qWarning().noquote() << "The Collar:" << problem;
		}
		state.networkError = problem;
		state.networkAvailable = problem.isEmpty();
		state.wifi = state.networkAvailable && p.value("WirelessEnabled").toBool();
		state.networkState = state.networkAvailable ? networkState : 0;
		refreshed();
	});
	call(BLUEZ, "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", {},
		[this](const QVariantList &args, const QString &error) {
			state.adapter.clear();
			state.bluetooth = false;
			QString problem = error;
			if (problem.isEmpty()) {
				const CollarObjects objects = qdbus_cast<CollarObjects>(args.value(0));
				for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
					if (it.value().contains(ADAPTER) &&
							it.value().value(ADAPTER).value("Powered").metaType().id() == QMetaType::Bool) {
						state.adapter = it.key().path();
						state.bluetooth = it.value().value(ADAPTER).value("Powered").toBool();
						break;
					}
				}
				if (state.adapter.isEmpty()) {
					problem = "No Bluetooth adapter is available.";
				}
			}
			if (problem != state.bluetoothError && !problem.isEmpty()) {
				qWarning().noquote() << "The Collar:" << problem;
			}
			state.bluetoothError = problem;
			refreshed();
		});
	properties("org.freedesktop.UPower", "/org/freedesktop/UPower/devices/DisplayDevice",
		"org.freedesktop.UPower.Device", [this](const QVariantMap &p, const QString &error) {
			QString problem = error;
			state.batteryPresent = false;
			state.batteryPercent = 0;
			state.batteryState = 0;
			if (problem.isEmpty()) {
				if (p.value("IsPresent").metaType().id() != QMetaType::Bool) {
					problem = "UPower returned incomplete battery status.";
				} else if (p.value("IsPresent").toBool()) {
					bool ok = false;
					const double percent = p.value("Percentage").toDouble(&ok);
					bool stateOk = false;
					const uint batteryState = p.value("State").toUInt(&stateOk);
					if (!ok || !std::isfinite(percent) || percent < 0 || percent > 100 ||
							!stateOk || batteryState > 6) {
						problem = "UPower returned invalid battery status.";
					} else {
						state.batteryPresent = true;
						state.batteryPercent = percent;
						state.batteryState = batteryState;
					}
				}
			}
			if (problem != state.powerError && !problem.isEmpty()) {
				qWarning().noquote() << "The Collar:" << problem;
			}
			state.powerError = problem;
			refreshed();
		});
	call(LOGIN, LOGIN_PATH, MANAGER, "CanSuspend", {},
		[this](const QVariantList &args, const QString &error) {
			state.canSuspend = error.isEmpty() ? args.value(0).toString() : QString();
			QString problem = error;
			if (problem.isEmpty() && state.canSuspend != "yes" && state.canSuspend != "no" &&
					state.canSuspend != "na" && state.canSuspend != "challenge") {
				problem = "The login service returned invalid sleep capability.";
				state.canSuspend.clear();
			}
			if (problem != state.sleepError && !problem.isEmpty()) {
				qWarning().noquote() << "The Collar: couldn't read sleep capability:" << problem;
			}
			state.sleepError = problem;
			refreshed();
		});
}

static bool readNumber(const QString &path, int &number) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return false;
	}
	bool ok = false;
	number = QString::fromUtf8(file.readAll()).trimmed().toInt(&ok);
	return ok;
}

void SystemControls::readBrightness() {
	state.backlight.clear();
	state.brightness = -1;
	state.maxBrightness = 0;
	QString problem = "No adjustable display backlight is available.";
	const QDir dir(m_backlightRoot);
	for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
		int maximum = 0, value = 0;
		const QString path = dir.filePath(name);
		if (!readNumber(path + "/max_brightness", maximum) || maximum <= 0 ||
				!readNumber(path + "/brightness", value) || value < 0 || value > maximum) {
			problem = "Couldn't read display backlight " + name + ".";
			continue;
		}
		state.backlight = name;
		state.maxBrightness = maximum;
		state.brightness = qRound(100.0 * value / maximum);
		problem.clear();
		break;
	}
	if (problem != state.brightnessError && !problem.isEmpty()) {
		qWarning().noquote() << "The Collar:" << problem;
	}
	state.brightnessError = problem;
}

bool SystemControls::beginAction() {
	if (busy || m_refreshing) {
		const QString error = "The Collar is refreshing or applying a setting. Please try again.";
		qWarning().noquote() << error;
		if (failed) {
			failed(error);
		}
		return false;
	}
	busy = true;
	if (changed) {
		changed();
	}
	return true;
}

void SystemControls::finishAction(const QString &error) {
	busy = false;
	if (!error.isEmpty()) {
		qWarning().noquote() << "The Collar:" << error;
		if (failed) {
			failed(error);
		}
	}
	refresh();
}

void SystemControls::setWifi(bool on) {
	if (!beginAction()) {
		return;
	}
	if (!state.networkAvailable) {
		finishAction("Network controls are unavailable. " + state.networkError);
		return;
	}
	call(NM, NM_PATH, "org.freedesktop.DBus.Properties", "Set",
		{ NM, "WirelessEnabled", QVariant::fromValue(QDBusVariant(on)) },
		[this](const QVariantList &, const QString &error) { finishAction(error); });
}

void SystemControls::setBluetooth(bool on) {
	if (!beginAction()) {
		return;
	}
	if (state.adapter.isEmpty()) {
		finishAction("Bluetooth controls are unavailable. " + state.bluetoothError);
		return;
	}
	call(BLUEZ, state.adapter, "org.freedesktop.DBus.Properties", "Set",
		{ ADAPTER, "Powered", QVariant::fromValue(QDBusVariant(on)) },
		[this](const QVariantList &, const QString &error) { finishAction(error); });
}

void SystemControls::setBrightness(int percent) {
	if (!beginAction()) {
		return;
	}
	readBrightness();
	if (percent < 0 || percent > 100 || state.backlight.isEmpty()) {
		finishAction("No valid display backlight or brightness was selected. " + state.brightnessError);
		return;
	}
	const QString device = state.backlight;
	const uint value = static_cast<uint>(std::max(1, qRound(state.maxBrightness * (percent / 100.0))));
	call(LOGIN, LOGIN_PATH, MANAGER, "GetSessionByPID",
		{ QVariant::fromValue(static_cast<uint>(getpid())) },
		[this, device, value](const QVariantList &args, const QString &error) {
			const QString path = qdbus_cast<QDBusObjectPath>(args.value(0)).path();
			if (!error.isEmpty() || path.isEmpty()) {
				finishAction("Couldn't find the active login session. " + error);
				return;
			}
			call(LOGIN, path, "org.freedesktop.login1.Session", "SetBrightness",
				{ "backlight", device, QVariant::fromValue(value) },
				[this](const QVariantList &, const QString &error) { finishAction(error); });
		});
}

void SystemControls::suspend() {
	if (!beginAction()) {
		return;
	}
	if (state.canSuspend != "yes" && state.canSuspend != "challenge") {
		finishAction("Sleep is not available for this session.");
		return;
	}
	call(LOGIN, LOGIN_PATH, MANAGER, "Suspend", { true },
		[this](const QVariantList &, const QString &error) { finishAction(error); }, 60000);
}
