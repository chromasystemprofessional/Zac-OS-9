#pragma once

#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <functional>
#include <vector>

struct InitializationDisk {
	QString device, identity, name;
	quint64 size = 0;
	QStringList children;
	bool unreadable = false;
	bool eligible = false;
};

bool parseInitializationDisks(const QByteArray &data,
	std::vector<InitializationDisk> *disks, QString *error);

class DiskInitialization : public QObject {
public:
	enum Choice { Ignore, Eject, Initialize, Erase };
	struct Interface {
		std::function<Choice(const InitializationDisk &)> choose;
		std::function<bool(const InitializationDisk &)> confirm;
		std::function<void(const QString &)> error;
		std::function<void(bool)> busy;
		std::function<void(const QString &)> initialized;
		std::function<void()> availabilityChanged = [] {};
		std::function<void(const QString &, bool)> inhibitMount = [](const QString &, bool) {};
	};
	DiskInitialization(Interface interface, QString helper, QString authorizer,
		QString ejector, QObject *parent = nullptr);
	void start();
	void mountFailed(const QString &device);
	void eraseDevice(const QString &device, const QString &volumeName);
	bool canEraseDevice(const QString &device) const { return m_erasable.contains(device); }

private:
	void scan();
	void acceptDisks(const std::vector<InitializationDisk> &disks);
	void revalidate(const InitializationDisk &disk, Choice choice);
	void operate(const InitializationDisk &disk, Choice choice);
	void report(const QString &message);
	void run(const QString &program, const QStringList &args, int timeout,
		std::function<void(const QByteArray &, const QString &)> finished);
	Interface m_interface;
	QString m_helper, m_authorizer, m_ejector;
	QTimer m_scanTimer;
	QSet<QString> m_seen, m_failed, m_erasable;
	bool m_scanning = false, m_operating = false, m_presenting = false;
	QString m_lastScanError, m_lastEraseError;
};

void diskInitializationStart(std::function<void()> availabilityChanged);
void diskEraseVolume(const QString &mountPath, const QString &volumeName);
bool diskCanEraseVolume(const QString &mountPath);
