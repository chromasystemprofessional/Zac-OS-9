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
	bool biosBoot = false; /* GRUB's BIOS boot partition on a GPT drive */
	bool logical = false;  /* inside an MBR drive's extended partition */
	qulonglong minSize = 0; /* the smallest it can shrink to, round its files; 0: not known yet */
	bool inUse() const { return !mountPoints.isEmpty() || type == "swap"; }
};

struct DiskGap {
	qulonglong offset = 0, size = 0;
};

/* A range a divider may be dragged in, in bytes from the drive's start. */
struct DiskSpan {
	qulonglong least = 0, most = 0;
};

struct DiskDrive {
	QString drive, block, device, model;
	QString table;     /* "gpt", "dos", or "" (none) */
	qulonglong size = 0;
	QList<DiskPart> parts; /* by offset */
	/* MBR extended partitions: not shown, but their space isn't free. */
	QList<DiskGap> containers;
	bool live = false;     /* the live system runs from it */
	bool system = false;   /* the running system's root is on it */
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
	QString partType;      /* Create: the partition's type, "" for its format's own (EfiType, BiosBootType) */
	QString describe(const DiskDrive &drive) const;
};

/* Partition types the installer makes: the EFI System partition (GPT,
 * then MBR) and GRUB's BIOS boot partition (GPT only). */
extern const QString EfiType, EfiTypeMbr, BiosBootType;

/* The formats Disk Setup makes, and those it can resize. */
QStringList diskFormats();
bool resizable(const QString &fsType);
/* "ZacOS (ext4)", "NTFS", "Haiku (BFS)"... */
QString formatName(const QString &fsType);
/* Why `op` can't be done to `drive` as planned so far, or "". */
QString refusal(const DiskDrive &drive, const DiskOp &op);
/* The drive as it will be once `op` is done. */
DiskDrive planned(DiskDrive drive, const DiskOp &op);
/* What Apply will erase: for its confirmation. */
QStringList erasedBy(const DiskDrive &drive, const QList<DiskOp> &ops);
/* The room partition `part` has on the drive as planned: from the end of
 * the one before it (or the drive's first MiB) to the start of the next
 * (or the drive's last MiB). */
DiskGap roomAround(const DiskDrive &drive, int part);
/* Where partition `part`'s end may be dragged to: a new one anywhere in its
 * room; one already on the drive only if it can be resized, and smaller
 * only as far as its files (minSize) once that's known. least == most when
 * it can't move. */
DiskSpan endLimits(const DiskDrive &drive, int part);

/* Gives partition `part` of the drive as `ops` leave it a new start and
 * size: a new partition's Create step changes; one already on the drive
 * gets a Resize step (its start stays), or loses it when back to its
 * size. False, with `ops` as it was, when the changes wouldn't all be
 * allowed then. */
bool reshape(const DiskDrive &start, QList<DiskOp> *ops, int part, qulonglong offset, qulonglong size);
/* The line under partition `part`, as the partition map drags it: where
 * it may go (its end's limits, and when a new partition follows right
 * after, that one's start too, keeping 8 MiB of it). */
DiskSpan boundaryLimits(const DiskDrive &drive, int part);
/* Moves that line to `at`: the partition's end, and a new partition's
 * start right after it with it. False, `ops` unchanged, if not allowed. */
bool moveBoundary(const DiskDrive &start, QList<DiskOp> *ops, int part, qulonglong at);
/* True when every step of `ops` is allowed on the drive as the ones
 * before it leave it. */
bool allowed(const DiskDrive &start, const QList<DiskOp> &ops);

/* Room for ZacOS 9 next to what's on a drive, for the installer. */
static constexpr qulonglong ZacosLeast = 16000 * MiB;  /* about 16 GB */
static constexpr qulonglong ZacosRoomy = 32000 * MiB;  /* what it's given when there's plenty */
struct BesidePlan {
	QList<DiskOp> ops; /* any shrinking, a boot partition it needs, then ZacOS's */
	qulonglong offset = 0; /* where ZacOS's partition starts, once they're done */
	QString why;       /* why there's no room, when ops is empty */
};
/* ZacOS in the drive's largest free space, or else in room taken from the
 * end of the partition with the most to spare, with the boot partition the
 * computer needs that the drive hasn't (EFI System for `uefi`, else BIOS
 * boot on GPT). The boot partition goes at the far end from the line
 * people will drag: before ZacOS in free space (ZacOS's own end moves),
 * after it in room taken from a partition (the line between them moves).
 * `name` is the new partition's. */
BesidePlan besidePlan(const DiskDrive &drive, bool uefi, const QString &name);

/* The layout bar: each piece's width, proportional but at least `least`
 * pixels, all of them filling `width`. */
QList<int> barWidths(const QList<qulonglong> &sizes, int width, int least = 8);
/* "1.8 TB" and the like. */
QString sizeText(qulonglong bytes);
