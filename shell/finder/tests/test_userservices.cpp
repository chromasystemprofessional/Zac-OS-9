/* User-service parsing and the safety policy. */

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

#include "userservices.h"

static int failures = 0;

static void check(bool ok, const char *what) {
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		failures++;
	}
}

static const UserService *find(const QList<UserService> &v, const char *unit) {
	for (const UserService &s : v) {
		if (s.unit == unit) {
			return &s;
		}
	}
	return nullptr;
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	const QString files =
		"backup.service enabled enabled\nsyncer.service disabled enabled\n"
		"pipewire.service enabled enabled\ndbus.service static -\nzacos9-shell.service enabled enabled\n"
		"gen.service enabled enabled\ntransient.service enabled enabled\n"
		"template@.service disabled enabled\nbad;rm.service enabled enabled\n";
	auto block = [](const char *id, const char *state, const char *frag) {
		return QString("Id=%1\nDescription=D %1\nActiveState=active\nUnitFileState=%2\nFragmentPath=%3\n")
			.arg(id, state, frag);
	};
	const QString show = block("backup.service", "enabled", "/home/u/.config/systemd/user/backup.service") + "\n" +
		block("syncer.service", "disabled", "/usr/lib/systemd/user/syncer.service") + "\n" +
		block("pipewire.service", "enabled", "/usr/lib/systemd/user/pipewire.service") + "\n" +
		block("dbus.service", "static", "/usr/lib/systemd/user/dbus.service") + "\n" +
		block("zacos9-shell.service", "enabled", "/x") + "\n" +
		block("gen.service", "enabled", "") + "\n" +
		block("transient.service", "enabled", "/run/systemd/transient/transient.service") + "\n" +
		block("unlisted.service", "enabled", "/x") + "\n";
	const QList<UserService> v = parseUserServices(files, show);
	check(find(v, "backup.service") && find(v, "backup.service")->manageable, "user service is manageable");
	check(find(v, "syncer.service") && find(v, "syncer.service")->manageable, "disabled service is manageable");
	check(!find(v, "pipewire.service")->manageable, "essential audio service is read-only");
	check(!find(v, "zacos9-shell.service")->manageable, "ZacOS services are read-only");
	check(!find(v, "dbus.service")->manageable, "static units are read-only");
	check(!find(v, "gen.service")->manageable, "generated units are read-only");
	check(find(v, "transient.service") && !find(v, "transient.service")->manageable,
		"transient units are read-only");
	check(!find(v, "unlisted.service"), "only listed units are shown");
	check(!userServiceUnitSafe("bad;rm.service") && !userServiceUnitSafe("a@b.service") &&
		!userServiceUnitSafe("--now") && !userServiceUnitSafe("dbus.service"), "unsafe unit names are refused");
	QString error;
	check(!userServiceAct("pipewire.service", UserServiceAction::Stop, &error) && !error.isEmpty(),
		"acting on an essential service is refused before systemctl runs");

	QTemporaryDir tmp;
	const QString bin = tmp.path() + "/systemctl";
	QFile fake(bin);
	if (!fake.open(QIODevice::WriteOnly)) {
		check(false, "fake systemctl is created for query-error tests");
		return 1;
	}
	fake.write(
		"#!/bin/sh\n"
		"case \"$4\" in\n"
		"list-unit-files)\n"
		"  case \"$FAKE_MODE\" in fail) echo listing-failed >&2; exit 1;; esac\n"
		"  [ \"$FAKE_MODE\" = empty ] && exit 0\n"
		"  if [ \"$FAKE_MODE\" = unsafe ]; then echo 'pipewire.service enabled enabled'; "
		"else echo 'backup.service enabled enabled'; fi\n"
		"  ;;\n"
		"show)\n"
		"  [ \"$FAKE_MODE\" = showfail ] && exit 1\n"
		"  if [ \"$FAKE_MODE\" = unsafe ]; then "
		"echo 'Id=pipewire.service'; echo 'Description=Audio'; "
		"echo 'ActiveState=active'; echo 'UnitFileState=enabled'; "
		"echo 'FragmentPath=/usr/lib/systemd/user/pipewire.service'; "
		"else echo 'Id=backup.service'; echo 'Description=Backup'; "
		"echo 'ActiveState=inactive'; echo 'UnitFileState=enabled'; "
		"echo 'FragmentPath=/usr/lib/systemd/user/backup.service'; fi\n"
		"  ;;\n"
		"enable|disable|start|stop) printf '%s\\n' \"$*\" >> \"$FAKE_ACTIONS\" ;;\n"
		"*) exit 2;;\n"
		"esac\n");
	fake.close();
	QFile::setPermissions(bin, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
	qputenv("PATH", (tmp.path() + ":" + QString::fromLocal8Bit(qgetenv("PATH"))).toLocal8Bit());
	qputenv("FAKE_ACTIONS", (tmp.path() + "/actions").toUtf8());
	qputenv("FAKE_MODE", "empty");
	UserServiceQuery query = userServices();
	check(query.success && query.services.isEmpty(),
		"an empty user-service list is successful, not a listing failure");
	qputenv("FAKE_MODE", "fail");
	query = userServices();
	check(!query.success && query.services.isEmpty() && !query.error.isEmpty(),
		"systemctl listing failures remain distinct from an empty list");
	qputenv("FAKE_MODE", "showfail");
	query = userServices();
	check(!query.success && !query.error.isEmpty(),
		"a failed service detail query is reported explicitly");
	qputenv("FAKE_MODE", "safe");
	query = userServices();
	check(query.success && find(query.services, "backup.service") &&
		find(query.services, "backup.service")->manageable, "user service details are queried");
	check(userServiceAct("backup.service", UserServiceAction::Stop, &error),
		"a currently manageable service can be stopped");
	QFile actions(tmp.path() + "/actions");
	check(actions.open(QIODevice::ReadOnly) &&
		actions.readAll().contains("--user --no-pager --no-ask-password stop -- backup.service"),
		"mutations use systemctl --user with argument-separated unit names");
	qputenv("FAKE_MODE", "unsafe");
	check(!userServiceAct("pipewire.service", UserServiceAction::Stop, &error) &&
		!error.isEmpty(), "the current safety policy is revalidated immediately before mutation");
	actions.close();
	check(actions.open(QIODevice::ReadOnly) &&
		!actions.readAll().contains("stop -- pipewire.service"),
		"rejected mutations never reach systemctl");
	return failures ? 1 : 0;
}
