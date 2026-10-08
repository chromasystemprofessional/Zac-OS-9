#include "userservices.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>

namespace {

constexpr int kMaxUnits = 300;

const char *const kEssentialPrefixes[] = {
	"dbus", "pipewire", "wireplumber", "pulseaudio", "xdg-", "dconf", "at-spi", "gvfs",
	"graphical-session", "systemd-", "zacos9", "gpg-agent", "ssh-agent", "gnome-", "plasma-",
	"polkit", "evolution", "tracker", "localsearch",
};

bool runSystemctl(const QStringList &args, QString *out, QString *err, int timeoutMs = 8000) {
	const QString exe = QStandardPaths::findExecutable("systemctl");
	if (exe.isEmpty()) {
		if (err) {
			*err = "systemd is not available.";
		}
		return false;
	}
	QProcess p;
	bool done = false;
	QObject::connect(&p, &QProcess::finished, [&done] { done = true; });
	QObject::connect(&p, &QProcess::errorOccurred, [&done](QProcess::ProcessError e) {
		done = done || e == QProcess::FailedToStart;
	});
	p.start(exe, QStringList{ "--user", "--no-pager", "--no-ask-password" } + args);
	QElapsedTimer clock;
	clock.start();
	while (!done && clock.elapsed() < timeoutMs) {
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents |
			QEventLoop::WaitForMoreEvents, 100);
	}
	if (!done) {
		p.kill();
		if (err) {
			*err = "systemd did not answer.";
		}
		return false;
	}
	if (out) {
		*out = QString::fromUtf8(p.readAllStandardOutput());
	}
	if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
		if (err) {
			*err = QString::fromUtf8(p.readAllStandardError()).trimmed();
			if (err->isEmpty()) {
				*err = "The request was not accepted.";
			}
		}
		return false;
	}
	return true;
}

} // namespace

bool userServiceUnitSafe(const QString &unit, QString *reason) {
	static const QRegularExpression valid("^[A-Za-z0-9:_.\\-]+\\.service$");
	auto no = [&](const char *why) {
		if (reason) {
			*reason = why;
		}
		return false;
	};
	if (!valid.match(unit).hasMatch()) {
		return no("Not a plain user service");
	}
	for (const char *prefix : kEssentialPrefixes) {
		if (unit.startsWith(QLatin1String(prefix))) {
			return no("Essential to the session");
		}
	}
	return true;
}

QList<UserService> parseUserServices(const QString &unitFiles, const QString &show) {
	QList<UserService> out;
	QStringList wanted;
	for (const QString &line : unitFiles.split('\n', Qt::SkipEmptyParts)) {
		const QString unit = line.section(QRegularExpression("\\s+"), 0, 0);
		if (unit.endsWith(".service") && !unit.contains('@') && wanted.size() < kMaxUnits) {
			wanted << unit;
		}
	}
	for (const QString &block : show.split("\n\n", Qt::SkipEmptyParts)) {
		UserService s;
		QString fragment;
		for (const QString &line : block.split('\n', Qt::SkipEmptyParts)) {
			const int eq = line.indexOf('=');
			const QString key = line.left(eq), value = line.mid(eq + 1);
			if (key == "Id") {
				s.unit = value;
			} else if (key == "Description") {
				s.description = value;
			} else if (key == "ActiveState") {
				s.activeState = value;
			} else if (key == "UnitFileState") {
				s.fileState = value;
			} else if (key == "FragmentPath") {
				fragment = value;
			}
		}
		if (s.unit.isEmpty() || !wanted.contains(s.unit)) {
			continue; /* only units the unit-file listing named */
		}
		s.manageable = userServiceUnitSafe(s.unit, &s.reason);
		if (s.manageable && s.fileState != "enabled" && s.fileState != "disabled") {
			s.manageable = false;
			s.reason = "Started by the system rather than by the user";
		}
		if (s.manageable && fragment.isEmpty()) {
			s.manageable = false;
			s.reason = "Generated or transient";
		}
		out << s;
	}
	std::sort(out.begin(), out.end(), [](const UserService &a, const UserService &b) {
		return a.unit.compare(b.unit, Qt::CaseInsensitive) < 0;
	});
	return out;
}

QList<UserService> userServices() {
	QString files;
	if (!runSystemctl({ "list-unit-files", "--type=service", "--no-legend", "--plain" }, &files, nullptr)) {
		return {};
	}
	QStringList units;
	for (const QString &line : files.split('\n', Qt::SkipEmptyParts)) {
		const QString unit = line.section(QRegularExpression("\\s+"), 0, 0);
		if (unit.endsWith(".service") && !unit.contains('@') && units.size() < kMaxUnits) {
			units << unit;
		}
	}
	QString show;
	if (units.isEmpty() || !runSystemctl(QStringList{ "show", "-p", "Id,Description,ActiveState,"
			"UnitFileState,FragmentPath" } + units, &show, nullptr, 15000)) {
		return {};
	}
	return parseUserServices(files, show);
}

bool userServiceAct(const QString &unit, UserServiceAction action, QString *error) {
	QString reason;
	if (!userServiceUnitSafe(unit, &reason)) {
		if (error) {
			*error = reason + ".";
		}
		return false;
	}
	const char *verb = "start";
	switch (action) {
	case UserServiceAction::EnableAtLogin: verb = "enable"; break;
	case UserServiceAction::DisableAtLogin: verb = "disable"; break;
	case UserServiceAction::Start: verb = "start"; break;
	case UserServiceAction::Stop: verb = "stop"; break;
	}
	return runSystemctl({ verb, "--", unit }, nullptr, error);
}
