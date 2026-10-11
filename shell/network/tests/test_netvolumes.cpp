/*
 * Names and mount points of AFP volumes (network/netvolumes.cpp): a volume
 * goes by its shared folder's name, as on Mac OS 9, and always mounts in the
 * same place, so reconnecting never makes "NAME (2)", "NAME (3)", ...
 */
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <cstdio>

#include "netvolumes.h"

static int fails;

static void check(bool ok, const char *what) {
	printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	check(afpVolumeDisplayName("adam's home", "adam") == "adam", "Netatalk's home volume is the folder");
	check(afpVolumeDisplayName("Adam's Home", "adam") == "Adam" &&
		afpVolumeDisplayName(QString::fromUtf8("adam’s home"), "adam") == "adam",
		"whatever its case or apostrophe");
	check(afpVolumeDisplayName("adam's home", "") == "adam's home" &&
		afpVolumeDisplayName("bob's home", "adam") == "bob's home",
		"someone else's home, or a guest, keeps the server's name");
	check(afpVolumeDisplayName("Projects", "adam") == "Projects", "a shared folder keeps its name");

	check(afpMountPointIn("/m", "192.168.2.110", "adam") == "/m/192.168.2.110/adam",
		"mounted at DIR/SERVER/NAME");
	check(afpMountPointIn("/m", "a", "Projects") != afpMountPointIn("/m", "b", "Projects"),
		"the same name on two servers mounts in two places");
	check(afpMountPointIn("/m", "srv", "a/b") == "/m/srv/a-b" &&
		afpMountPointIn("/m", "", "..") == "/m/Server/Untitled",
		"names that aren't folder names are made safe");

	/* Reconnecting: leftovers from earlier connections go, the place stays. */
	QTemporaryDir home;
	qputenv("XDG_DATA_HOME", home.path().toUtf8());
	const QString mounts = home.path() + "/zacos9/mounts";
	QDir().mkpath(mounts + "/adam's home (2)");
	QDir().mkpath(mounts + "/192.168.2.110/adam");
	QDir().mkpath(mounts + "/old-server/Stuff/kept");
	bool mounted = true;
	const QString place = afpMountPointFor("192.168.2.110", "adam's home", "adam", &mounted);
	check(place == mounts + "/192.168.2.110/adam" && !mounted, "the same place as last time, not mounted");
	check(!QDir(mounts + "/adam's home (2)").exists(), "an old numbered leftover is removed");
	check(QDir(mounts + "/old-server/Stuff/kept").exists(), "a folder with something in it is left alone");
	return fails ? 1 : 0;
}
