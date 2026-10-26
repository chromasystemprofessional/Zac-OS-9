#include "extensions.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QTimer>

static QVariant unwrap(const QVariant &value) {
	return value.canConvert<QDBusVariant>() ? value.value<QDBusVariant>().variant() : value;
}

static QList<ExtensionItem> menuItems(const QVariant &value) {
	QList<ExtensionItem> items;
	const QVariant v = unwrap(value);
	if (!v.canConvert<QDBusArgument>()) {
		return items;
	}
	const QDBusArgument arg = v.value<QDBusArgument>();
	arg.beginArray();
	while (!arg.atEnd()) {
		ExtensionItem item;
		arg.beginStructure();
		arg >> item.id >> item.label >> item.flags;
		arg.endStructure();
		items << item;
	}
	arg.endArray();
	return items;
}

static QImage iconImage(const QVariant &value) {
	const QByteArray bytes = unwrap(value).toByteArray();
	if (bytes.size() != 16 * 16 * 4) {
		return {};
	}
	QImage image(16, 16, QImage::Format_ARGB32);
	for (int y = 0; y < 16; ++y) {
		auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
		for (int x = 0; x < 16; ++x) {
			const auto *p = reinterpret_cast<const uchar *>(bytes.constData()) + (y * 16 + x) * 4;
			row[x] = qRgba(p[1], p[2], p[3], p[0]); // big-endian ARGB, as sent
		}
	}
	return image;
}

static QList<ExtensionModule> parseModules(const QString &service, const QDBusMessage &reply) {
	QList<ExtensionModule> modules;
	const QVariant v = reply.arguments().value(0);
	QList<QVariantMap> maps;
	if (v.canConvert<QList<QVariantMap>>() && !v.canConvert<QDBusArgument>()) {
		maps = v.value<QList<QVariantMap>>(); // when this process has registered the type itself
	} else if (v.canConvert<QDBusArgument>()) {
		const QDBusArgument arg = v.value<QDBusArgument>();
		arg.beginArray();
		while (!arg.atEnd()) {
			QVariantMap map;
			arg >> map;
			maps << map;
		}
		arg.endArray();
	}
	for (const QVariantMap &map : maps) {
		ExtensionModule m;
		m.service = service;
		m.id = unwrap(map.value("id")).toString();
		if (m.id.isEmpty()) {
			continue;
		}
		m.label = unwrap(map.value("label")).toString();
		m.description = unwrap(map.value("description")).toString();
		m.active = unwrap(map.value("active")).toBool();
		m.icon = iconImage(map.value("icon"));
		m.menu = menuItems(map.value("menu"));
		modules << m;
	}
	return modules;
}

QString CollarExtensions::defaultPrefix() {
	const QByteArray prefix = qgetenv("ZACOS9_COLLAR_EXTENSION_PREFIX");
	return prefix.isEmpty() ? QString("org.zacos9.CollarExtension") : QString::fromUtf8(prefix);
}

CollarExtensions::CollarExtensions(QObject *parent, const QString &prefix)
	: QObject(parent), m_prefix(prefix), m_debounce(new QTimer(this)) {
	m_debounce->setSingleShot(true);
	m_debounce->setInterval(150);
	connect(m_debounce, &QTimer::timeout, this, [this] { refresh(); });
	auto bus = QDBusConnection::sessionBus();
	bus.connect(QString(), Path, Interface, "Changed", m_debounce, SLOT(start()));
	// Qt's wildcard is "prefix*" (refresh() keeps only the names under "prefix.").
	auto *watcher = new QDBusServiceWatcher(m_prefix + "*", bus, QDBusServiceWatcher::WatchForOwnerChange, this);
	connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, m_debounce, [this] { m_debounce->start(); });
}

const ExtensionModule *CollarExtensions::module(const QString &key) const {
	for (const ExtensionModule &m : modules) {
		if (m.key() == key) {
			return &m;
		}
	}
	return nullptr;
}

void CollarExtensions::refresh() {
	auto bus = QDBusConnection::sessionBus();
	const QStringList names = bus.interface() ? bus.interface()->registeredServiceNames().value() : QStringList();
	QStringList services;
	for (const QString &name : names) {
		if (name.startsWith(m_prefix + ".")) {
			services << name;
		}
	}
	bool gone = false;
	for (const QString &service : m_byService.keys()) {
		if (!services.contains(service)) {
			m_byService.remove(service);
			gone = true;
		}
	}
	if (gone) {
		publish();
	}
	for (const QString &service : services) {
		query(service);
	}
}

void CollarExtensions::query(const QString &service) {
	// One call at a time per extension; a slow one can't pile up five-second refreshes.
	if (m_pending.value(service)) {
		return;
	}
	m_pending[service] = 1;
	const QDBusMessage message = QDBusMessage::createMethodCall(service, Path, Interface, "GetModules");
	auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 5000), this);
	connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, service] {
		watcher->deleteLater();
		m_pending.remove(service);
		const QDBusMessage reply = watcher->reply();
		if (reply.type() == QDBusMessage::ReplyMessage) {
			m_byService[service] = parseModules(service, reply);
		} else {
			m_byService.remove(service);
		}
		publish();
	});
}

void CollarExtensions::publish() {
	QList<ExtensionModule> all;
	for (const QList<ExtensionModule> &list : m_byService) { // QMap: ordered by service name
		all << list;
	}
	if (all == modules) {
		return;
	}
	modules = all;
	if (changed) {
		changed();
	}
}

void CollarExtensions::activate(const QString &key, const QString &item) {
	const ExtensionModule *m = module(key);
	if (!m) {
		return;
	}
	QDBusMessage message = QDBusMessage::createMethodCall(m->service, Path, Interface, "Activate");
	message.setArguments({ m->id, item });
	auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 30000), this);
	connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, label = m->label] {
		watcher->deleteLater();
		const QDBusMessage reply = watcher->reply();
		if (reply.type() == QDBusMessage::ErrorMessage && failed) {
			failed(reply.errorMessage().isEmpty() ? QString("%1 didn't respond.").arg(label) : reply.errorMessage());
		}
		m_debounce->start();
	});
}
