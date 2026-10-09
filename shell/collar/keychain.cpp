#include "keychain.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QTimer>
#include <memory>

static const QString Root = "/org/freedesktop/secrets";
static const QString ServiceInterface = "org.freedesktop.Secret.Service";
static const QString CollectionInterface = "org.freedesktop.Secret.Collection";
static const QString PromptInterface = "org.freedesktop.Secret.Prompt";
static const QString Properties = "org.freedesktop.DBus.Properties";

static QVariant unwrap(const QVariant &value) {
	return value.canConvert<QDBusVariant>() ? value.value<QDBusVariant>().variant() : value;
}

static QStringList objectPaths(const QVariant &value) {
	QStringList paths;
	const QVariant v = unwrap(value);
	if (v.canConvert<QDBusArgument>()) {
		for (const QDBusObjectPath &p : qdbus_cast<QList<QDBusObjectPath>>(v)) {
			paths << p.path();
		}
	} else {
		for (const QDBusObjectPath &p : v.value<QList<QDBusObjectPath>>()) {
			paths << p.path();
		}
	}
	return paths;
}

static QString objectPath(const QVariant &value) {
	return unwrap(value).value<QDBusObjectPath>().path();
}

QString Keychains::defaultService() {
	const QByteArray service = qgetenv("ZACOS9_SECRETS_SERVICE");
	return service.isEmpty() ? QString("org.freedesktop.secrets") : QString::fromUtf8(service);
}

Keychains::Keychains(QObject *parent, const QString &service)
	: QObject(parent), m_service(service), m_debounce(new QTimer(this)) {
	m_debounce->setSingleShot(true);
	m_debounce->setInterval(150);
	connect(m_debounce, &QTimer::timeout, this, [this] { refresh(); });
	auto bus = QDBusConnection::sessionBus();
	for (const char *name : { "CollectionCreated", "CollectionDeleted", "CollectionChanged" }) {
		bus.connect(m_service, Root, ServiceInterface, name, m_debounce, SLOT(start()));
	}
	// A keychain locking or unlocking announces itself as a property change.
	bus.connect(m_service, QString(), Properties, "PropertiesChanged", m_debounce, SLOT(start()));
	bus.connect(m_service, QString(), PromptInterface, "Completed", m_debounce, SLOT(start()));
	auto *watcher = new QDBusServiceWatcher(m_service, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
	connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, m_debounce, [this] { m_debounce->start(); });
}

void Keychains::call(const QString &path, const QString &interface, const QString &method,
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

const KeychainInfo *Keychains::keychain(const QString &path) const {
	for (const KeychainInfo &k : keychains) {
		if (k.path == path) {
			return &k;
		}
	}
	return nullptr;
}

void Keychains::refresh() {
	if (m_refreshing) {
		m_again = true;
		return;
	}
	m_refreshing = true;
	// Reading the collections starts the keyring daemon if D-Bus can; failing that, there are no keychains.
	call(Root, Properties, "Get", { ServiceInterface, QString("Collections") }, [this](const QDBusMessage &reply) {
		if (reply.type() != QDBusMessage::ReplyMessage) {
			finish(false, {}, QString());
			return;
		}
		struct Scan {
			int pending = 0;
			QList<KeychainInfo> keychains;
			QString defaultPath;
		};
		auto scan = std::make_shared<Scan>();
		const QStringList paths = objectPaths(reply.arguments().value(0));
		auto done = [this, scan] {
			if (--scan->pending == 0) {
				QList<KeychainInfo> found;
				for (const KeychainInfo &k : scan->keychains) {
					if (!k.path.isEmpty()) {
						found << k;
					}
				}
				finish(true, found, scan->defaultPath);
			}
		};
		scan->pending = 1 + paths.size();
		scan->keychains.resize(paths.size());
		call(Root, ServiceInterface, "ReadAlias", { QString("default") }, [scan, done](const QDBusMessage &r) {
			if (r.type() == QDBusMessage::ReplyMessage) {
				scan->defaultPath = objectPath(r.arguments().value(0));
			}
			done();
		});
		for (int i = 0; i < paths.size(); ++i) {
			const QString path = paths[i];
			call(path, Properties, "GetAll", { CollectionInterface }, [scan, done, i, path](const QDBusMessage &r) {
				if (r.type() == QDBusMessage::ReplyMessage && !r.arguments().isEmpty()) {
					const QVariantMap props = qdbus_cast<QVariantMap>(r.arguments().first());
					const QString label = unwrap(props.value("Label")).toString();
					// The session keyring lives in memory and has no name; it isn't a keychain to manage.
					if (!label.isEmpty() && !path.endsWith("/session")) {
						scan->keychains[i] = { path, label, unwrap(props.value("Locked")).toBool() };
					}
				}
				done();
			});
		}
	});
}

void Keychains::finish(bool isAvailable, const QList<KeychainInfo> &found, const QString &defaultCollection) {
	m_refreshing = false;
	const bool differs = !m_reported || isAvailable != available || found != keychains ||
		defaultCollection != defaultPath;
	available = isAvailable;
	keychains = found;
	defaultPath = defaultCollection;
	m_reported = true;
	if (differs && changed) {
		changed();
	}
	if (m_again) {
		m_again = false;
		refresh();
	}
}

void Keychains::fail(const QDBusMessage &reply, const QString &what) {
	if (failed) {
		failed(QString("%1 %2").arg(what, reply.errorMessage().isEmpty() ? reply.errorName() : reply.errorMessage()));
	}
}

/* Lock and Unlock answer with the paths they changed and, if the user must
 * be asked, a prompt to show ("/" for none). */
void Keychains::prompt(const QDBusMessage &reply, const QString &what) {
	if (reply.type() != QDBusMessage::ReplyMessage) {
		fail(reply, what);
		return;
	}
	const QString promptPath = objectPath(reply.arguments().value(1));
	if (promptPath.isEmpty() || promptPath == "/") {
		m_debounce->start();
		return;
	}
	call(promptPath, PromptInterface, "Prompt", { QString() }, [this, what](const QDBusMessage &r) {
		if (r.type() != QDBusMessage::ReplyMessage) {
			fail(r, what);
		}
	});
}

void Keychains::unlock(const QString &path) {
	const KeychainInfo *k = keychain(path);
	const QString what = QString("Couldn't unlock the keychain “%1”.").arg(k ? k->name : path);
	call(Root, ServiceInterface, "Unlock", { QVariant::fromValue(QList<QDBusObjectPath>{ QDBusObjectPath(path) }) },
		[this, what](const QDBusMessage &reply) { prompt(reply, what); });
}

void Keychains::lock(const QString &path) {
	const KeychainInfo *k = keychain(path);
	const QString what = QString("Couldn't lock the keychain “%1”.").arg(k ? k->name : path);
	call(Root, ServiceInterface, "Lock", { QVariant::fromValue(QList<QDBusObjectPath>{ QDBusObjectPath(path) }) },
		[this, what](const QDBusMessage &reply) { prompt(reply, what); });
}

void Keychains::lockAll() {
	QList<QDBusObjectPath> paths;
	for (const KeychainInfo &k : keychains) {
		if (!k.locked) {
			paths << QDBusObjectPath(k.path);
		}
	}
	if (paths.isEmpty()) {
		return;
	}
	call(Root, ServiceInterface, "Lock", { QVariant::fromValue(paths) },
		[this](const QDBusMessage &reply) { prompt(reply, "Couldn't lock the keychains."); });
}

void Keychains::setDefault(const QString &path) {
	const KeychainInfo *k = keychain(path);
	const QString what = QString("Couldn't make “%1” the default keychain.").arg(k ? k->name : path);
	call(Root, ServiceInterface, "SetAlias", { QString("default"), QVariant::fromValue(QDBusObjectPath(path)) },
		[this, what](const QDBusMessage &reply) {
			if (reply.type() != QDBusMessage::ReplyMessage) {
				fail(reply, what);
			}
			m_debounce->start();
		});
}
