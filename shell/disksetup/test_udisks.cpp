/* Disk Vet's reading of UDisks2's objects (udisks.cpp), and which volumes
 * mount by themselves (mountpolicy.cpp). */
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

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
	return 0;
}
