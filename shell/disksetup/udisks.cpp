#include "udisks.h"

#include <QLocale>
#include <algorithm>

static QString bytes(const QVariant &v) {
	QByteArray b = v.toByteArray();
	while (b.endsWith('\0')) {
		b.chop(1);
	}
	return QString::fromUtf8(b);
}

static QStringList bytesList(const QVariant &v) {
	QStringList out;
	const QDBusArgument arg = v.value<QDBusArgument>();
	if (arg.currentType() == QDBusArgument::ArrayType) {
		arg.beginArray();
		while (!arg.atEnd()) {
			QByteArray b;
			arg >> b;
			while (b.endsWith('\0')) {
				b.chop(1);
			}
			out << QString::fromUtf8(b);
		}
		arg.endArray();
	} else {
		for (const QByteArray &b : v.value<QList<QByteArray>>()) {
			out << QString::fromUtf8(b).remove(QChar('\0'));
		}
	}
	return out;
}

void parseUDisks(const UDisksObjects &objects, QList<VetVolume> *volumes, QMap<QString, VetDrive> *drives) {
	for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
		const UDisksInterfaces &ifs = it.value();
		const QString path = it.key().path();
		if (ifs.contains("org.freedesktop.UDisks2.Drive")) {
			const QVariantMap d = ifs.value("org.freedesktop.UDisks2.Drive");
			VetDrive drive;
			drive.model = (d.value("Vendor").toString() + ' ' + d.value("Model").toString()).trimmed();
			if (ifs.contains("org.freedesktop.UDisks2.Drive.Ata")) {
				const QVariantMap a = ifs.value("org.freedesktop.UDisks2.Drive.Ata");
				drive.smart = a.value("SmartSupported").toBool() && a.value("SmartEnabled").toBool() &&
					a.value("SmartUpdated").toULongLong() > 0;
				drive.failing = a.value("SmartFailing").toBool();
				const double kelvin = a.value("SmartTemperature").toDouble();
				drive.celsius = kelvin > 0 ? kelvin - 273.15 : -1;
				const qulonglong seconds = a.value("SmartPowerOnSeconds").toULongLong();
				drive.hours = seconds > 0 ? qint64(seconds / 3600) : -1;
			} else if (ifs.contains("org.freedesktop.UDisks2.NVMe.Controller")) {
				const QVariantMap n = ifs.value("org.freedesktop.UDisks2.NVMe.Controller");
				drive.smart = n.value("SmartUpdated").toULongLong() > 0;
				drive.failing = !n.value("SmartCriticalWarning").toStringList().isEmpty();
				const int kelvin = n.value("SmartTemperature").toInt();
				drive.celsius = kelvin > 0 ? kelvin - 273.15 : -1;
				const qulonglong hours = n.value("SmartPowerOnHours").toULongLong();
				drive.hours = hours > 0 ? qint64(hours) : -1;
			}
			drives->insert(path, drive);
		}
		if (!ifs.contains("org.freedesktop.UDisks2.Filesystem") || !ifs.contains("org.freedesktop.UDisks2.Block")) {
			continue;
		}
		const QVariantMap b = ifs.value("org.freedesktop.UDisks2.Block");
		if (b.value("HintIgnore").toBool() || b.value("IdUsage").toString() != "filesystem") {
			continue;
		}
		VetVolume v;
		v.path = path;
		v.device = bytes(b.value("PreferredDevice"));
		if (v.device.isEmpty()) {
			v.device = bytes(b.value("Device"));
		}
		v.type = b.value("IdType").toString();
		v.uuid = b.value("IdUUID").toString();
		v.size = b.value("Size").toULongLong();
		const QString label = b.value("IdLabel").toString();
		v.name = label.isEmpty() ? v.device.section('/', -1) : label;
		v.drive = b.value("Drive").value<QDBusObjectPath>().path();
		const QVariantMap f = ifs.value("org.freedesktop.UDisks2.Filesystem");
		v.mountPoints = bytesList(f.value("MountPoints"));
		/* What UDisks (and its tools) can check: fsck for ext, vfat, exfat, xfs, btrfs... */
		static const QStringList checkable = { "ext2", "ext3", "ext4", "vfat", "exfat", "xfs", "btrfs",
			"ntfs", "hfsplus", "f2fs" };
		v.canCheck = checkable.contains(v.type);
		v.canRepair = v.canCheck && v.type != "btrfs";
		volumes->append(v);
	}
	std::sort(volumes->begin(), volumes->end(), [](const VetVolume &a, const VetVolume &b) {
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});
}

QString driveHealth(const VetDrive &d) {
	QString text = d.model.isEmpty() ? QString("This drive") : d.model;
	if (!d.smart) {
		return text + ": its health isn't known (no SMART readings yet).";
	}
	QStringList parts = { d.failing ? "FAILING — copy what's on it elsewhere now" : "healthy" };
	if (d.celsius >= 0) {
		parts << QString::number(qRound(d.celsius)) + " °C";
	}
	if (d.hours >= 0) {
		parts << QLocale().toString(d.hours) + " hours powered on";
	}
	return text + ": " + parts.join(", ") + ".";
}
