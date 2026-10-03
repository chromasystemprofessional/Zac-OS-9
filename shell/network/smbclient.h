#pragma once

/*
 * The Network Browser's Windows half: Samba shares, through gio (gvfs's
 * smb backend) rather than a client of our own — Debian already has a
 * good one.
 *
 * There is no share list here the way afpclient.h has afpListVolumes():
 * anonymously browsing an SMB server's share list through gio needs the
 * server root mounted first and behaved inconsistently testing against
 * it (see docs/network.md); the share name is typed instead, the way
 * Windows' own "Map Network Drive" asks for \\server\share rather than
 * browsing. AFP, with FPGetSrvrParms behind it, has no such problem.
 */

#include <QString>

/* Mounts smb://server/share through gio: Guest with -a (no prompting
 * at all), a named user by answering gio mount's own prompts on its
 * stdin (user, domain, password — the order it always asks in; there
 * is no flag for this, so this is the one sure way to hand it a
 * password that was never on a command line). Blocks until gio
 * finishes (gvfs mounts are quick; nothing here waits on a volume
 * staying mounted the way platinum-afp's FUSE process does). */
bool smbMount(const QString &server, const QString &share, const QString &user,
	const QString &password, QString *error);

/* gio mount -u on the same URI; no authentication needed to unmount
 * what is already mounted. */
bool smbUnmount(const QString &server, const QString &share);
