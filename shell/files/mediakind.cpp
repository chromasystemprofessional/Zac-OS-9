#include "mediakind.h"

#include <QFile>
#include <QFileInfo>

#include <sys/stat.h>
#include <sys/sysmacros.h>

namespace {

bool enabled(const QMap<QString, QString> &properties, const char *key) {
	const auto value = properties.constFind(QLatin1String(key));
	if (value == properties.constEnd()) {
		return false;
	}
	return *value == QLatin1String("1") ||
		value->compare(QLatin1String("true"), Qt::CaseInsensitive) == 0 ||
		value->compare(QLatin1String("yes"), Qt::CaseInsensitive) == 0;
}

int count(const QMap<QString, QString> &properties, const char *key) {
	const auto value = properties.constFind(QLatin1String(key));
	if (value == properties.constEnd()) {
		return 0;
	}
	bool ok = false;
	const int number = value->toInt(&ok);
	return ok && number > 0 ? number : 0;
}

QMap<QString, QString> readUdevProperties(dev_t device) {
	QMap<QString, QString> properties;
	QFile file(QStringLiteral("/run/udev/data/b%1:%2")
		.arg(major(device)).arg(minor(device)));
	if (!file.open(QIODevice::ReadOnly)) {
		return properties;
	}
	/* The database is small; bound it anyway, as every on-disk value is
	 * untrusted. Only E: lines are properties. */
	const QByteArray data = file.read(64 * 1024);
	for (const QByteArray &line : data.split('\n')) {
		if (!line.startsWith("E:")) {
			continue;
		}
		const int equals = line.indexOf('=');
		if (equals <= 2) {
			continue;
		}
		properties.insert(QString::fromUtf8(line.mid(2, equals - 2)),
			QString::fromUtf8(line.mid(equals + 1)));
	}
	return properties;
}

QString readLoopBacking(const QString &name) {
	QFile file(QStringLiteral("/sys/class/block/%1/loop/backing_file").arg(name));
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return QString::fromUtf8(file.read(4096)).trimmed();
}

QString loopBacking(const QString &kernelName) {
	QString backing = readLoopBacking(kernelName);
	if (!backing.isEmpty()) {
		return backing;
	}
	/* A partition of a loop device (loop0p1) has no loop/ directory of
	 * its own; the backing file belongs to loop0. */
	if (kernelName.startsWith(QLatin1String("loop"))) {
		const int partition = kernelName.indexOf(QLatin1Char('p'), 4);
		if (partition > 4 && partition + 1 < kernelName.size()) {
			return readLoopBacking(kernelName.left(partition));
		}
	}
	return {};
}

} // namespace

MediaKind classifyMedia(const QMap<QString, QString> &properties,
		const QString &kernelName, const QString &loopBacking) {
	/* A loop device is an image, whatever the image contains. */
	if (!loopBacking.isEmpty() || kernelName.startsWith(QLatin1String("loop"))) {
		return MediaKind::LoopImage;
	}

	/* Optical media: cdrom_id writes the track counts and the blank
	 * state. A mixed-mode disc with a data track is data: it has a
	 * filesystem to browse. OpticalVideo is set later from the mounted
	 * disc's content type, which udev cannot know. */
	if (enabled(properties, "ID_CDROM") ||
			kernelName.startsWith(QLatin1String("sr"))) {
		if (count(properties, "ID_CDROM_MEDIA_TRACK_COUNT_DATA") > 0) {
			return MediaKind::OpticalData;
		}
		if (count(properties, "ID_CDROM_MEDIA_TRACK_COUNT_AUDIO") > 0) {
			return MediaKind::OpticalAudio;
		}
		if (properties.value(QStringLiteral("ID_CDROM_MEDIA_STATE"))
				.compare(QLatin1String("blank"), Qt::CaseInsensitive) == 0 ||
				(enabled(properties, "ID_CDROM_MEDIA") &&
				 count(properties, "ID_CDROM_MEDIA_TRACK_COUNT") == 0)) {
			return MediaKind::OpticalBlank;
		}
		return MediaKind::OpticalData;
	}

	/* Check the removable-disk-specific tags before the generic floppy
	 * one: an Iomega drive may carry more than one ID_DRIVE_* tag. */
	if (enabled(properties, "ID_DRIVE_FLOPPY_JAZ")) {
		return MediaKind::Jaz;
	}
	if (enabled(properties, "ID_DRIVE_FLOPPY_ZIP")) {
		return MediaKind::Zip;
	}
	if (enabled(properties, "ID_DRIVE_FLOPPY") ||
			kernelName.startsWith(QLatin1String("fd"))) {
		return MediaKind::Floppy;
	}

	const auto bus = properties.constFind(QStringLiteral("ID_BUS"));
	if (bus != properties.constEnd() &&
			bus->compare(QLatin1String("usb"), Qt::CaseInsensitive) == 0) {
		return MediaKind::Usb;
	}
	return MediaKind::Disk;
}

MediaKind mediaKindForDevice(const QString &device, QString *backingImage) {
	if (backingImage) {
		backingImage->clear();
	}
	if (!device.startsWith(QLatin1String("/dev/")) ||
			device.contains(QLatin1String("/../")) || device == QLatin1String("/dev/")) {
		return MediaKind::Disk;
	}
	struct stat info {};
	if (::stat(QFile::encodeName(device).constData(), &info) != 0 ||
			!S_ISBLK(info.st_mode)) {
		return MediaKind::Disk;
	}
	const QString kernelName = QFileInfo(device).fileName();
	const QString backing = loopBacking(kernelName);
	if (backingImage) {
		*backingImage = backing;
	}
	return classifyMedia(readUdevProperties(info.st_rdev), kernelName, backing);
}

QString mediaKindName(MediaKind kind) {
	switch (kind) {
	case MediaKind::Disk: return QStringLiteral("disk");
	case MediaKind::Usb: return QStringLiteral("usb");
	case MediaKind::OpticalData: return QStringLiteral("optical-data");
	case MediaKind::OpticalAudio: return QStringLiteral("optical-audio");
	case MediaKind::OpticalVideo: return QStringLiteral("optical-video");
	case MediaKind::OpticalBlank: return QStringLiteral("optical-blank");
	case MediaKind::Floppy: return QStringLiteral("floppy");
	case MediaKind::Zip: return QStringLiteral("zip");
	case MediaKind::Jaz: return QStringLiteral("jaz");
	case MediaKind::LoopImage: return QStringLiteral("loop-image");
	}
	return QStringLiteral("disk");
}
