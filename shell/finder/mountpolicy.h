#pragma once

/*
 * Which volumes mount by themselves, at login and when their drive is
 * connected: every one, unless Disk Setup's "Mount automatically" is off
 * for it. Kept by filesystem UUID in ~/.config/zacos9/disks.conf, read by
 * the Sniffer (localvolumes.cpp) and set by Disk Setup.
 *
 * In the live session (started from the install medium) the computer's own
 * disks stay closed unless "Mount automatically" is turned on for them:
 * they are what gets erased to install, and a mounted partition can't be
 * (issue #31). USB drives and discs still mount.
 *
 * A volume a crash left marked in use (NTFS not closed cleanly) refuses an
 * ordinary mount; the Sniffer then mounts it read-only, and Disk Setup
 * offers Repair.
 */

#include <QString>

/* `internal`: on a drive that isn't removable (not USB, not a card). */
bool volumeMountsAutomatically(const QString &uuid, bool internal = false);
void setVolumeMountsAutomatically(const QString &uuid, bool on);

/* Started from the install medium (/run/live; ZACOS9_LIVE=1 or 0 for tests). */
bool liveSession();

/* Whether `device` (/dev/sda1) is mounted read-only, from `mountinfo`
 * (/proc/self/mountinfo's format; that file unless given, for tests). */
bool volumeMountedReadOnly(const QString &device, const QString &mountinfo = QString());
