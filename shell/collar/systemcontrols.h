#pragma once

#include <QObject>
#include <QDBusObjectPath>
#include <QVariantMap>
#include <functional>

using CollarInterfaces = QMap<QString, QVariantMap>;
using CollarObjects = QMap<QDBusObjectPath, CollarInterfaces>;
Q_DECLARE_METATYPE(CollarInterfaces)
Q_DECLARE_METATYPE(CollarObjects)

struct CollarState {
	bool networkAvailable = false, wifi = false;
	uint networkState = 0;
	QString networkError = "Reading network status...";
	QString adapter, bluetoothError = "Reading Bluetooth status...";
	bool bluetooth = false;
	bool batteryPresent = false;
	double batteryPercent = 0;
	uint batteryState = 0;
	QString powerError = "Reading battery status...";
	QString canSuspend, sleepError;
};

class SystemControls : public QObject {
public:
	explicit SystemControls(QObject *parent = nullptr);
	CollarState state;
	bool busy = false;
	std::function<void()> changed;
	std::function<void(const QString &)> failed;

	void refresh();
	void setWifi(bool on);
	void setBluetooth(bool on);
	void suspend();
	bool updating() const { return busy || m_refreshing; }

private:
	using Done = std::function<void(const QVariantList &, const QString &)>;
	bool m_refreshing = false;
	int m_pending = 0;
	void call(const QString &service, const QString &path, const QString &interface,
		const QString &method, const QVariantList &args, Done done, int timeout = 5000);
	void properties(const QString &service, const QString &path, const QString &interface,
		std::function<void(const QVariantMap &, const QString &)> done);
	void refreshed();
	bool beginAction();
	void finishAction(const QString &error);
};
