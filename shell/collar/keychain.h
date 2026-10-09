#pragma once

/*
 * The user's keychains, as Mac OS 9's Keychain Strip showed them: each
 * keychain, locked or not, which one is the default, and locking and
 * unlocking. On Linux they are the Secret Service's collections
 * (org.freedesktop.secrets, normally gnome-keyring-daemon); unlocking asks
 * for the password in the keyring's own prompt.
 */

#include <QDBusMessage>
#include <QList>
#include <QObject>
#include <QVariantList>
#include <functional>

class QTimer;

struct KeychainInfo {
	QString path, name;
	bool locked = true;
	bool operator==(const KeychainInfo &o) const { return path == o.path && name == o.name && locked == o.locked; }
};

class Keychains : public QObject {
public:
	explicit Keychains(QObject *parent = nullptr, const QString &service = defaultService());
	/* $ZACOS9_SECRETS_SERVICE (for tests), else org.freedesktop.secrets. */
	static QString defaultService();
	bool available = false;
	QList<KeychainInfo> keychains; /* the session keyring (in memory only) left out */
	QString defaultPath;
	std::function<void()> changed;
	std::function<void(const QString &)> failed;

	void refresh();
	const KeychainInfo *keychain(const QString &path) const;
	const KeychainInfo *defaultKeychain() const { return keychain(defaultPath); }
	void unlock(const QString &path);
	void lock(const QString &path);
	void lockAll();
	void setDefault(const QString &path);

private:
	QString m_service;
	QTimer *m_debounce;
	bool m_refreshing = false, m_again = false, m_reported = false;
	void call(const QString &path, const QString &interface, const QString &method, const QVariantList &args,
		std::function<void(const QDBusMessage &)> done);
	void fail(const QDBusMessage &reply, const QString &what);
	void prompt(const QDBusMessage &reply, const QString &what);
	void finish(bool available, const QList<KeychainInfo> &keychains, const QString &defaultPath);
};
