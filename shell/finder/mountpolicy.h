#pragma once

/*
 * Which volumes mount by themselves, at login and when their drive is
 * connected: every one, unless Disk Setup's "Mount automatically" is off
 * for it. Kept by filesystem UUID in ~/.config/zacos9/disks.conf, read by
 * the Sniffer (localvolumes.cpp) and set by Disk Setup.
 *
 * A volume a crash left marked in use (NTFS not closed cleanly) refuses an
 * ordinary mount; the Sniffer then mounts it read-only, and Disk Setup
 * offers Repair.
 */

#include <QString>

bool volumeMountsAutomatically(const QString &uuid);
void setVolumeMountsAutomatically(const QString &uuid, bool on);

/* Whether `device` (/dev/sda1) is mounted read-only, from `mountinfo`
 * (/proc/self/mountinfo's format; that file unless given, for tests). */
bool volumeMountedReadOnly(const QString &device, const QString &mountinfo = QString());
