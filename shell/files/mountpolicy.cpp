#include "mountpolicy.h"

#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

static QString disksConf() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		"/zacos9/disks.conf";
}

bool liveSession() {
	const QByteArray forced = qgetenv("ZACOS9_LIVE");
	return forced.isEmpty() ? QFileInfo(QStringLiteral("/run/live")).isDir() : forced == "1";
}

bool volumeMountsAutomatically(const QString &uuid, bool internal) {
	const bool fallback = !(internal && liveSession());
	if (uuid.isEmpty()) {
		return fallback;
	}
	QSettings settings(disksConf(), QSettings::IniFormat);
	return settings.value("mount-automatically/" + uuid, fallback).toBool();
}

void setVolumeMountsAutomatically(const QString &uuid, bool on) {
	if (uuid.isEmpty()) {
		return;
	}
	/* On is kept too: in the live session an internal disk is off unless set. */
	QSettings settings(disksConf(), QSettings::IniFormat);
	settings.setValue("mount-automatically/" + uuid, on);
}

/* mountinfo: "ID PARENT MAJ:MIN ROOT MOUNTPOINT OPTIONS [TAGS...] - TYPE
 * SOURCE SUPEROPTIONS"; read-only is in either set of options. */
bool volumeMountedReadOnly(const QString &device, const QString &mountinfo) {
	QFile file(mountinfo.isEmpty() ? QStringLiteral("/proc/self/mountinfo") : mountinfo);
	if (device.isEmpty() || !file.open(QIODevice::ReadOnly)) {
		return false;
	}
	const QString canonical = QFileInfo(device).canonicalFilePath();
	for (const QByteArray &raw : file.readAll().split('\n')) {
		const QStringList fields = QString::fromUtf8(raw).split(' ');
		const int dash = fields.indexOf("-");
		if (dash < 6 || dash + 2 >= fields.size()) {
			continue;
		}
		const QString source = fields[dash + 2];
		if (source != device && (canonical.isEmpty() || source != canonical)) {
			continue;
		}
		const QStringList options = fields[5].split(',') +
			(dash + 3 < fields.size() ? fields[dash + 3].split(',') : QStringList());
		if (options.contains("ro")) {
			return true;
		}
	}
	return false;
}
