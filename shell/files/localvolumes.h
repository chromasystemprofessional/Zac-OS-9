#pragma once

/*
 * Locally-mounted storage volumes shown as disk icons on the desktop.
 *
 * Covers block-device mounts (internal drives, USB sticks, SD cards,
 * optical discs) but not /, network mounts (AFP/SMB — those are in
 * netvolumes.h), or virtual filesystems (tmpfs, proc, squashfs, …).
 * Uses GVolumeMonitor plus the read-only Mac helper's mount records.
 */

#include <QString>
#include <QByteArray>
#include <QSet>
#include <functional>
#include <vector>

#include "mediakind.h"

struct LocalVolume {
	QString name;      /* what the Finder shows */
	QString path;      /* mount point */
	bool ejectable;    /* can be ejected (USB, optical, SD) */
	QString macDevice; /* read-only helper mounts, including APFS FUSE */
	bool zacos = false; /* an installed ZacOS 9 system */
	QString zacosVersion;
	/* What the medium is, from udev's drive tags. The Finder uses this
	 * to draw a CD, floppy or Zip instead of a hard disk; it is also
	 * what AutoPlay checks before treating a mount as a disc. */
	MediaKind kind = MediaKind::Disk;
	QString device;     /* /dev/sr0, /dev/fd0, /dev/sdz1, ... */
	QString drive;      /* the GDrive's device, when it has one */
	QString backingImage; /* a loop device's image file */
	bool mediaPresent = true; /* a mounted volume always has media */
	/* True when a GMount exists. An audio CD or blank disc has a GIO
	 * mount with no native path; its `path` is then a stable "media:"
	 * key, and it opens or ejects through its GVolume. */
	bool mounted = true;
	/* A volume's activation URI (cdda://..., burn://...) when it has
	 * no filesystem to mount. */
	QString activationUri;
};

bool localVolumePathShown(const QString &path);

/* Is this mount an installed ZacOS 9 system? Its Finder name comes from
 * /etc/zacos9/disk-name on that disk; version comes from the disk's dpkg
 * status. Both files are read with limits because the disk is untrusted. */
bool localVolumeZacosInfo(const QString &mountPath, QString *name = nullptr,
	QString *version = nullptr);

/* Selected mounted volume's block device, only when backed by physical USB. */
QString localVolumeUsbDevice(const QString &mountPath);

/* Every real storage volume mounted right now, except / and network mounts. */
std::vector<LocalVolume> localVolumes();
std::vector<LocalVolume> localVolumeMacNames(const QByteArray &record,
	const QString &container, const QSet<QString> &mounted);

/* Call `f` whenever a volume is mounted or unmounted. Safe to call more
 * than once; every callback is kept. */
void localVolumesOnChange(std::function<void()> f);
void localVolumesOnError(std::function<void(const QString &)> f);

/* Called with a disc's content type when AutoPlay finds no installed
 * program that can play it. The Finder offers to install a player. */
void localVolumesOnAutoplayMissing(std::function<void(const QString &)> f);

/* Mounts every drive volume that isn't mounted yet, as Mac OS 9 put every
 * disk on the desktop at startup, and from then on each one that appears
 * (a USB stick, a disc), including volumes that become mountable after their
 * initial insertion notification. GIO callbacks are serviced alongside Qt.
 * Each volume is tried once per insertion: one that can't be
 * mounted (a Windows partition left in use by Fast Startup, an encrypted
 * one) is logged and reported to the disk-initialization controller, which
 * independently excludes unsafe and non-USB storage. Internal drives need polkit's
 * filesystem-mount-system, which 50-zacos9-mount.rules grants to
 * administrators at the computer. */
void localVolumesMountAll();

/* Failed block-device mounts, excluding cancellation and authorization errors. */
void localVolumesOnMountFailed(std::function<void(const QString &)> f);

/* Retry mounting the newly initialized disk, without retrying other volumes. */
void localVolumeMountDevice(const QString &device);
void localVolumeInhibitMount(const QString &device, bool inhibit);

/* Mount an image asynchronously, returning its first mounted volume or error. */
void localVolumeMountMacImage(const QString &path,
	std::function<void(const QString &, const QString &)> completed);

/* Set up a generic disk image on a read-only loop device, mount its
 * filesystem(s), and return the first mounted path. */
void localVolumeMountImage(const QString &path,
	std::function<void(const QString &, const QString &)> completed);

/* Convert a single-data-track CUE/BIN image with bchunk, mount the
 * temporary ISO read-only, and return its first mounted path. */
void localVolumeMountCueImage(const QString &path,
	std::function<void(const QString &, const QString &)> completed);

/* Mount an unmounted local volume (for a desktop item with no filesystem
 * yet). Completes with its mount path, or an error. Audio CDs and blank
 * discs are handled by their own Finder commands, not this mount. */
void localVolumeOpen(const LocalVolume &v,
	std::function<void(const QString &, const QString &)> completed);

/* Ejects or unmounts `v` asynchronously. Returns false immediately if
 * no matching GMount can be found; the actual unmount happens on the
 * GLib event loop — GVolumeMonitor fires mount-removed when done,
 * which will call any registered localVolumesOnChange callback. */
bool localVolumeEject(const LocalVolume &v);
