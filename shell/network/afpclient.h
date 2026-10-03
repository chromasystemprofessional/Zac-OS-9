#pragma once

/*
 * The Network Browser's AFP half: platinum-afp (network/afp), run as
 * three different commands for the dialog sequence it drives —
 * info (before logging in), volumes (after), mount (the volume chosen).
 */

#include <QString>
#include <QStringList>
#include <vector>

struct AfpServerInfo {
	bool ok = false;
	QString name, machine;
	QStringList uams;
	bool guestAllowed = false;
	/* No UAM but Cleartxt Passwrd: the server can't do better, and
	 * logging in as anyone but Guest means sending the password as is.
	 * The login dialog warns before trying, the way Mac OS 9 did. */
	bool cleartextOnly = false;
	QString error; /* set when !ok */
};

/* platinum-afp info: what the login dialog is built from (the server's
 * name, whether Guest is offered, whether a cleartext warning is
 * needed) — before anyone has typed anything. */
AfpServerInfo afpServerInfo(const QString &address);

struct AfpVolume {
	QString name;
	bool hasPassword = false; /* a separate, legacy AFP volume password; see docs */
};

/* platinum-afp volumes: logs in and lists what's on the server. Empty
 * with *ok false and *error set on any failure (including a wrong
 * password, which comes back as "wrong" below so the dialog can ask
 * again instead of just failing). */
std::vector<AfpVolume> afpListVolumes(const QString &address, const QString &user,
	const QString &password, bool cleartext, bool *ok, bool *wrongPassword, QString *error);

/* platinum-afp mount: starts a fully detached background process (it
 * runs for as long as the volume stays mounted — hours, potentially,
 * well past this window closing) at `mountPoint`, which is created if
 * missing. The password is piped to the child's own stdin before it
 * execs, never placed on a command line; nothing here keeps a QProcess
 * alive to hold that pipe open; it is written and closed before
 * returning. True if the process was *started*; this does not wait to
 * see whether the mount itself then succeeds (that takes a few
 * seconds and the Finder will simply show the disk once it answers —
 * see netvolumes.h). */
bool afpMount(const QString &address, const QString &volume, const QString &mountPoint,
	const QString &user, const QString &password, bool cleartext, QString *error);
