#include "displaycontrols.h"

#include <QDateTime>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <algorithm>
#include <memory>
#include <unistd.h>

#include "settings.h"

static const QString LOGIN = QStringLiteral("org.freedesktop.login1");

bool CollarDisplay::internal() const {
	return name.startsWith("eDP") || name.startsWith("LVDS") || name.startsWith("DSI");
}

QString DisplayControls::defaultOutputsPath() {
	return qEnvironmentVariable("XDG_RUNTIME_DIR") + "/zacos9-outputs-" + qEnvironmentVariable("WAYLAND_DISPLAY");
}

DisplayControls::DisplayControls(QObject *parent, const QString &outputsPath, const QString &drmRoot,
		const QString &backlightRoot, const QString &ddcutil)
	: QObject(parent), m_outputsPath(outputsPath), m_drmRoot(drmRoot), m_backlightRoot(backlightRoot),
	  m_ddcutil(ddcutil), m_watcher(new QFileSystemWatcher(this)) {
	connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
		// The compositor rewrites the file; a replaced file drops out of the watch.
		QTimer::singleShot(100, this, [this] { refresh(m_hardware); });
	});
}

static bool readNumber(const QString &path, int &number) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return false;
	}
	bool ok = false;
	number = QString::fromUtf8(file.readAll()).trimmed().toInt(&ok);
	return ok;
}

static QString readText(const QString &path) {
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

void DisplayControls::readOutputs() {
	displays.clear();
	mirrored = false;
	QFile file(m_outputsPath);
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	for (const QByteArray &line : file.readAll().split('\n')) {
		const QList<QByteArray> w = line.split(' ');
		if (w.value(0) == "mirror") {
			mirrored = w.value(1) == "1";
		} else if (w.value(0) == "output" && !w.value(1).isEmpty()) {
			CollarDisplay d;
			d.name = QString::fromUtf8(w.value(1));
			const QList<QByteArray> wh = w.value(2).split('x');
			d.size = QSize(wh.value(0).toInt(), wh.value(1).toInt());
			d.nested = w.value(6) == "1";
			d.main = w.value(12) == "1";
			displays << d;
		} else if (w.value(0) == "mode" && !displays.isEmpty()) {
			const QList<QByteArray> wh = w.value(1).split('@').value(0).split('x');
			const QSize size(wh.value(0).toInt(), wh.value(1).toInt());
			if (!size.isEmpty() && !displays.last().modes.contains(size)) {
				displays.last().modes << size;
			}
		}
	}
	for (CollarDisplay &d : displays) {
		if (d.modes.isEmpty()) {
			// A nested (windowed) screen has no modes of its own; match the Monitors panel.
			d.modes = { { 640, 480 }, { 800, 600 }, { 832, 624 }, { 1024, 768 }, { 1152, 870 },
				{ 1280, 720 }, { 1280, 800 }, { 1280, 1024 }, { 1440, 900 }, { 1600, 900 }, { 1920, 1080 } };
		}
		if (!d.size.isEmpty() && !d.modes.contains(d.size)) {
			d.modes << d.size;
		}
		std::sort(d.modes.begin(), d.modes.end(), [](const QSize &a, const QSize &b) {
			return a.width() != b.width() ? a.width() < b.width() : a.height() < b.height();
		});
	}
}

void DisplayControls::readBacklight(CollarDisplay &d) {
	d.backend = CollarDisplay::NoBrightness;
	d.brightness = -1;
	d.brightnessError = "This built-in display has no adjustable backlight.";
	QStringList candidates;
	const QDir drm(m_drmRoot);
	for (const QString &connector : drm.entryList({ "card*-" + d.name }, QDir::Dirs | QDir::NoDotAndDotDot)) {
		const QDir dir(drm.filePath(connector));
		for (const QString &child : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
			if (QFile::exists(dir.filePath(child) + "/max_brightness")) {
				candidates << child;
			}
		}
	}
	if (candidates.isEmpty()) {
		// Some drivers don't parent the backlight to its connector: prefer the most direct control.
		const QDir dir(m_backlightRoot);
		QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System, QDir::Name);
		for (const char *type : { "raw", "platform", "firmware" }) {
			for (const QString &entry : entries) {
				if (readText(dir.filePath(entry) + "/type") == type) {
					candidates << entry;
				}
			}
		}
	}
	for (const QString &name : candidates) {
		int maximum = 0, value = 0;
		const QString path = m_backlightRoot + "/" + name;
		if (!readNumber(path + "/max_brightness", maximum) || maximum <= 0 ||
				!readNumber(path + "/brightness", value) || value < 0 || value > maximum) {
			d.brightnessError = "Couldn't read display backlight " + name + ".";
			continue;
		}
		d.backend = CollarDisplay::Backlight;
		d.backlight = name;
		d.maxBrightness = maximum;
		d.brightness = qRound(100.0 * value / maximum);
		d.brightnessError.clear();
		return;
	}
}

void DisplayControls::applyDdc(CollarDisplay &d) {
	d.backend = CollarDisplay::Ddc;
	d.bus = m_buses.value(d.name, -1);
	d.brightness = -1;
	if (d.bus < 0) {
		d.brightnessError = m_detecting || !m_detected ? "Looking for DDC/CI brightness control..."
			: m_ddcErrors.value(QString(), "This display doesn't offer brightness control over DDC/CI.");
		return;
	}
	const auto value = m_ddcValues.value(d.name, { -1, 0 });
	d.maxBrightness = value.second;
	if (value.second > 0) {
		d.brightness = qRound(100.0 * value.first / value.second);
		d.brightnessError.clear();
	} else {
		d.brightnessError = m_ddcErrors.value(d.name, "Reading brightness over DDC/CI...");
	}
}

void DisplayControls::refresh(bool hardware) {
	m_hardware = m_hardware || hardware;
	if (QFile::exists(m_outputsPath) && !m_watcher->files().contains(m_outputsPath)) {
		m_watcher->addPath(m_outputsPath);
	}
	readOutputs();
	QStringList ddc;
	const QDir drm(m_drmRoot);
	for (CollarDisplay &d : displays) {
		if (d.internal()) {
			readBacklight(d);
		} else if (!d.nested && !drm.entryList({ "card*-" + d.name }, QDir::Dirs).isEmpty()) {
			ddc << d.name;
		} else {
			d.brightnessError = "This display has no hardware brightness control.";
		}
	}
	if (ddc != m_ddcOutputs) {
		m_ddcOutputs = ddc;
		m_detected = false;
	}
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (hardware && !ddc.isEmpty() && !m_detecting &&
			(!m_detected || (m_buses.isEmpty() && now - m_lastDetect > 60000))) {
		detect();
	} else if (hardware && m_detected && now - m_lastDdcRead > 30000) {
		m_lastDdcRead = now;
		for (const QString &name : m_buses.keys()) {
			if (ddc.contains(name) && !m_busy.contains(name)) {
				readDdc(name);
			}
		}
	}
	for (CollarDisplay &d : displays) {
		if (ddc.contains(d.name)) {
			applyDdc(d);
		}
	}
	notify();
}

const CollarDisplay *DisplayControls::display(const QString &name) const {
	for (const CollarDisplay &d : displays) {
		if (d.name == name) {
			return &d;
		}
	}
	return nullptr;
}

QString DisplayControls::label(const QString &name) const {
	for (int i = 0; i < displays.size(); ++i) {
		const CollarDisplay &d = displays[i];
		if (d.name == name) {
			const QString device = d.internal() ? "Built-in Display (" + d.name + ")" : d.name;
			return displays.size() == 1 ? "Display: " + device
				: QString("Display %1: %2%3").arg(i + 1).arg(device, d.main ? " (Main)" : "");
		}
	}
	return "Display";
}

bool DisplayControls::setResolution(const QString &name, QSize size, QString *error) {
	const CollarDisplay *d = display(name);
	if (!d || !d->modes.contains(size)) {
		*error = "That resolution isn't available on this display.";
		return false;
	}
	const QByteArray key = "display." + name.toUtf8() + ".resolution";
	const QByteArray value = QString("%1x%2").arg(size.width()).arg(size.height()).toUtf8();
	if (!pl_setting_set(key.constData(), value.constData())) {
		*error = "Couldn't save the display resolution.";
		return false;
	}
	return true;
}

void DisplayControls::run(const QStringList &args,
		std::function<void(const QString &, const QString &)> done) {
	m_queue.append({ args, std::move(done) });
	if (!m_running) {
		next();
	}
}

void DisplayControls::next() {
	if (m_queue.isEmpty()) {
		m_running = false;
		return;
	}
	m_running = true;
	const Job job = m_queue.takeFirst();
	auto *process = new QProcess(this);
	auto settled = std::make_shared<bool>(false);
	auto finish = [this, process, job, settled](const QString &error) {
		if (*settled) {
			return;
		}
		*settled = true;
		const QString output = QString::fromUtf8(process->readAllStandardOutput());
		process->deleteLater();
		job.done(output, error);
		next();
	};
	connect(process, &QProcess::errorOccurred, this, [process, finish](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			finish("ddcutil isn't installed, so external display brightness is unavailable.");
		} else if (e == QProcess::Crashed && process->property("timedOut").toBool()) {
			finish("The display didn't answer its DDC/CI brightness request in time.");
		}
	});
	connect(process, &QProcess::finished, this, [process, finish](int code, QProcess::ExitStatus status) {
		if (process->property("timedOut").toBool()) {
			finish("The display didn't answer its DDC/CI brightness request in time.");
		} else if (status != QProcess::NormalExit || code != 0) {
			QString detail = QString::fromUtf8(process->readAllStandardError()).trimmed();
			finish(detail.isEmpty() ? QString("ddcutil failed with status %1.").arg(code) : detail);
		} else {
			finish(QString());
		}
	});
	QTimer::singleShot(20000, process, [process] {
		process->setProperty("timedOut", true);
		process->kill();
	});
	process->start(m_ddcutil, job.args);
}

void DisplayControls::detect() {
	m_detecting = true;
	m_lastDetect = QDateTime::currentMSecsSinceEpoch();
	run({ "detect" }, [this](const QString &output, const QString &error) {
		m_detecting = false;
		m_detected = true;
		m_buses.clear();
		m_ddcValues.clear();
		m_ddcErrors.clear();
		if (!error.isEmpty()) {
			m_ddcErrors.insert(QString(), "External display brightness is unavailable. " + error);
		} else {
			// Blocks start with an unindented "Display N"; invalid or phantom displays are skipped.
			bool valid = false;
			int bus = -1;
			QString connector;
			auto flush = [&] {
				if (valid && bus >= 0 && !connector.isEmpty()) {
					m_buses.insert(connector, bus);
				}
				valid = false;
				bus = -1;
				connector.clear();
			};
			static const QRegularExpression busLine(R"(I2C bus:\s*/dev/i2c-(\d+))");
			static const QRegularExpression drmLine(R"(DRM[ _]connector:\s*card\d+-(\S+))");
			for (const QString &line : output.split('\n')) {
				if (!line.isEmpty() && !line.at(0).isSpace()) {
					flush();
					valid = line.startsWith("Display ");
				} else if (const auto m = busLine.match(line); m.hasMatch()) {
					bus = m.captured(1).toInt();
				} else if (const auto m = drmLine.match(line); m.hasMatch()) {
					connector = m.captured(1);
				}
			}
			flush();
		}
		m_lastDdcRead = QDateTime::currentMSecsSinceEpoch();
		for (const QString &name : m_buses.keys()) {
			readDdc(name);
		}
		refresh(false);
	});
}

void DisplayControls::readDdc(const QString &name) {
	const int bus = m_buses.value(name, -1);
	if (bus < 0) {
		return;
	}
	run({ "--bus", QString::number(bus), "--brief", "getvcp", "10" },
		[this, name](const QString &output, const QString &error) {
			static const QRegularExpression vcp(R"(VCP\s+10\s+C\s+(\d+)\s+(\d+))");
			const auto m = vcp.match(output);
			const int current = m.captured(1).toInt(), maximum = m.captured(2).toInt();
			if (error.isEmpty() && m.hasMatch() && maximum > 0 && current <= maximum) {
				m_ddcValues.insert(name, { current, maximum });
				m_ddcErrors.remove(name);
			} else {
				m_ddcValues.remove(name);
				m_ddcErrors.insert(name, "This display doesn't allow brightness control over DDC/CI." +
					(error.isEmpty() ? QString() : " " + error));
			}
			refresh(false);
		});
}

void DisplayControls::notify() {
	if (changed) {
		changed();
	}
}

void DisplayControls::finished(const QString &name, const QString &error) {
	m_busy.removeAll(name);
	if (!error.isEmpty() && failed) {
		failed(error);
	}
	refresh(false);
}

void DisplayControls::setBrightness(const QString &name, int percent) {
	refresh(false);
	const CollarDisplay *d = display(name);
	QString error;
	if (!d) {
		error = "That display is no longer connected.";
	} else if (m_busy.contains(name)) {
		error = "The display is still applying a brightness change. Please try again.";
	} else if (percent < 0 || percent > 100 || d->brightness < 0 || d->maxBrightness <= 0) {
		error = "Brightness can't be changed on this display. " + d->brightnessError;
	}
	if (!error.isEmpty()) {
		if (failed) {
			failed(error);
		}
		return;
	}
	m_busy << name;
	notify();
	if (d->backend == CollarDisplay::Ddc) {
		const int value = qRound(d->maxBrightness * (percent / 100.0));
		run({ "--bus", QString::number(d->bus), "setvcp", "10", QString::number(value) },
			[this, name](const QString &, const QString &error) {
				if (!error.isEmpty()) {
					finished(name, "Couldn't set the display's brightness over DDC/CI. " + error);
					return;
				}
				readDdc(name);
				finished(name, QString());
			});
		return;
	}
	const QString device = d->backlight;
	const uint value = static_cast<uint>(std::max(1, qRound(d->maxBrightness * (percent / 100.0))));
	auto call = [this](const QString &path, const QString &interface, const QString &method,
			const QVariantList &args, std::function<void(const QDBusMessage &)> done) {
		QDBusMessage message = QDBusMessage::createMethodCall(LOGIN, path, interface, method);
		message.setArguments(args);
		auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, 5000), this);
		connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, done] {
			const QDBusMessage reply = watcher->reply();
			watcher->deleteLater();
			done(reply);
		});
	};
	auto failure = [](const QDBusMessage &reply) {
		return reply.type() == QDBusMessage::ErrorMessage ? reply.errorName() + ": " + reply.errorMessage() : QString();
	};
	call("/org/freedesktop/login1", "org.freedesktop.login1.Manager", "GetSessionByPID",
		{ QVariant::fromValue(static_cast<uint>(getpid())) },
		[this, call, failure, name, device, value](const QDBusMessage &reply) {
			const QString path = qdbus_cast<QDBusObjectPath>(reply.arguments().value(0)).path();
			if (!failure(reply).isEmpty() || path.isEmpty()) {
				finished(name, "Couldn't find the active login session. " + failure(reply));
				return;
			}
			call(path, "org.freedesktop.login1.Session", "SetBrightness",
				{ "backlight", device, QVariant::fromValue(value) },
				[this, failure, name](const QDBusMessage &reply) { finished(name, failure(reply)); });
		});
}
