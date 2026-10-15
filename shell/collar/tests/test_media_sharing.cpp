/* The Collar's media module against a fake MPRIS player on the session bus,
 * and its File Sharing module against a fake sharing.conf, ss and helper. */
#include "collar.h"
#include "filesharing.h"
#include "media.h"
#include "settings.h"

#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QElapsedTimer>
#include <QFile>
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

static const QString Prefix = "org.zacos9.test.mpris.";
static const QString Service = Prefix + "Fake";
static const QString Path = "/org/mpris/MediaPlayer2";

class FakePlayer : public QDBusVirtualObject {
public:
	QString status = "Playing";
	QStringList calls;
	QString introspect(const QString &) const override { return {}; }

	bool handleMessage(const QDBusMessage &message, const QDBusConnection &bus) override {
		const QString member = message.member();
		auto variant = [](const QVariant &value) { return QVariant::fromValue(QDBusVariant(value)); };
		if (member == "GetAll" && message.arguments().value(0) == "org.mpris.MediaPlayer2") {
			bus.send(message.createReply(QVariant(QVariantMap{
				{ "Identity", "Fake Player" }, { "CanRaise", true } })));
		} else if (member == "GetAll") {
			bus.send(message.createReply(QVariant(QVariantMap{ { "PlaybackStatus", status },
				{ "CanPlay", true }, { "CanPause", true }, { "CanGoNext", true }, { "CanGoPrevious", false },
				{ "Metadata", variant(QVariantMap{ { "xesam:title", "Song" },
					{ "xesam:artist", QStringList{ "Band" } } }) } })));
		} else {
			calls << member;
			if (member == "PlayPause") {
				status = status == "Playing" ? "Paused" : "Playing";
			}
			bus.send(message.createReply());
			QDBusMessage signal = QDBusMessage::createSignal(Path, "org.freedesktop.DBus.Properties",
				"PropertiesChanged");
			signal.setArguments({ QString("org.mpris.MediaPlayer2.Player"),
				QVariantMap{ { "PlaybackStatus", status } }, QStringList() });
			bus.send(signal);
		}
		return true;
	}
};

static void write(const QString &path, const QByteArray &text, bool executable = false) {
	QFile f(path);
	f.open(QIODevice::WriteOnly);
	f.write(text);
	f.close();
	if (executable) {
		f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
	}
}

static QByteArray read(const QString &path) {
	QFile f(path);
	return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static void testParsing() {
	bool afp, smb;
	parseSharingConf("name=X\nafp=yes\nsmb=no\n", &afp, &smb);
	check(afp && !smb, "sharing.conf: Mac sharing on, Windows off");
	check(sharingConfWith("name=X\nowner=me\nafp=yes\nsmb=yes\nguests=no\n", false, false) ==
		"name=X\nowner=me\nafp=no\nsmb=no\nguests=no\n", "turning it off keeps the rest of sharing.conf");
	check(sharingConfWith("name=X\n", true, false) == "name=X\nafp=yes\nsmb=no\n",
		"and adds the switches if missing");
	const QList<SharingVisitor> v = parseVisitors(
		"0 0 192.168.1.2:548 [::ffff:192.168.1.5]:51234\n"
		"0 0 [::ffff:192.168.1.2]:445 10.0.0.9:50000\n"
		"0 0 192.168.1.2:445 10.0.0.9:50001\n"
		"0 0 192.168.1.2:22 10.0.0.7:40000\n");
	check(v.size() == 2 && v[0] == SharingVisitor{ "Mac", "192.168.1.5" } &&
		v[1] == SharingVisitor{ "Windows", "10.0.0.9" },
		"visitors: one per computer, by port, the IPv4 address unwrapped, other ports ignored");
}

int main(int argc, char **argv) {
	QTemporaryDir dir;
	if (!dir.isValid()) {
		return 1;
	}
	const QString root = dir.path();
	qputenv("XDG_CONFIG_HOME", root.toUtf8());
	qputenv("XDG_RUNTIME_DIR", root.toUtf8());
	qputenv("XDG_DATA_HOME", (root + "/data").toUtf8());
	qputenv("WAYLAND_DISPLAY", "test-collar-media");
	qputenv("ZACOS9_KDECONNECT_SERVICE", "org.zacos9.test.NoKdeConnect");
	qputenv("ZACOS9_SECRETS_SERVICE", "org.zacos9.test.NoSecrets");
	qputenv("ZACOS9_MPRIS_PREFIX", Prefix.toUtf8());
	// File Sharing: its settings here, a fake ss, and a fake helper (through a
	// fake pkexec) that writes what it is given as the settings.
	QDir(root).mkpath("etc");
	qputenv("ZACOS9_SHARING_CONF", (root + "/etc").toUtf8());
	write(root + "/ss", "#!/bin/sh\nprintf '0 0 192.168.1.2:548 [::ffff:192.168.1.5]:51234\\n"
		"0 0 192.168.1.2:445 10.0.0.9:50000\\n'\n", true);
	qputenv("ZACOS9_SS", (root + "/ss").toUtf8());
	write(root + "/helper", QString("#!/bin/sh\n[ \"$1\" = apply ] && cat > '%1/etc/sharing.conf'\n")
		.arg(root).toUtf8(), true);
	qputenv("ZACOS9_SHARING_HELPER", (root + "/helper").toUtf8());
	QDir(root).mkpath("bin");
	write(root + "/bin/pkexec", "#!/bin/sh\nexec \"$@\"\n", true);
	qputenv("PATH", (root + "/bin:" + qgetenv("PATH")).toUtf8());
	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);
	pl_setting_set("sound-theme", "none");

	testParsing();

	Collar collar(false);
	collar.show();
	MediaPlayers &media = collar.media();
	SharingStatus &sharing = collar.fileSharing();
	media.refresh();
	sharing.refresh();
	settle(300);
	check(collar.moduleIndex(Collar::Media) < 0, "no player, no media module");
	check(collar.moduleIndex(Collar::FileSharing) < 0, "File Sharing never set up, no File Sharing module");

	// Opens a module's menu from the keyboard, goes down `presses` items and chooses.
	collar.setFocus();
	auto choose = [&](Collar::Kind kind, int presses) {
		collar.setVisibleModules(collar.modules().size());
		QKeyEvent left(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
		for (int i = 0; i < collar.modules().size(); ++i) {
			QApplication::sendEvent(&collar, &left);
		}
		QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
		for (int i = 0; i < collar.moduleIndex(kind); ++i) {
			QApplication::sendEvent(&collar, &right);
		}
		key(collar, Qt::Key_Return);
		QWidget *menu = QApplication::activePopupWidget();
		if (!menu) {
			return false;
		}
		for (int i = 0; i < presses; ++i) {
			key(*menu, Qt::Key_Down);
		}
		key(*menu, Qt::Key_Return);
		return true;
	};

	// ---- Media ----
	FakePlayer fake;
	QDBusConnection daemon = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "fake-player");
	check(daemon.registerVirtualObject(Path, &fake) && daemon.registerService(Service), "fake player registered");
	check(until([&] { return media.players.size() == 1; }), "the player is found as soon as it appears");
	const int index = collar.moduleIndex(Collar::Media);
	check(index == 2 && collar.modules().value(1).kind == Collar::SoundSet &&
		collar.modules().value(3).kind == Collar::Network, "one media module, after the sound set");
	check(collar.menuLabel(index) == "Fake Player", "its menu is named after the player");
	check(media.current() && media.current()->title == "Song" && media.current()->artist == "Band" &&
		media.current()->playing(), "what it plays: Song by Band, playing");
	check(choose(Collar::Media, 1) && until([&] { return fake.calls.value(0) == "PlayPause"; }),
		"Pause asks the player to pause");
	check(until([&] { return media.current() && media.current()->status == "Paused"; }),
		"the module follows the player's state");
	fake.calls.clear();
	// Paused: Play, Next Track (Previous Track is disabled), then Show Fake Player.
	check(choose(Collar::Media, 3) && until([&] { return fake.calls.value(0) == "Raise"; }),
		"Show brings the player forward");
	daemon.unregisterService(Service);
	check(until([&] { return collar.moduleIndex(Collar::Media) < 0; }), "the module goes away with the player");

	// ---- File Sharing ----
	const QString conf = root + "/etc/sharing.conf";
	write(conf, "name=Test Computer\nowner=me\nafp=yes\nsmb=no\nguests=no\n");
	sharing.refresh();
	check(until([&] { return sharing.visitors.size() == 2; }) && sharing.on() && sharing.afp && !sharing.smb,
		"File Sharing set up and on (Mac), two computers connected");
	const int share = collar.moduleIndex(Collar::FileSharing);
	check(share >= 0 && collar.modules().value(share - 1).kind == Collar::Network,
		"one File Sharing module, after Network");
	check(choose(Collar::FileSharing, 1) && until([&] { return !sharing.on(); }),
		"Turn File Sharing Off turns it off");
	check(read(conf) == "name=Test Computer\nowner=me\nafp=no\nsmb=no\nguests=no\n",
		"through the helper, with the other settings kept");
	check(collar.moduleIndex(Collar::FileSharing) >= 0, "the module stays while it is off");
	check(choose(Collar::FileSharing, 1) && until([&] { return sharing.on(); }) && sharing.afp && !sharing.smb,
		"Turn File Sharing On brings back what was on before");
	QFile::remove(conf);
	sharing.refresh();
	check(until([&] { return collar.moduleIndex(Collar::FileSharing) < 0; }),
		"without its settings the module goes away");
	return failures ? 1 : 0;
}
