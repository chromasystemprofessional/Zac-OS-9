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

/* The name a mounted AFP volume goes by on the desktop: the shared folder's
 * own, as on Mac OS 9 - which showed two volumes of the same name side by
 * side rather than numbering them. Netatalk names a home folder volume
 * "NAME's home" by default; logged in as NAME, that is the folder NAME. */
QString afpVolumeDisplayName(const QString &volume, const QString &user);

/* Where an AFP volume is mounted: DIR/SERVER/NAME (NAME from
 * afpVolumeDisplayName), so the same volume always gets the same place and
 * volumes of the same name on two servers never collide. */
QString afpMountPointIn(const QString &dir, const QString &server, const QString &name);

/* The mount point for a volume, under this account's own data directory.
 * Empty folders left by earlier connections (a logout or a dropped server
 * skips the cleanup) are removed, and a dead mount still in the way is
 * unmounted. *mounted is set if the volume is already mounted there.
 * Creates nothing; `afpMount` does that. */
QString afpMountPointFor(const QString &server, const QString &volume, const QString &user,
	bool *mounted);

/* Unmounts a volume found by netVolumes(): fusermount3 -u for AFP,
 * gio mount -u for SMB. */
bool netVolumeEject(const NetVolume &v, QString *error);
