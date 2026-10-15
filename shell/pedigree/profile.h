#pragma once

/*
 * What Pedigree shows (Mac OS 9's Apple System Profiler, Missing Manual
 * ch. 13): this computer's papers, read without asking for a password
 * from /sys, /proc, /etc and dpkg's status file. Items nest: a section,
 * the things in it, their details.
 *
 * ZACOS9_PEDIGREE_ROOT puts another directory in place of / (tests);
 * then nothing is asked of the running system (network addresses, free
 * space, host name), so a fixture gives the same profile anywhere.
 */

#include <QList>
#include <QString>

struct ProfileItem {
	QString label, value;
	QList<ProfileItem> children;
};

/* "System Profile": System overview, Hardware overview, Displays,
 * Network overview. */
QList<ProfileItem> systemProfile();
/* "Devices and Volumes": Disks and their volumes, USB, PCI, Bluetooth,
 * Sound. */
QList<ProfileItem> devicesAndVolumes();

/* Items as indented text: "label: value", four spaces a level. */
QString profileText(const QList<ProfileItem> &items);
/* Both tabs, as text for File > Save Report. */
QString profileReport(const QList<ProfileItem> &system, const QList<ProfileItem> &devices);

/* 1536 -> "1.5 KB": binary units, as the Mac's. */
QString profileSize(double bytes);
/* The monitor's name in an EDID block (its 0xFC descriptor), or "". */
QString edidName(const QByteArray &edid);
/* A name from pci.ids or usb.ids: the vendor, or the vendor's device. */
QString idsVendor(const QString &idsFile, const QString &vendor);
QString idsDevice(const QString &idsFile, const QString &vendor, const QString &device);
