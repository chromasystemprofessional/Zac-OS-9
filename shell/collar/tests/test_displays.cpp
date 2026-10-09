#include "displaycontrols.h"
#include "settings.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVirtualObject>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
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

static bool write(const QString &path, const QByteArray &data) {
	QDir().mkpath(QFileInfo(path).path());
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

static QByteArray read(const QString &path) {
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed() : QByteArray();
}

class Login : public QDBusVirtualObject {
public:
	QString brightnessPath, device;
	QString introspect(const QString &) const override { return {}; }
	bool handleMessage(const QDBusMessage &message, const QDBusConnection &bus) override {
		if (message.member() == "GetSessionByPID") {
			bus.send(message.createReply(QVariantList{
				QVariant::fromValue(QDBusObjectPath("/org/freedesktop/login1/session/test")) }));
		} else if (message.member() == "SetBrightness") {
			const auto args = message.arguments();
			device = args.value(1).toString();
			check(args.value(0) == "backlight", "built-in brightness goes through logind's backlight subsystem");
			write(brightnessPath, QByteArray::number(args.value(2).toUInt()));
			bus.send(message.createReply());
		} else {
			return false;
		}
		return true;
	}
};

int main(int argc, char **argv) {
	QTemporaryDir dir;
	if (!dir.isValid() || argc < 2) {
		return 1;
	}
	const QString root = dir.path();
	qputenv("XDG_CONFIG_HOME", (root + "/config").toUtf8());
	qputenv("DBUS_SYSTEM_BUS_ADDRESS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));
	qputenv("FAKE_DDC_STATE", (root + "/ddc").toUtf8());
	qputenv("FAKE_DDC_LOG", (root + "/ddc.log").toUtf8());
	QCoreApplication app(argc, argv);
	const QString ddcutil = QFileInfo(app.arguments().at(1)).absoluteFilePath();

	auto bus = QDBusConnection::systemBus();
	Login login;
	login.brightnessPath = root + "/backlight/intel_backlight/brightness";
	check(bus.registerVirtualObject("/org/freedesktop/login1", &login, QDBusConnection::SubPath) &&
		bus.registerService("org.freedesktop.login1"), "own a fake login service on the isolated bus");

	const QString outputs = root + "/outputs";
	check(write(outputs,
		"mirror 0\n"
		"output HDMI-A-1 1920x1080 scale 1 nested 0 x 0 y 0 main 1\n"
		"mode 1920x1080@60000\nmode 1280x720@60000\nmode 1280x720@50000\nmode 640x480@60000\n"
		"output DP-3 1920x1080 scale 1 nested 0 x 1920 y 0 main 0\nmode 1920x1080@60000\n"
		"output eDP-1 1366x768 scale 1 nested 0 x 3840 y 0 main 0\nmode 1366x768@60000\n"
		"output HEADLESS-1 1024x768 scale 1 nested 0 x 0 y 0 main 0\n") &&
		write(root + "/drm/card1-HDMI-A-1/status", "connected") &&
		write(root + "/drm/card1-DP-3/status", "connected") &&
		write(root + "/drm/card0-eDP-1/intel_backlight/max_brightness", "1000") &&
		write(root + "/backlight/intel_backlight/max_brightness", "1000") &&
		write(root + "/backlight/intel_backlight/brightness", "500") &&
		write(root + "/backlight/acpi_video0/max_brightness", "84") &&
		write(root + "/backlight/acpi_video0/brightness", "84") &&
		write(root + "/backlight/acpi_video0/type", "firmware") &&
		write(root + "/ddc", "40"), "create outputs, DRM connectors, backlights and a DDC monitor");

	DisplayControls displays(nullptr, outputs, root + "/drm", root + "/backlight", ddcutil);
	QString error;
	displays.failed = [&](const QString &message) { error = message; };
	displays.refresh(false);
	check(displays.displays.size() == 4, "read every published output");
	const CollarDisplay *hdmi = displays.display("HDMI-A-1");
	check(hdmi && hdmi->modes == QList<QSize>({ { 640, 480 }, { 1280, 720 }, { 1920, 1080 } }),
		"modes are deduplicated across refresh rates and sorted");
	check(displays.label("HDMI-A-1") == "Display 1: HDMI-A-1 (Main)" &&
		displays.label("eDP-1") == "Display 3: Built-in Display (eDP-1)", "menus can name each display");
	const CollarDisplay *builtIn = displays.display("eDP-1");
	check(builtIn && builtIn->backend == CollarDisplay::Backlight && builtIn->backlight == "intel_backlight" &&
		builtIn->brightness == 50, "built-in panel uses its connector's backlight, not a firmware duplicate");
	const CollarDisplay *headless = displays.display("HEADLESS-1");
	check(headless && headless->brightness < 0 && headless->brightnessError.contains("no hardware"),
		"a display with no DRM connector never gets a phantom brightness control");
	check(headless && headless->modes.contains(QSize(1024, 768)) && headless->modes.size() > 1,
		"a modeless (nested) display offers the Monitors panel's sizes");
	check(read(root + "/ddc.log").isEmpty(), "reading outputs alone doesn't probe DDC/CI");

	displays.refresh(true);
	check(until([&] { const auto *d = displays.display("HDMI-A-1"); return d && d->brightness == 40; }),
		"read an external monitor's brightness over DDC/CI");
	const CollarDisplay *dp = displays.display("DP-3");
	check(dp && dp->brightness < 0 && dp->brightnessError.contains("DDC/CI"),
		"an external monitor without DDC/CI reports why brightness is unavailable");

	displays.setBrightness("HDMI-A-1", 75);
	check(displays.busy("HDMI-A-1"), "brightness changes are asynchronous");
	check(until([&] { return !displays.busy("HDMI-A-1") && displays.display("HDMI-A-1")->brightness == 75; }) &&
		read(root + "/ddc") == "75" && error.isEmpty(), "set external brightness with setvcp and read it back");
	qputenv("FAKE_DDC_FAIL", "1");
	displays.setBrightness("HDMI-A-1", 10);
	check(until([&] { return !displays.busy("HDMI-A-1"); }) && error.contains("Setting value failed") &&
		read(root + "/ddc") == "75", "surface a monitor's DDC/CI refusal");
	qunsetenv("FAKE_DDC_FAIL");
	error.clear();
	displays.setBrightness("DP-3", 50);
	check(!error.isEmpty() && !displays.busy("DP-3"), "refuse brightness on a monitor that can't do it");
	error.clear();

	displays.setBrightness("eDP-1", 25);
	check(until([&] { return !displays.busy("eDP-1"); }) && error.isEmpty() &&
		login.device == "intel_backlight" && read(login.brightnessPath) == "250" &&
		displays.display("eDP-1")->brightness == 25, "set built-in brightness through logind");

	QString problem;
	check(!displays.setResolution("HDMI-A-1", QSize(1234, 567), &problem) && !problem.isEmpty(),
		"reject a resolution the display doesn't list");
	check(displays.setResolution("HDMI-A-1", QSize(1280, 720), &problem), "switch a display's resolution");
	char value[64] = {};
	check(pl_setting("display.HDMI-A-1.resolution", value, sizeof(value)) && QByteArray(value) == "1280x720",
		"resolution goes to the compositor's per-display setting");

	bool changed = false;
	displays.changed = [&] { changed = true; };
	check(write(outputs, "mirror 0\noutput HDMI-A-1 1280x720 scale 1 nested 0 x 0 y 0 main 1\n"
		"mode 1920x1080@60000\nmode 1280x720@60000\n"),
		"compositor republishes its outputs");
	check(until([&] { return changed && displays.display("HDMI-A-1") &&
		displays.display("HDMI-A-1")->size == QSize(1280, 720) && displays.displays.size() == 1; }),
		"pick up the new resolution and removed displays");

	DisplayControls missing(nullptr, outputs, root + "/drm", root + "/backlight", root + "/no-ddcutil");
	missing.refresh(true);
	check(until([&] { const auto *d = missing.display("HDMI-A-1");
		return d && d->brightnessError.contains("ddcutil isn't installed"); }),
		"explain that external brightness needs ddcutil");
	return failures ? 1 : 0;
}
