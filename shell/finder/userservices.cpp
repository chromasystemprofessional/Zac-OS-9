#include "userservices.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QEventLoop>
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
	p.start(exe, QStringList{ "--user", "--no-pager", "--no-ask-password" } + args);
	QElapsedTimer clock;
	clock.start();
	while (p.state() == QProcess::Starting && clock.elapsed() < 3000) {
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents |
			QEventLoop::WaitForMoreEvents, 100);
	}
	if (p.state() == QProcess::NotRunning && p.error() == QProcess::FailedToStart) {
		if (err) {
			*err = "systemctl --user could not be started: " + p.errorString();
		}
		return false;
	}
	clock.restart();
	while (p.state() != QProcess::NotRunning && clock.elapsed() < timeoutMs) {
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents |
			QEventLoop::WaitForMoreEvents, 100);
	}
	if (p.state() != QProcess::NotRunning) {
		p.kill();
		clock.restart();
		while (p.state() != QProcess::NotRunning && clock.elapsed() < 1000) {
			QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents |
				QEventLoop::WaitForMoreEvents, 100);
		}
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
	const QString normalized = unit.toLower();
	for (const char *prefix : kEssentialPrefixes) {
		if (normalized.startsWith(QLatin1String(prefix))) {
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
		const QString normalizedFragment = QDir::cleanPath(fragment);
		const bool generated = normalizedFragment.startsWith("/run/systemd/generator") ||
			normalizedFragment.startsWith("/run/systemd/transient") ||
			normalizedFragment.contains("/systemd/generator.") ||
			normalizedFragment.contains("/systemd/transient/");
		if (s.manageable && (fragment.isEmpty() || generated)) {
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

UserServiceQuery userServices() {
	UserServiceQuery result;
	QString files;
	if (!runSystemctl({ "list-unit-files", "--type=service", "--no-legend", "--plain" },
			&files, &result.error)) {
		return result;
	}
	QStringList units;
	for (const QString &line : files.split('\n', Qt::SkipEmptyParts)) {
		const QString unit = line.section(QRegularExpression("\\s+"), 0, 0);
		if (unit.endsWith(".service") && !unit.contains('@') && units.size() < kMaxUnits) {
			units << unit;
		}
	}
	if (units.isEmpty()) {
		result.success = true;
		return result;
	}
	QString show;
	if (!runSystemctl(QStringList{ "show", "-p", "Id,Description,ActiveState,"
			"UnitFileState,FragmentPath" } + units, &show, &result.error, 15000)) {
		return result;
	}
	result.services = parseUserServices(files, show);
	result.success = true;
	return result;
}

bool userServiceAct(const QString &unit, UserServiceAction action, QString *error) {
	QString reason;
	if (!userServiceUnitSafe(unit, &reason)) {
		if (error) {
			*error = reason + ".";
		}
		return false;
	}
	const UserServiceQuery current = userServices();
	if (!current.success) {
		if (error) {
			*error = current.error;
		}
		return false;
	}
	const auto it = std::find_if(current.services.cbegin(), current.services.cend(),
		[&unit](const UserService &service) { return service.unit == unit; });
	if (it == current.services.cend() || !it->manageable) {
		if (error) {
			*error = it == current.services.cend() ?
				"The service is no longer listed as a manageable user service." :
				(it->reason + ".");
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

void userServicesAsync(QObject *receiver, std::function<void(UserServiceQuery)> done) {
	QPointer<QObject> guard(receiver);
	auto *thread = QThread::create([guard, done = std::move(done)]() mutable {
		UserServiceQuery result = userServices();
		if (!guard) {
			return;
		}
		QMetaObject::invokeMethod(guard.data(),
			[guard, done = std::move(done), result = std::move(result)]() mutable {
				if (guard) {
					done(std::move(result));
				}
			}, Qt::QueuedConnection);
	});
	QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
	thread->start();
}

void userServiceActAsync(QObject *receiver, const QString &unit, UserServiceAction action,
		std::function<void(bool, const QString &)> done) {
	QPointer<QObject> guard(receiver);
	auto *thread = QThread::create([guard, unit, action, done = std::move(done)]() mutable {
		QString error;
		const bool success = userServiceAct(unit, action, &error);
		if (!guard) {
			return;
		}
		QMetaObject::invokeMethod(guard.data(),
			[guard, done = std::move(done), success, error]() mutable {
				if (guard) {
					done(success, error);
				}
			}, Qt::QueuedConnection);
	});
	QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
	thread->start();
}
