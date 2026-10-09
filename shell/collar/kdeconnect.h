#pragma once

/*
 * KDE Connect, when it is installed: its paired phones and tablets in reach,
 * their battery and signal, the actions its indicator offers, and devices
 * asking to pair. Everything goes through kdeconnectd's session-bus API
 * (org.kde.kdeconnect), which starts the daemon on first use.
 */

#include <QDBusMessage>
#include <QList>
#include <QObject>
#include <QPair>
#include <QStringList>
#include <QVariantList>
#include <functional>
#include <memory>

class QTimer;

struct PhoneDevice {
	QString id, name, type;
	QStringList plugins;
	int charge = -1;
	bool charging = false;
	QString network; /* cellular network type, e.g. "5G" */
	int signal = -1; /* 0-4 */
	QList<QPair<QString, QString>> commands; /* key, name */
	bool has(const QString &plugin) const { return plugins.contains("kdeconnect_" + plugin); }
	bool operator==(const PhoneDevice &o) const {
		return id == o.id && name == o.name && type == o.type && plugins == o.plugins && charge == o.charge &&
			charging == o.charging && network == o.network && signal == o.signal && commands == o.commands;
	}
};

class KdeConnect : public QObject {
public:
	explicit KdeConnect(QObject *parent = nullptr, const QString &service = defaultService());
	/* $ZACOS9_KDECONNECT_SERVICE (for tests), else org.kde.kdeconnect. */
	static QString defaultService();
	bool installed = false;
	QList<PhoneDevice> devices;  /* paired and reachable */
	QList<PhoneDevice> requests; /* asking to pair: id and name */
	std::function<void()> changed;
	std::function<void(const QString &)> failed;

	void refresh();
	const PhoneDevice *device(const QString &id) const;
	void browse(const QString &id);
	void sendClipboard(const QString &id);
	void ring(const QString &id);
	void ping(const QString &id);
	void shareFiles(const QString &id, const QStringList &urls);
	void runCommand(const QString &id, const QString &key);
	void editCommands(const QString &id);
	void acceptPairing(const QString &id);
	void rejectPairing(const QString &id);

private:
	struct Scan;
	QString m_service;
	QTimer *m_debounce;
	int m_generation = 0;
	bool m_refreshing = false, m_again = false, m_reported = false;
	void call(const QString &path, const QString &interface, const QString &method, const QVariantList &args,
		std::function<void(const QDBusMessage &)> done);
	void action(const QString &id, const QString &plugin, const QString &method, const QVariantList &args,
		const QString &what);
	void scanDevices(const std::shared_ptr<Scan> &scan);
	void finish(const std::shared_ptr<Scan> &scan);
};
