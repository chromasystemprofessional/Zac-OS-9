#pragma once

/*
 * Collar modules from other programs. A program that owns a session-bus name
 * starting with org.zacos9.CollarExtension. serves its modules at
 * /org/zacos9/CollarExtension (interface org.zacos9.CollarExtension1):
 *
 *   GetModules() -> aa{sv}   one dict per module:
 *       id s, label s, description s, active b,
 *       icon ay (16 x 16, 1024 bytes: A, R, G, B per pixel, straight alpha,
 *                rows top to bottom),
 *       menu a(ssu): (item id, label, flags) with flags
 *           1 enabled, 2 checked, 4 separator
 *   Activate(s module, s item)   a D-Bus error's message is shown in an alert
 *   signal Changed()             The Collar calls GetModules again
 *
 * See docs/collar.md, "Extensions".
 */

#include <QImage>
#include <QList>
#include <QMap>
#include <QObject>
#include <functional>

class QTimer;

struct ExtensionItem {
	enum Flags { Enabled = 1, Checked = 2, Separator = 4 };
	QString id, label;
	uint flags = Enabled;
	bool operator==(const ExtensionItem &o) const { return id == o.id && label == o.label && flags == o.flags; }
};

struct ExtensionModule {
	QString service, id, label, description;
	bool active = false;
	QImage icon; /* 16 x 16 ARGB32, or null */
	QList<ExtensionItem> menu;
	/* The Collar's Module::device for this module. */
	QString key() const { return service + '\n' + id; }
	bool operator==(const ExtensionModule &o) const {
		return service == o.service && id == o.id && label == o.label && description == o.description &&
			active == o.active && icon == o.icon && menu == o.menu;
	}
};

class CollarExtensions : public QObject {
public:
	static constexpr const char *Interface = "org.zacos9.CollarExtension1";
	static constexpr const char *Path = "/org/zacos9/CollarExtension";
	/* $ZACOS9_COLLAR_EXTENSION_PREFIX (for tests), else org.zacos9.CollarExtension. */
	static QString defaultPrefix();
	explicit CollarExtensions(QObject *parent = nullptr, const QString &prefix = defaultPrefix());

	/* Every extension's modules, ordered by service name. */
	QList<ExtensionModule> modules;
	std::function<void()> changed;
	std::function<void(const QString &)> failed;

	void refresh();
	const ExtensionModule *module(const QString &key) const;
	void activate(const QString &key, const QString &item);

private:
	QString m_prefix;
	QTimer *m_debounce;
	QMap<QString, QList<ExtensionModule>> m_byService;
	QMap<QString, int> m_pending; /* service -> calls in flight */
	void query(const QString &service);
	void publish();
};
