#include "media.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QTimer>
#include <algorithm>
#include <memory>

static const QString Path = "/org/mpris/MediaPlayer2";
static const QString RootInterface = "org.mpris.MediaPlayer2";
static const QString PlayerInterface = "org.mpris.MediaPlayer2.Player";
static const QString Properties = "org.freedesktop.DBus.Properties";

static QVariant unwrap(const QVariant &value) {
	return value.canConvert<QDBusVariant>() ? value.value<QDBusVariant>().variant() : value;
}

static QVariantMap map(const QVariant &value) {
	const QVariant v = unwrap(value);
	return v.canConvert<QDBusArgument>() ? qdbus_cast<QVariantMap>(v) : v.toMap();
}

QString MediaPlayers::defaultPrefix() {
	const QByteArray prefix = qgetenv("ZACOS9_MPRIS_PREFIX");
	return prefix.isEmpty() ? QString("org.mpris.MediaPlayer2.") : QString::fromUtf8(prefix);
}

MediaPlayers::MediaPlayers(QObject *parent, const QString &prefix)
	: QObject(parent), m_prefix(prefix), m_debounce(new QTimer(this)) {
	m_debounce->setSingleShot(true);
	m_debounce->setInterval(150);
	connect(m_debounce, &QTimer::timeout, this, [this] { refresh(); });
	auto bus = QDBusConnection::sessionBus();
	// Players come and go, and announce what they play as property changes.
	bus.connect("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameOwnerChanged",
		m_debounce, SLOT(start()));
	bus.connect(QString(), Path, Properties, "PropertiesChanged", m_debounce, SLOT(start()));
}

void MediaPlayers::call(const QString &service, const QString &interface, const QString &method,
		const QVariantList &args, std::function<void(const QDBusMessage &)> done) {
	QDBusMessage message = QDBusMessage::createMethodCall(service,
		service == "org.freedesktop.DBus" ? QString("/org/freedesktop/DBus") : Path, interface, method);
	message.setArguments(args);
	auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 5000), this);
	connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, done = std::move(done)] {
		watcher->deleteLater();
		if (done) {
			done(watcher->reply());
		}
	});
}

void MediaPlayers::refresh() {
	if (m_refreshing) {
		m_again = true;
		return;
	}
	m_refreshing = true;
	call("org.freedesktop.DBus", "org.freedesktop.DBus", "ListNames", {}, [this](const QDBusMessage &reply) {
		QStringList services;
		if (reply.type() == QDBusMessage::ReplyMessage) {
			for (const QString &name : reply.arguments().value(0).toStringList()) {
				if (name.startsWith(m_prefix)) {
					services << name;
				}
			}
		}
		services.sort();
		if (services.isEmpty()) {
			finish({});
			return;
		}
		struct Scan {
			int pending = 0;
			QList<MediaPlayer> players;
		};
		auto scan = std::make_shared<Scan>();
		scan->pending = 2 * services.size();
		scan->players.resize(services.size());
		auto done = [this, scan] {
			if (--scan->pending == 0) {
				QList<MediaPlayer> found;
				for (const MediaPlayer &p : scan->players) {
					if (!p.service.isEmpty()) {
						found << p;
					}
				}
				finish(found);
			}
		};
		for (int i = 0; i < services.size(); ++i) {
			const QString service = services[i];
			call(service, Properties, "GetAll", { RootInterface }, [scan, done, i, service](const QDBusMessage &r) {
				if (r.type() == QDBusMessage::ReplyMessage) {
					const QVariantMap props = map(r.arguments().value(0));
					MediaPlayer &p = scan->players[i];
					p.service = service;
					p.identity = unwrap(props.value("Identity")).toString();
					if (p.identity.isEmpty()) {
						p.identity = service.section('.', 3, 3);
					}
					p.canRaise = unwrap(props.value("CanRaise")).toBool();
				}
				done();
			});
			call(service, Properties, "GetAll", { PlayerInterface }, [scan, done, i](const QDBusMessage &r) {
				if (r.type() == QDBusMessage::ReplyMessage) {
					const QVariantMap props = map(r.arguments().value(0));
					MediaPlayer &p = scan->players[i];
					p.status = unwrap(props.value("PlaybackStatus")).toString();
					p.canPlay = unwrap(props.value("CanPlay")).toBool();
					p.canPause = unwrap(props.value("CanPause")).toBool();
					p.canNext = unwrap(props.value("CanGoNext")).toBool();
					p.canPrevious = unwrap(props.value("CanGoPrevious")).toBool();
					const QVariantMap meta = map(props.value("Metadata"));
					p.title = unwrap(meta.value("xesam:title")).toString();
					const QVariant artist = unwrap(meta.value("xesam:artist"));
					p.artist = artist.toStringList().isEmpty() ? artist.toString() : artist.toStringList().join(", ");
				}
				done();
			});
		}
	});
}

void MediaPlayers::finish(const QList<MediaPlayer> &found) {
	m_refreshing = false;
	const bool differs = found != players;
	players = found;
	if (differs && changed) {
		changed();
	}
	if (m_again) {
		m_again = false;
		refresh();
	}
}

const MediaPlayer *MediaPlayers::current() const {
	for (const MediaPlayer &p : players) {
		if (p.service == m_chosen) {
			return &p;
		}
	}
	for (const char *status : { "Playing", "Paused" }) {
		for (const MediaPlayer &p : players) {
			if (p.status == status) {
				return &p;
			}
		}
	}
	return players.isEmpty() ? nullptr : &players.first();
}

void MediaPlayers::choose(const QString &service) {
	m_chosen = service;
	if (changed) {
		changed();
	}
}

void MediaPlayers::command(const QString &service, const QString &interface, const QString &method,
		const QString &what) {
	call(service, interface, method, {}, [this, what](const QDBusMessage &reply) {
		if (reply.type() != QDBusMessage::ReplyMessage && failed) {
			failed(QString("%1 %2").arg(what,
				reply.errorMessage().isEmpty() ? reply.errorName() : reply.errorMessage()));
		}
		m_debounce->start();
	});
}

void MediaPlayers::playPause(const QString &service) {
	command(service, PlayerInterface, "PlayPause", "Couldn't play or pause.");
}

void MediaPlayers::next(const QString &service) {
	command(service, PlayerInterface, "Next", "Couldn't go to the next track.");
}

void MediaPlayers::previous(const QString &service) {
	command(service, PlayerInterface, "Previous", "Couldn't go to the previous track.");
}

void MediaPlayers::raise(const QString &service) {
	command(service, RootInterface, "Raise", "Couldn't bring the player forward.");
}
