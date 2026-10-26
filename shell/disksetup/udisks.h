#pragma once

/*
 * Disk Vet's view of UDisks2 (org.freedesktop.UDisks2): the volumes it can
 * check, and their drives' health (SMART), from GetManagedObjects.
 */

#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QList>
#include <QMap>
#include <QString>
#include <QVariantMap>

using UDisksInterfaces = QMap<QString, QVariantMap>;
using UDisksObjects = QMap<QDBusObjectPath, UDisksInterfaces>;
Q_DECLARE_METATYPE(UDisksInterfaces)
Q_DECLARE_METATYPE(UDisksObjects)

struct VetVolume {
	QString path;      /* the block device's object */
	QString device;    /* /dev/sda1 */
	QString name;      /* its label, else the device */
	QString type;      /* ext4, vfat... */
	qulonglong size = 0;
	QStringList mountPoints;
	bool canCheck = false, canRepair = false;
	QString drive;     /* the drive's object */
	QString uuid;      /* its filesystem's, for "Mount automatically" */
	bool readOnly = false; /* mounted read-only (mountpolicy.h), set by the caller */
};

struct VetDrive {
	QString model;
	bool smart = false, failing = false;
	bool removable = false; /* USB, a card or disc: not the computer's own */
	double celsius = -1;   /* -1: unknown */
	qint64 hours = -1;
};

/* The volumes with a filesystem UDisks can check, by name, and every drive. */
void parseUDisks(const UDisksObjects &objects, QList<VetVolume> *volumes, QMap<QString, VetDrive> *drives);
/* "Samsung SSD 850 EVO: healthy, 34 °C, 12,345 hours". */
QString driveHealth(const VetDrive &drive);
