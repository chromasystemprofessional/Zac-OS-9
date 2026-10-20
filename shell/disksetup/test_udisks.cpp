/* Disk Vet's reading of UDisks2's objects (udisks.cpp), which volumes
 * mount by themselves (mountpolicy.cpp), and the programs using one (inuse.cpp). */
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <cstdio>

#include "inuse.h"
#include "mountpolicy.h"
#include "udisks.h"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

int main(int argc, char **argv) {
	QTemporaryDir home;
	qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
	QCoreApplication app(argc, argv);
	const QString drive = "/org/freedesktop/UDisks2/drives/WDC";
	UDisksObjects objects;
	objects[QDBusObjectPath(drive)] = {
		{ "org.freedesktop.UDisks2.Drive", { { "Vendor", "WDC" }, { "Model", "WD20" } } },
		{ "org.freedesktop.UDisks2.Drive.Ata", { { "SmartSupported", true }, { "SmartEnabled", true },
			{ "SmartUpdated", qulonglong(1) }, { "SmartFailing", false }, { "SmartTemperature", 303.15 },
			{ "SmartPowerOnSeconds", qulonglong(3600 * 12598) } } },
	};
	auto block = [&](const char *dev, const char *label, const char *type, bool ignore = false) {
		return UDisksInterfaces{
			{ "org.freedesktop.UDisks2.Block", { { "Device", QByteArray(dev) + '\0' }, { "IdLabel", label },
				{ "IdType", type }, { "IdUsage", "filesystem" }, { "Size", qulonglong(1000) },
				{ "IdUUID", QString(label) + "-uuid" },
				{ "HintIgnore", ignore }, { "Drive", QVariant::fromValue(QDBusObjectPath(drive)) } } },
			{ "org.freedesktop.UDisks2.Filesystem", { { "MountPoints", QVariant::fromValue(QList<QByteArray>()) } } },
		};
	};
	objects[QDBusObjectPath("/b/sdb1")] = block("/dev/sdb1", "Archive", "ntfs");
	objects[QDBusObjectPath("/b/sdb2")] = block("/dev/sdb2", "", "ext4");
	objects[QDBusObjectPath("/b/sdb3")] = block("/dev/sdb3", "Hidden", "vfat", true);
	QList<VetVolume> volumes;
	QMap<QString, VetDrive> drives;
	parseUDisks(objects, &volumes, &drives);
	CHECK(volumes.size() == 2);
	CHECK(volumes[0].name == "Archive" && volumes[0].canCheck && volumes[0].mountPoints.isEmpty());
	CHECK(volumes[0].uuid == "Archive-uuid" && !volumes[0].readOnly);
	CHECK(volumes[1].name == "sdb2" && volumes[1].device == "/dev/sdb2" && volumes[1].canRepair);
	CHECK(drives.contains(drive) && !drives[drive].failing);
	CHECK(driveHealth(drives[drive]) == "WDC WD20: healthy, 30 °C, 12,598 hours powered on.");
	drives[drive].failing = true;
	CHECK(driveHealth(drives[drive]).contains("FAILING"));
	std::puts("ok: Disk Vet reads volumes and drive health from UDisks");

	/* Every volume mounts by itself until "Mount automatically" is off. */
	CHECK(volumeMountsAutomatically("Archive-uuid") && volumeMountsAutomatically(QString()));
	setVolumeMountsAutomatically("Archive-uuid", false);
	CHECK(!volumeMountsAutomatically("Archive-uuid") && volumeMountsAutomatically("other-uuid"));
	CHECK(QFile::exists(home.path() + "/zacos9/disks.conf"));
	setVolumeMountsAutomatically("Archive-uuid", true);
	CHECK(volumeMountsAutomatically("Archive-uuid"));
	setVolumeMountsAutomatically(QString(), false);
	CHECK(volumeMountsAutomatically(QString()));
	std::puts("ok: volumes mount automatically unless that is turned off for them");

	/* Read-only, from either the mount's options or the superblock's. */
	const QString info = home.path() + "/mountinfo";
	QFile file(info);
	CHECK(file.open(QIODevice::WriteOnly));
	file.write("29 1 8:3 / / rw,relatime shared:1 - ext4 /dev/sdb3 rw,errors=remount-ro\n"
		"81 29 8:1 / /media/adam/Archive\\040and\\040Projects ro,nosuid,nodev,relatime shared:44 - ntfs3 /dev/sda1 ro,uid=1001\n"
		"82 29 8:33 / /media/adam/GAMING\\040SSD rw,nosuid,nodev,relatime shared:45 - ntfs3 /dev/sdc1 rw,uid=1001\n"
		"83 29 259:3 / /media/adam/Windows rw,relatime - ntfs3 /dev/nvme0n1p3 ro,uid=1001\n");
	file.close();
	CHECK(volumeMountedReadOnly("/dev/sda1", info));
	CHECK(!volumeMountedReadOnly("/dev/sdc1", info) && !volumeMountedReadOnly("/dev/sdb3", info));
	CHECK(volumeMountedReadOnly("/dev/nvme0n1p3", info));
	CHECK(!volumeMountedReadOnly("/dev/sdz9", info) && !volumeMountedReadOnly(QString(), info));
	std::puts("ok: volumes mounted read-only are told apart");

	/* The programs using a volume: by working directory, open file or
	 * mapped file, under the mount point itself (not a look-alike). */
	const QString proc = home.path() + "/proc", mp = "/media/adam/Windows";
	auto process = [&](const char *pid, const char *comm, const QString &cwd, const QString &fd3 = QString(),
			const QString &mapped = QString(), const char *cgroup = "app-com.example.Finder-1.scope") {
		const QString dir = proc + '/' + pid;
		QDir().mkpath(dir + "/fd");
		QFile g(dir + "/cgroup");
		g.open(QIODevice::WriteOnly);
		g.write("0::/user.slice/user-1001.slice/user@1001.service/app.slice/" + QByteArray(cgroup) + '\n');
		g.close();
		QFile c(dir + "/comm");
		c.open(QIODevice::WriteOnly);
		c.write(QByteArray(comm) + '\n');
		c.close();
		QFile::link(cwd, dir + "/cwd");
		QFile::link("/", dir + "/root");
		QFile::link("/usr/bin/" + QString(comm), dir + "/exe");
		QFile::link("/dev/pts/0", dir + "/fd/0");
		if (!fd3.isEmpty()) {
			QFile::link(fd3, dir + "/fd/3");
		}
		QFile m(dir + "/maps");
		m.open(QIODevice::WriteOnly);
		m.write("7f00 r-xp 00000000 08:03 1234                     /usr/lib/libc.so.6\n");
		if (!mapped.isEmpty()) {
			m.write("7f10 r--p 00000000 103:03 99                      " + mapped.toUtf8() + "\n");
		}
		m.close();
	};
	process("4661", "bash", mp + "/Users/adame/web-app", mp + "/Users/adame/web-app/start.sh", QString(),
		"chromasystem-lab.service");
	process("4711", "php", mp + "/Users/adame/web-app", QString(), QString(), "chromasystem-lab.service");
	process("4712", "php", mp + "/Users/adame/web-app", QString(), QString(), "chromasystem-lab.service");
	process("5000", "finder", "/home/adam", "/home/adam/Desktop/note.txt");
	process("5001", "cat", "/home/adam", mp + "-old/file"); /* a look-alike */
	process("5002", "viewer", "/home/adam", QString(), mp + "/Pictures/a.jpg (deleted)");
	process("5003", "sh", mp); /* the mount point itself */
	QDir().mkpath(proc + "/self/fd");
	QFile::link(mp, proc + "/self/cwd"); /* not a process */
	QList<Holder> holders = programsUsing(mp, proc);
	QStringList pids;
	for (const Holder &h : holders) {
		pids << QString::number(h.pid) + ':' + h.name;
	}
	pids.sort();
	/* A user service's programs go by the service; a plain program by its name. */
	CHECK(pids == QStringList({ "4661:chromasystem-lab", "4711:chromasystem-lab", "4712:chromasystem-lab",
		"5002:viewer", "5003:sh" }));
	CHECK(holders[0].unit == "chromasystem-lab.service" && holders[3].unit.isEmpty() && holders[4].unit.isEmpty());
	CHECK(holderNames(holders) == "chromasystem-lab, viewer and sh");
	CHECK(holderNames(holders.mid(3, 1)) == "viewer" && holderNames({}) == "");
	CHECK(holderNames({ holders[1], holders[2] }) == "chromasystem-lab");
	CHECK(holderNames({ holders[3], holders[4] }) == "viewer and sh");
	CHECK(programsUsing(mp + '/', proc).size() == 5); /* a trailing slash is fine */
	CHECK(programsUsing("/media/adam/Other", proc).isEmpty());
	CHECK(programsUsing("/", proc).isEmpty() && programsUsing(QString(), proc).isEmpty());
	std::puts("ok: the programs using a volume are found");
	return 0;
}
