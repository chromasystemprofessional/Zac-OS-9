/* User-service parsing and the safety policy. */

#include <QCoreApplication>
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
		"gen.service enabled enabled\ntemplate@.service disabled enabled\nbad;rm.service enabled enabled\n";
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
		block("unlisted.service", "enabled", "/x") + "\n";
	const QList<UserService> v = parseUserServices(files, show);
	check(find(v, "backup.service") && find(v, "backup.service")->manageable, "user service is manageable");
	check(find(v, "syncer.service") && find(v, "syncer.service")->manageable, "disabled service is manageable");
	check(!find(v, "pipewire.service")->manageable, "essential audio service is read-only");
	check(!find(v, "zacos9-shell.service")->manageable, "ZacOS services are read-only");
	check(!find(v, "dbus.service")->manageable, "static units are read-only");
	check(!find(v, "gen.service")->manageable, "generated units are read-only");
	check(!find(v, "unlisted.service"), "only listed units are shown");
	check(!userServiceUnitSafe("bad;rm.service") && !userServiceUnitSafe("a@b.service") &&
		!userServiceUnitSafe("--now") && !userServiceUnitSafe("dbus.service"), "unsafe unit names are refused");
	QString error;
	check(!userServiceAct("pipewire.service", UserServiceAction::Stop, &error) && !error.isEmpty(),
		"acting on an essential service is refused before systemctl runs");
	return failures ? 1 : 0;
}
