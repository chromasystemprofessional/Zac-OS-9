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

/* Ejects or unmounts `v` asynchronously. Returns false immediately if
 * no matching GMount can be found; the actual unmount happens on the
 * GLib event loop — GVolumeMonitor fires mount-removed when done,
 * which will call any registered localVolumesOnChange callback. */
bool localVolumeEject(const LocalVolume &v);
