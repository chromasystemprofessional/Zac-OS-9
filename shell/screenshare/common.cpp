#include "common.h"

#include <QCoreApplication>
#include <algorithm>
#include <cctype>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFile>
#include <QLocalSocket>
#include <QSaveFile>
#include <pwd.h>
#include <unistd.h>

#include "sharingclient.h"

namespace screenshare {

QString configDir() {
	QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
	if (base.isEmpty()) {
		base = QDir::homePath() + "/.config";
	}
	return base + "/zacos9";
}

QString runtimeDir() {
	QString base = qEnvironmentVariable("XDG_RUNTIME_DIR");
	if (base.isEmpty()) {
		base = QString("/tmp/zacos9-%1").arg(getuid());
	}
	return base + "/zacos9-screenshare";
}

QString certFile() { return configDir() + "/screensharing/cert.pem"; }
QString keyFile() { return configDir() + "/screensharing/key.pem"; }
QString hostSocket() { return runtimeDir() + "/host.sock"; }

static QString settingsFile() { return configDir() + "/screensharing.conf"; }

Settings readSettings() {
	Settings s;
	QFile f(settingsFile());
	if (f.open(QIODevice::ReadOnly)) {
		for (const QByteArray &raw : f.readAll().split('\n')) {
			const QString line = QString::fromUtf8(raw).trimmed();
			const QString key = line.section('=', 0, 0), value = line.section('=', 1);
			if (key == "ask") s.ask = value != "no";
			else if (key == "control") s.control = value != "no";
			else if (key == "sound") s.sound = value != "no";
		}
	}
	return s;
}

bool writeSettings(const Settings &s) {
	QDir().mkpath(configDir());
	QSaveFile f(settingsFile());
	if (!f.open(QIODevice::WriteOnly)) {
		return false;
	}
	f.write(QString("ask=%1\ncontrol=%2\nsound=%3\n")
		.arg(s.ask ? "yes" : "no", s.control ? "yes" : "no", s.sound ? "yes" : "no").toUtf8());
	return f.commit();
}

QString ownerName() {
	const QString env = qEnvironmentVariable("ZACOS9_SHARING_OWNER");
	if (!env.isEmpty()) {
		return env;
	}
	const passwd *pw = getpwuid(getuid());
	return pw ? QString::fromLocal8Bit(pw->pw_name) : QString();
}

/* As the File Sharing panel reads it (shell/panels/filesharing.cpp). */
QString computerName() {
	QFile f(sharingConfFile("sharing.conf"));
	if (f.open(QIODevice::ReadOnly)) {
		for (const QByteArray &raw : f.readAll().split('\n')) {
			const QString line = QString::fromUtf8(raw);
			if (line.section('=', 0, 0) == "name" && !line.section('=', 1).isEmpty()) {
				return line.section('=', 1);
			}
		}
	}
	const QString owner = ownerName();
	const passwd *pw = getpwnam(owner.toLocal8Bit().constData());
	const QString full = pw && pw->pw_gecos ? QString::fromLocal8Bit(pw->pw_gecos).section(',', 0, 0)
	                                        : QString();
	return QString("%1’s Computer").arg(full.isEmpty() ? owner : full);
}

QString certFingerprint(const QString &pemFile) {
	QFile f(pemFile);
	if (!f.open(QIODevice::ReadOnly)) {
		return QString();
	}
	const QByteArray pem = f.readAll();
	const QByteArray begin = "-----BEGIN CERTIFICATE-----", end = "-----END CERTIFICATE-----";
	const int from = pem.indexOf(begin), to = pem.indexOf(end);
	if (from < 0 || to < from) {
		return QString();
	}
	return derFingerprint(QByteArray::fromBase64(pem.mid(from + begin.size(), to - from - begin.size())));
}

QString derFingerprint(const QByteArray &der) {
	const QString hex = QString::fromLatin1(QCryptographicHash::hash(der, QCryptographicHash::Sha256).toHex().toUpper());
	QStringList groups;
	for (int i = 0; i < hex.size(); i += 4) {
		groups << hex.mid(i, 4);
	}
	return groups.join(' ');
}

QString knownHostsFile() {
	return configDir() + "/screensharing/known-hosts";
}

QString autostartFile() {
	QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
	if (base.isEmpty()) {
		base = QDir::homePath() + "/.config";
	}
	return base + "/autostart/zacos9-screenshare-host.desktop";
}

bool startsAtLogin() {
	return QFile::exists(autostartFile());
}

bool setStartsAtLogin(bool on, const QString &hostProgram) {
	if (!on) {
		return !QFile::exists(autostartFile()) || QFile::remove(autostartFile());
	}
	QDir().mkpath(QFileInfo(autostartFile()).absolutePath());
	QSaveFile f(autostartFile());
	if (!f.open(QIODevice::WriteOnly)) {
		return false;
	}
	f.write(QString("[Desktop Entry]\nType=Application\nName=Screen Sharing\n"
	                "Comment=Lets other ZacOS computers see this screen\n"
	                "Exec=%1\nOnlyShowIn=ZacOS9;\nNoDisplay=true\n").arg(hostProgram).toUtf8());
	return f.commit();
}

bool hostRequest(const QString &command, QStringList *reply, int timeoutMs) {
	QLocalSocket s;
	s.connectToServer(hostSocket());
	QElapsedTimer clock;
	clock.start();
	while (s.state() == QLocalSocket::ConnectingState && clock.elapsed() < timeoutMs) {
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents, 50);
	}
	if (s.state() != QLocalSocket::ConnectedState) {
		return false;
	}
	s.write(command.toUtf8() + '\n');
	QByteArray answer;
	/* The host closes the connection after its answer. */
	while (s.state() == QLocalSocket::ConnectedState && clock.elapsed() < timeoutMs) {
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents, 50);
		answer += s.readAll();
	}
	answer += s.readAll();
	if (reply) {
		*reply = QString::fromUtf8(answer).split('\n', Qt::SkipEmptyParts);
	}
	return !answer.isEmpty();
}

} // namespace screenshare

namespace screenshare {

/* avahi-browse -p escapes a name's bytes as \DDD (decimal) and \. */
static QString avahiUnescape(const QString &s) {
	QByteArray out;
	const QByteArray in = s.toUtf8();
	for (int i = 0; i < in.size(); i++) {
		if (in[i] == '\\' && i + 3 < in.size() + 0 && isdigit(uchar(in[i + 1])) && isdigit(uchar(in[i + 2])) &&
				isdigit(uchar(in[i + 3]))) {
			out += char(in.mid(i + 1, 3).toInt());
			i += 3;
		} else if (in[i] == '\\' && i + 1 < in.size()) {
			out += in[++i];
		} else {
			out += in[i];
		}
	}
	return QString::fromUtf8(out);
}

std::vector<SharedScreen> parseScreenBrowse(const QString &text, const QString &ownName) {
	std::vector<SharedScreen> found;
	for (const QString &line : text.split('\n')) {
		/* =;IFACE;IPv4|IPv6;NAME;_rfb._tcp;DOMAIN;HOST;ADDRESS;PORT;TXT (NAME's ; escaped) */
		QStringList f;
		QString cur;
		for (int i = 0; i < line.size(); i++) {
			if (line[i] == '\\' && i + 1 < line.size()) {
				cur += line[i];
				cur += line[++i];
			} else if (line[i] == ';' && f.size() < 9) {
				f << cur;
				cur.clear();
			} else {
				cur += line[i];
			}
		}
		f << cur;
		if (f.size() < 10 || f[0] != "=" || f[4] != "_rfb._tcp" || !f[9].contains("\"zacos=1\"")) {
			continue;
		}
		SharedScreen s;
		s.name = avahiUnescape(f[3]);
		if (s.name.isEmpty() || s.name == ownName) {
			continue;
		}
		const bool v4 = f[2] == "IPv4";
		s.address = v4 ? f[7] : f[6];
		s.port = static_cast<quint16>(f[8].toUInt());
		if (!s.port) {
			continue;
		}
		auto same = std::find_if(found.begin(), found.end(), [&](const SharedScreen &o) { return o.name == s.name; });
		if (same == found.end()) {
			found.push_back(s);
		} else if (v4) {
			*same = s; /* IPv4 rather than a .local name */
		}
	}
	std::sort(found.begin(), found.end(), [](const SharedScreen &a, const SharedScreen &b) {
		return QString::localeAwareCompare(a.name, b.name) < 0;
	});
	return found;
}

} // namespace screenshare
