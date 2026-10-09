/* The Collar's Keychain module against a fake Secret Service (gnome-keyring) on the session bus. */
#include "collar.h"
#include "keychain.h"
#include "settings.h"

#include <QApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QElapsedTimer>
#include <QImage>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>

static int failures;
static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	failures += !ok;
}

static bool until(const std::function<bool()> &done, int timeout = 5000) {
	QElapsedTimer clock;
	clock.start();
	while (!done() && clock.elapsed() < timeout) {
		QCoreApplication::processEvents();
		QThread::msleep(1);
	}
	return done();
}

static void settle(int duration = 80) {
	until([] { return false; }, duration);
}

static void key(QWidget &widget, int code) {
	QKeyEvent event(QEvent::KeyPress, code, Qt::NoModifier);
	QApplication::sendEvent(&widget, &event);
	settle();
}

static const QString Service = "org.zacos9.test.Secrets";
static const QString Root = "/org/freedesktop/secrets";
static const QString Login = Root + "/collection/login";
static const QString Default = Root + "/collection/Default_5fKeyring";
static const QString Session = Root + "/collection/session";
static const QString PromptPath = Root + "/prompt/p1";

class FakeSecrets : public QDBusVirtualObject {
public:
	QMap<QString, bool> locked = { { Login, true }, { Default, true }, { Session, false } };
	QString alias = Default;
	QStringList calls, pendingUnlock;
	QString introspect(const QString &) const override { return {}; }

	bool handleMessage(const QDBusMessage &message, const QDBusConnection &bus) override {
		const QString path = message.path(), member = message.member();
		const QVariantList args = message.arguments();
		auto variant = [](const QVariant &value) { return QVariant::fromValue(QDBusVariant(value)); };
		auto paths = [](const QVariant &value) {
			QStringList out;
			for (const QDBusObjectPath &p : qdbus_cast<QList<QDBusObjectPath>>(value)) {
				out << p.path();
			}
			return out;
		};
		if (path == Root && member == "Get") {
			bus.send(message.createReply(variant(QVariant::fromValue(QList<QDBusObjectPath>{
				QDBusObjectPath(Login), QDBusObjectPath(Default), QDBusObjectPath(Session) }))));
		} else if (path == Root && member == "ReadAlias") {
			bus.send(message.createReply(QVariant::fromValue(QDBusObjectPath(alias))));
		} else if (path.startsWith(Root + "/collection/") && member == "GetAll") {
			const QString label = path == Login ? "Login" : path == Default ? "Default Keyring" : QString();
			bus.send(message.createReply(QVariant(QVariantMap{
				{ "Label", label }, { "Locked", locked.value(path) } })));
		} else if (path == Root && member == "Unlock") {
			// Locked keychains need their password: the caller must show the prompt.
			pendingUnlock = paths(args.value(0));
			calls << "Unlock " + pendingUnlock.join(",");
			bus.send(message.createReply({ QVariant::fromValue(QList<QDBusObjectPath>()),
				QVariant::fromValue(QDBusObjectPath(PromptPath)) }));
		} else if (path == PromptPath && member == "Prompt") {
			calls << "Prompt";
			bus.send(message.createReply());
			for (const QString &p : pendingUnlock) {
				locked[p] = false;
			}
			QDBusMessage done = QDBusMessage::createSignal(PromptPath, "org.freedesktop.Secret.Prompt", "Completed");
			done << false << QVariant::fromValue(QDBusVariant(QVariant::fromValue(QList<QDBusObjectPath>())));
			bus.send(done);
		} else if (path == Root && member == "Lock") {
			const QStringList which = paths(args.value(0));
			calls << "Lock " + which.join(",");
			QList<QDBusObjectPath> done;
			for (const QString &p : which) {
				locked[p] = true;
				done << QDBusObjectPath(p);
			}
			bus.send(message.createReply({ QVariant::fromValue(done), QVariant::fromValue(QDBusObjectPath("/")) }));
		} else if (path == Root && member == "SetAlias") {
			alias = args.value(1).value<QDBusObjectPath>().path();
			calls << "SetAlias " + args.value(0).toString() + " " + alias;
			bus.send(message.createReply());
		} else {
			bus.send(message.createErrorReply("org.freedesktop.DBus.Error.UnknownMethod", member));
		}
		return true;
	}
};

int main(int argc, char **argv) {
	QTemporaryDir config;
	if (!config.isValid()) {
		return 1;
	}
	qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
	qputenv("XDG_RUNTIME_DIR", config.path().toUtf8());
	qputenv("XDG_DATA_HOME", (config.path() + "/data").toUtf8());
	qputenv("WAYLAND_DISPLAY", "test-collar-keychain");
	qputenv("ZACOS9_KDECONNECT_SERVICE", "org.zacos9.test.NoKdeConnect");
	qputenv("ZACOS9_SECRETS_SERVICE", Service.toUtf8());
	// No Keychain Access, so the menu's last item is Lock All Keychains.
	qputenv("PATH", config.path().toUtf8());
	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);
	pl_setting_set("sound-theme", "none");

	Collar collar(false);
	collar.show();
	Keychains &keys = collar.keychains();
	keys.refresh();
	settle(300);
	check(!keys.available && collar.moduleIndex(Collar::Keychain) < 0, "no keyring daemon, no Keychain module");

	FakeSecrets fake;
	QDBusConnection daemon = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "fake-secrets");
	check(daemon.registerVirtualObject(Root, &fake, QDBusConnection::SubPath) && daemon.registerService(Service),
		"fake Secret Service registered");
	check(until([&] { return keys.available && keys.keychains.size() == 2; }),
		"the keychains are found as soon as the keyring daemon appears");
	const int index = collar.moduleIndex(Collar::Keychain);
	check(index == 4 && collar.modules().value(5).kind == Collar::Resolution,
		"one Keychain module, after Bluetooth");
	check(collar.menuLabel(index) == "Keychain", "its menu is labelled Keychain");
	check(keys.keychains.value(0).name == "Login" && keys.keychains.value(1).name == "Default Keyring" &&
		keys.keychains.value(0).locked && keys.defaultPath == Default,
		"Login and Default Keyring listed, the session keyring left out, Default Keyring the default");
	check(keys.defaultKeychain() && keys.defaultKeychain()->locked, "the default keychain starts locked");

	// The padlock's left shackle leg (x 9 of the 16 px icon) reaches the body when locked.
	auto shackleClosed = [&] {
		const QImage image = collar.grab().toImage();
		const QRect cell = collar.moduleRect(collar.moduleIndex(Collar::Keychain));
		return image.pixelColor(cell.left() + 2 + 9, 4 + 6) == QColor(Qt::black);
	};
	check(shackleClosed(), "a locked default keychain shows a closed padlock");

	collar.setFocus();
	auto choose = [&](int key1, int presses) {
		collar.setVisibleModules(collar.modules().size());
		QKeyEvent left(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
		for (int i = 0; i < collar.modules().size(); ++i) {
			QApplication::sendEvent(&collar, &left);
		}
		QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
		for (int i = 0; i < collar.moduleIndex(Collar::Keychain); ++i) {
			QApplication::sendEvent(&collar, &right);
		}
		key(collar, Qt::Key_Return);
		QWidget *menu = QApplication::activePopupWidget();
		if (!menu) {
			return false;
		}
		for (int i = 0; i < presses; ++i) {
			key(*menu, key1);
		}
		key(*menu, Qt::Key_Return);
		return until([&] { return !fake.calls.isEmpty(); });
	};
	// The menu opens on the checked default keychain: Login above it, then Unlock Login, Unlock Default Keyring.
	check(choose(Qt::Key_Down, 2) && fake.calls.value(0) == "Unlock " + Default &&
		until([&] { return fake.calls.size() == 2; }) && fake.calls.value(1) == "Prompt",
		"Unlock asks for the keychain's password through the keyring's prompt");
	check(until([&] { return keys.defaultKeychain() && !keys.defaultKeychain()->locked; }),
		"the keychain shows unlocked once the prompt completes");
	check(!shackleClosed(), "and the padlock opens");
	fake.calls.clear();

	check(choose(Qt::Key_Up, 1) && fake.calls.value(0) == "SetAlias default " + Login &&
		until([&] { return keys.defaultPath == Login; }), "choosing a keychain makes it the default");
	check(shackleClosed(), "the padlock follows the new default (Login, locked)");
	fake.calls.clear();

	// Default is now Login (item 2): down past Default Keyring, Unlock Login, Lock Default Keyring to Lock All.
	check(choose(Qt::Key_Down, 4) && fake.calls.value(0) == "Lock " + Default &&
		until([&] { return keys.keychain(Default) && keys.keychain(Default)->locked; }),
		"Lock All Keychains locks every unlocked keychain");
	fake.calls.clear();

	daemon.unregisterService(Service);
	check(until([&] { return !keys.available; }) && collar.moduleIndex(Collar::Keychain) < 0,
		"the module goes away with the keyring daemon");
	return failures ? 1 : 0;
}
