#pragma once

/*
 * What's currently mounted from the network: read from the filesystem
 * itself (an AFP mount through mountinfo, an SMB one from gvfs's own
 * runtime directory), not from any registry of our own — so it can
 * never drift from reality, and a volume unmounted by hand (fusermount3
 * -u, or gio mount -u, typed at a terminal) disappears from the Finder
 * exactly as if Put Away had done it.
 */

#include <QString>
#include <QByteArray>
#include <vector>

struct NetVolume {
	enum Kind { AFP, SMB };
	Kind kind = AFP;
	QString name; /* the volume or share name: AFP, the mount point's own
	               * basename; SMB, parsed from gvfs's mount name */
	QString server, share; /* share is the AFP volume name too, for unmounting */
	QString path; /* where it's mounted: open it, eject it */
};

/* Every AFP and SMB volume mounted right now. */
std::vector<NetVolume> netVolumes();
QString netVolumeMountInfoPath(QByteArray path);

/* Where an AFP volume should be mounted: under this account's own data
 * directory, named after the volume, with " (2)" and so on if that name
 * is already taken by another live mount (two servers can each have a
 * volume called the same thing). Creates nothing; `afpMount` does that. */
QString afpMountPointFor(const QString &volumeName);

/* Unmounts a volume found by netVolumes(): fusermount3 -u for AFP,
 * gio mount -u for SMB. */
bool netVolumeEject(const NetVolume &v, QString *error);
