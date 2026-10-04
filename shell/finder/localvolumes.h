#pragma once

/*
 * Locally-mounted storage volumes shown as disk icons on the desktop.
 *
 * Covers block-device mounts (internal drives, USB sticks, SD cards,
 * optical discs) but not /, network mounts (AFP/SMB — those are in
 * netvolumes.h), or virtual filesystems (tmpfs, proc, squashfs, …).
 * Uses GVolumeMonitor, so only mounts that have a real GVolume backing
 * are shown; that naturally excludes most kernel pseudo-filesystems.
 */

#include <QString>
#include <functional>
#include <vector>

struct LocalVolume {
	QString name;      /* what the Finder shows */
	QString path;      /* mount point */
	bool ejectable;    /* can be ejected (USB, optical, SD) */
};

/* Every real storage volume mounted right now, except / and network mounts. */
std::vector<LocalVolume> localVolumes();

/* Call `f` whenever a volume is mounted or unmounted. Safe to call more
 * than once; every callback is kept. */
void localVolumesOnChange(std::function<void()> f);

/* Mounts every drive volume that isn't mounted yet, as Mac OS 9 put every
 * disk on the desktop at startup, and from then on each one that appears
 * (a USB stick, a disc). Each volume is tried once: one that can't be
 * mounted (a Windows partition left in use by Fast Startup, an encrypted
 * one) is logged and left alone. Internal drives need polkit's
 * filesystem-mount-system, which 50-zacos9-mount.rules grants to
 * administrators at the computer. */
void localVolumesMountAll();

/* Ejects or unmounts `v` asynchronously. Returns false immediately if
 * no matching GMount can be found; the actual unmount happens on the
 * GLib event loop — GVolumeMonitor fires mount-removed when done,
 * which will call any registered localVolumesOnChange callback. */
bool localVolumeEject(const LocalVolume &v);
