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

/* Where an AFP volume is mounted: DIR/SERVER/ID/NAME. NAME (from
 * afpVolumeDisplayName) is the disk's name on the desktop. ID comes from the
 * exact server and volume names, so volumes whose names make the same folder
 * name ("a/b" and "a-b"; "adam's home", shown as "adam", beside a volume
 * "adam") never share a place, while the same volume always gets the same
 * one, and volumes of the same name on two servers never collide. */
QString afpMountPointIn(const QString &dir, const QString &server, const QString &volume,
	const QString &name);

/* The server and volume an AFP mount's source names: "afp://SERVER/VOLUME",
 * as zacos9-afp gives it (SERVER may carry a port; VOLUME is raw text, not a
 * URL path). */
bool afpMountSource(const QString &source, QString *server, QString *volume);

/* Whether a mounted AFP volume is SERVER's VOLUME, as nearly as its source
 * can say (zacos9-afp writes a comma there as "_", and stops at 255 bytes). */
bool afpMountIs(const NetVolume &v, const QString &server, const QString &volume);

/* Which of the mounts `live` SERVER's VOLUME is already mounted as: the one
 * at `path`, or at `legacy` (where 0.1.50 and before put it), that really is
 * that volume; -1 if none. *taken is set if something else is mounted at
 * `path` - never to be reused, or mounted over. */
int afpMountAmong(const std::vector<NetVolume> &live, const QString &path, const QString &legacy,
	const QString &server, const QString &volume, bool *taken);

/* The mount point for a volume, under this account's own data directory.
 * Empty folders left by earlier connections (a logout or a dropped server
 * skips the cleanup) are removed, and a dead mount of the volume still in
 * the way is unmounted. *mounted is set if the volume is already mounted
 * (the path returned is where). Empty if a different volume is mounted in
 * its place. Creates nothing; `afpMount` does that. */
QString afpMountPointFor(const QString &server, const QString &volume, const QString &user,
	bool *mounted);

/* Unmounts a volume found by netVolumes(): fusermount3 -u for AFP,
 * gio mount -u for SMB. */
bool netVolumeEject(const NetVolume &v, QString *error);
