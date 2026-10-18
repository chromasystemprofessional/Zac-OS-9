#pragma once

/*
 * Disk Setup's partitioning (gap plan 7.6): the drives as UDisks2 shows
 * them, and changes queued until Apply. Nothing here touches a disk; the
 * window runs the queue through UDisks2 (main.cpp), one step at a time.
 *
 * Safety: the drive the live system runs from is never changed; nor is a
 * partition in use (mounted, or swap); nor the running system's drive as a
 * whole (its other, unused partitions may be).
 */

#include <QList>
#include <QString>
#include <QStringList>

#include "udisks.h"

struct DiskPart {
	QString block;     /* its object; empty while only planned */
	QString device;    /* /dev/sda2 */
	int number = 0;
	qulonglong offset = 0, size = 0;
	QString type, label; /* filesystem (ext4, vfat, ntfs, swap...) and name */
	QStringList mountPoints;
	bool efi = false;  /* the EFI System Partition: what the computer starts up from */
	bool inUse() const { return !mountPoints.isEmpty() || type == "swap"; }
};

struct DiskDrive {
	QString drive, block, device, model;
	QString table;     /* "gpt", "dos", or "" (none) */
	qulonglong size = 0;
	QList<DiskPart> parts; /* by offset */
	bool live = false;     /* the live system runs from it */
	bool system = false;   /* the running system's root is on it */
};

struct DiskGap {
	qulonglong offset = 0, size = 0;
};

static constexpr qulonglong MiB = 1024 * 1024;

/* Every drive with a whole-disk block device, by device name. */
QList<DiskDrive> parseDrives(const UDisksObjects &objects);
/* Unused space at least 8 MiB long, 1 MiB aligned, leaving the first
 * and last MiB to the partition table. */
QList<DiskGap> freeGaps(const DiskDrive &drive);

struct DiskOp {
	enum Kind { NewTable, Delete, Create, Format, Resize } kind = Create;
	QString tableType;     /* NewTable: gpt or dos */
	int part = -1;         /* Delete, Format, Resize: the partition's index */
	qulonglong offset = 0, size = 0; /* Create: where; Resize: the new size */
	QString fsType, label; /* Create, Format */
	QString describe(const DiskDrive &drive) const;
};

/* The formats Disk Setup makes, and those it can resize. */
QStringList diskFormats();
bool resizable(const QString &fsType);
/* Why `op` can't be done to `drive` as planned so far, or "". */
QString refusal(const DiskDrive &drive, const DiskOp &op);
/* The drive as it will be once `op` is done. */
DiskDrive planned(DiskDrive drive, const DiskOp &op);
/* What Apply will erase: for its confirmation. */
QStringList erasedBy(const DiskDrive &drive, const QList<DiskOp> &ops);
/* The layout bar: each piece's width, proportional but at least `least`
 * pixels, all of them filling `width`. */
QList<int> barWidths(const QList<qulonglong> &sizes, int width, int least = 8);
/* "1.8 TB" and the like. */
QString sizeText(qulonglong bytes);
