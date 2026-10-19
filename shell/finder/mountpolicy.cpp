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

bool volumeMountsAutomatically(const QString &uuid) {
	if (uuid.isEmpty()) {
		return true;
	}
	QSettings settings(disksConf(), QSettings::IniFormat);
	return settings.value("mount-automatically/" + uuid, true).toBool();
}

void setVolumeMountsAutomatically(const QString &uuid, bool on) {
	if (uuid.isEmpty()) {
		return;
	}
	QSettings settings(disksConf(), QSettings::IniFormat);
	if (on) {
		settings.remove("mount-automatically/" + uuid);
	} else {
		settings.setValue("mount-automatically/" + uuid, false);
	}
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
