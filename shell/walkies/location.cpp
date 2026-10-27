#include "location.h"
#include "conf.h"

#include <QDir>
#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include "settings.h"

static QString dir() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/zacos9/Walkies";
}

static bool validName(const QString &name) {
	return !name.trimmed().isEmpty() && !name.contains('/') && !name.startsWith('.') && name.size() <= 64;
}

QStringList locationNames() {
	QStringList names;
	for (const QString &f : QDir(dir()).entryList({ "*.location" }, QDir::Files, QDir::Name | QDir::IgnoreCase)) {
		names << f.chopped(9);
	}
	return names;
}

bool readLocation(const QString &name, Location *l) {
	QFile f(dir() + '/' + name + ".location");
	if (!validName(name) || !f.open(QIODevice::ReadOnly)) {
		return false;
	}
	*l = Location();
	l->name = name;
	const KeyValueFile file = KeyValueFile::parse(f.readAll());
	l->network = file.value("network");
	l->sink = file.value("sink");
	if (file.contains("volume")) {
		l->volume = file.value("volume").toInt();
	}
	l->timezone = file.value("timezone");
	l->printer = file.value("printer");
	l->trickSet = file.value("tricks-set");
	return true;
}

bool saveLocation(const Location &l, QString *error) {
	if (!validName(l.name)) {
		if (error) {
			*error = "Use a name without slashes, 64 characters at most.";
		}
		return false;
	}
	QDir().mkpath(dir());
	KeyValueFile file;
	auto put = [&](const char *key, const QString &value) {
		if (!value.isEmpty() && !value.contains('\n')) {
			file.set(key, value);
		}
	};
	put("network", l.network);
	put("sink", l.sink);
	put("volume", l.volume >= 0 ? QString::number(l.volume) : QString());
	put("timezone", l.timezone);
	put("printer", l.printer);
	put("tricks-set", l.trickSet);
	if (!file.save(dir() + '/' + l.name.trimmed() + ".location")) {
		if (error) {
			*error = "The location couldn't be saved.";
		}
		return false;
	}
	return true;
}

bool deleteLocation(const QString &name) {
	return validName(name) && QFile::remove(dir() + '/' + name + ".location");
}

QString currentLocation() {
	char v[96];
	return pl_setting("walkies-location", v, sizeof(v)) ? QString::fromUtf8(v) : QString();
}

void setCurrentLocation(const QString &name) {
	pl_setting_set("walkies-location", name.isEmpty() ? nullptr : name.toUtf8().constData());
}

QList<WalkiesStep> applySteps(const Location &l, const QMap<QString, bool> &services) {
	QList<WalkiesStep> steps;
	if (!l.network.isEmpty()) {
		steps << WalkiesStep{ "the network", "nmcli", { "connection", "up", "id", l.network } };
	}
	if (!l.sink.isEmpty()) {
		steps << WalkiesStep{ "the sound output", "pactl", { "set-default-sink", l.sink } };
		if (l.volume >= 0) {
			steps << WalkiesStep{ "the volume", "pactl",
				{ "set-sink-volume", l.sink, QString::number(std::min(l.volume, 150)) + "%" } };
		}
	}
	if (!l.timezone.isEmpty()) {
		steps << WalkiesStep{ "the time zone", "timedatectl", { "set-timezone", l.timezone } };
	}
	if (!l.printer.isEmpty()) {
		steps << WalkiesStep{ "the default printer", "lpoptions", { "-d", l.printer } };
	}
	for (auto it = services.cbegin(); it != services.cend(); ++it) {
		steps << WalkiesStep{ "the service " + it.key(), "systemctl",
			{ "--user", it.value() ? "enable" : "disable", it.key() } };
	}
	return steps;
}

QList<WalkiesStep> captureSteps() {
	return {
		{ "the network", "nmcli", { "-t", "-f", "NAME,TYPE", "connection", "show", "--active" } },
		{ "the sound output", "pactl", { "get-default-sink" } },
		{ "the volume", "pactl", { "get-sink-volume", "@DEFAULT_SINK@" } },
		{ "the time zone", "timedatectl", { "show", "-p", "Timezone", "--value" } },
		{ "the default printer", "lpstat", { "-d" } },
	};
}

void parseCapture(const QStringList &out, Location *l) {
	/* nmcli -t: NAME:TYPE, colons in names escaped as \: ; the first that
	 * isn't the loopback or a bridge. */
	for (const QString &line : out.value(0).split('\n')) {
		const int colon = line.lastIndexOf(':');
		const QString type = line.mid(colon + 1).trimmed();
		if (colon > 0 && type != "loopback" && type != "bridge" && !type.isEmpty()) {
			l->network = line.left(colon).replace("\\:", ":");
			break;
		}
	}
	l->sink = out.value(1).trimmed();
	const QRegularExpressionMatch m = QRegularExpression("(\\d+)%").match(out.value(2));
	l->volume = m.hasMatch() ? m.captured(1).toInt() : -1;
	l->timezone = out.value(3).trimmed();
	const QRegularExpressionMatch d = QRegularExpression("destination: (\\S+)").match(out.value(4));
	l->printer = d.hasMatch() ? d.captured(1) : QString();
}

QString describe(const Location &l) {
	QStringList parts;
	if (!l.network.isEmpty()) parts << "Network: " + l.network;
	if (!l.sink.isEmpty()) parts << "Sound: " + l.sink.section('.', -1) + (l.volume >= 0 ? QString(" at %1%").arg(l.volume) : QString());
	if (!l.timezone.isEmpty()) parts << "Time zone: " + l.timezone;
	if (!l.printer.isEmpty()) parts << "Printer: " + QString(l.printer).replace('_', ' ');
	if (!l.trickSet.isEmpty()) parts << "Startup set: " + l.trickSet;
	return parts.isEmpty() ? QString("Nothing remembered.") : parts.join("  ·  ");
}
