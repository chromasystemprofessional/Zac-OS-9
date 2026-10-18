/* Disk Vet's reading of UDisks2's objects (udisks.cpp). */
#include <QCoreApplication>
#include <cstdio>

#include "udisks.h"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

int main(int argc, char **argv) {
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
	CHECK(volumes[1].name == "sdb2" && volumes[1].device == "/dev/sdb2" && volumes[1].canRepair);
	CHECK(drives.contains(drive) && !drives[drive].failing);
	CHECK(driveHealth(drives[drive]) == "WDC WD20: healthy, 30 °C, 12,598 hours powered on.");
	drives[drive].failing = true;
	CHECK(driveHealth(drives[drive]).contains("FAILING"));
	std::puts("ok: Disk Vet reads volumes and drive health from UDisks");
	return 0;
}
