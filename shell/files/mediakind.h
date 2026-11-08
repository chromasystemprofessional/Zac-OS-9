#pragma once

/*
 * What kind of medium a local block device holds.
 *
 * UDisks2 knows the drive (optical, floppy, Zip, USB) but GIO's volume
 * names are not enough to draw it: the Finder needs the media kind, not
 * just the mount point. The udev database already carries the drive and
 * optical-track properties, so this classification is read-only and does
 * not open a device (a floppy drive must not be spun up just to ask).
 */

#include <QMap>
#include <QString>

enum class MediaKind {
	Disk,
	Usb,
	OpticalData,
	OpticalAudio,
	OpticalVideo,
	OpticalBlank,
	Floppy,
	Zip,
	Jaz,
	LoopImage,
};

/* Classify from udev's E: properties for a block device. `kernelName` is
 * the /sys/class/block name (sda1, sr0, fd0, loop0p1); `loopBacking` is
 * non-empty for a loop device. Kept pure so the table is testable without
 * a real drive. */
MediaKind classifyMedia(const QMap<QString, QString> &properties,
	const QString &kernelName = {}, const QString &loopBacking = {});

/* Read /run/udev/data for `device` and, for a loop partition, its parent;
 * fill `backingImage` with the loop's backing file when there is one.
 * `device` is a /dev path from GIO, not arbitrary user input. */
MediaKind mediaKindForDevice(const QString &device, QString *backingImage = nullptr);

/* A stable name for logs and tests ("optical-data", "floppy", ...). */
QString mediaKindName(MediaKind kind);
