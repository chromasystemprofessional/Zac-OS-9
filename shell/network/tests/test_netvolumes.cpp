/*
 * Names and mount points of AFP volumes (files/netvolumes.cpp): a volume
 * goes by its shared folder's name, as on Mac OS 9, and always mounts in the
 * same place, so reconnecting never makes "NAME (2)", "NAME (3)", ...
 */
#include <QCoreApplication>
#include <QDir>
#include <QFile>
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

	/* Mount points: DIR/SERVER/ID/NAME, NAME the disk's name. */
	const QString adam = afpMountPointIn("/m", "192.168.2.110", "adam's home", "adam");
	check(adam.startsWith("/m/192.168.2.110/") && adam.endsWith("/adam") &&
		adam.count('/') == 4, "mounted at DIR/SERVER/ID/NAME, its folder named as the disk is");
	check(adam == afpMountPointIn("/m", "192.168.2.110", "adam's home", "adam") &&
		afpMountPointIn("/m", "Server.local", "Projects", "Projects") ==
			afpMountPointIn("/m", "server.local", "Projects", "Projects"),
		"the same volume always mounts in the same place, however its server's name is cased");
	check(afpMountPointIn("/m", "a", "Projects", "Projects") !=
		afpMountPointIn("/m", "b", "Projects", "Projects"),
		"the same name on two servers mounts in two places");
	const QString slashed = afpMountPointIn("/m", "srv", "a/b", "a/b");
	const QString dashed = afpMountPointIn("/m", "srv", "a-b", "a-b");
	check(slashed.endsWith("/a-b") && dashed.endsWith("/a-b") && slashed != dashed,
		"\"a/b\" and \"a-b\" both read a-b, but never share a place");
	check(adam != afpMountPointIn("/m", "192.168.2.110", "adam", "adam"),
		"nor does a home volume shown as \"adam\" and a volume called adam");
	check(afpMountPointIn("/m", "", "..", "..").startsWith("/m/Server/") &&
		afpMountPointIn("/m", "", "..", "..").endsWith("/Untitled"),
		"names that aren't folder names are made safe");

	/* Which volume a mount is: from its source, exactly. */
	QString server, volume;
	check(afpMountSource("afp://127.0.0.1:5548/Mac #1?/x", &server, &volume) &&
		server == "127.0.0.1:5548" && volume == "Mac #1?/x",
		"a mount's source gives its server (and port) and volume, # and ? and / too");
	check(!afpMountSource("afp://srv", &server, &volume) && !afpMountSource("afp:///v", &server, &volume) &&
		!afpMountSource("smb://srv/v", &server, &volume), "other sources name no AFP volume");
	auto mount = [](const QString &path, const QString &source) {
		NetVolume v;
		v.path = path;
		afpMountSource(source, &v.server, &v.share);
		return v;
	};
	check(afpMountIs(mount("/x", "afp://SRV/a_b"), "srv", "a,b") &&
		!afpMountIs(mount("/x", "afp://srv/a/b"), "srv", "a-b") &&
		!afpMountIs(mount("/x", "afp://srv:5548/a"), "srv", "a"),
		"a mount is a volume only if its server and name match");

	/* Reusing a mount: only the volume asked for, at its place or the old one. */
	const QString legacy = "/m/srv/a-b";
	bool taken = true;
	std::vector<NetVolume> live = { mount(legacy, "afp://srv/a/b") };
	check(afpMountAmong(live, dashed, legacy, "srv", "a-b", &taken) == -1 && !taken,
		"\"a/b\" mounted where 0.1.50 put it isn't taken for \"a-b\"");
	check(afpMountAmong(live, slashed, legacy, "srv", "a/b", &taken) == 0 && !taken,
		"but is found again as itself, so it isn't mounted twice");
	live = { mount(dashed, "afp://srv/a/b") };
	check(afpMountAmong(live, dashed, legacy, "srv", "a-b", &taken) == -1 && taken,
		"a different volume at a volume's place is never reused or mounted over");
	live = { mount(legacy, "afp://srv/a-b"), mount(dashed, "afp://srv/a-b") };
	check(afpMountAmong(live, dashed, legacy, "srv", "a-b", &taken) == 1 && !taken,
		"its own place wins over the old one");
	NetVolume smb = mount(dashed, "afp://srv/a-b");
	smb.kind = NetVolume::SMB;
	live = { smb };
	check(afpMountAmong(live, dashed, legacy, "srv", "a-b", &taken) == -1, "an SMB share isn't an AFP mount");

	/* Reconnecting: leftovers from earlier connections go, the place stays. */
	QTemporaryDir home;
	qputenv("XDG_DATA_HOME", home.path().toUtf8());
	const QString mounts = home.path() + "/zacos9/mounts";
	QDir().mkpath(mounts + "/adam's home (2)");
	QDir().mkpath(mounts + "/192.168.2.110/adam");
	QDir().mkpath(mounts + "/old-server/0123456789abcdef/Stuff");
	QFile note(mounts + "/old-server/0123456789abcdef/Stuff/note");
	note.open(QIODevice::WriteOnly);
	note.close();
	bool mounted = true;
	const QString place = afpMountPointFor("192.168.2.110", "adam's home", "adam", &mounted);
	check(place == afpMountPointIn(mounts, "192.168.2.110", "adam's home", "adam") && !mounted,
		"the same place as last time, not mounted");
	check(!QDir(mounts + "/adam's home (2)").exists() && !QDir(mounts + "/192.168.2.110/adam").exists(),
		"old leftovers, numbered or from 0.1.50, are removed");
	check(note.exists(), "a folder with something in it is left alone");
	return fails ? 1 : 0;
}
