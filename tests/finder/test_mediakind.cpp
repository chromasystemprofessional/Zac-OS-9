#include <QMap>
#include <QString>

#include <cassert>
#include <initializer_list>
#include <utility>

#include "mediakind.h"

static QMap<QString, QString> properties(
		std::initializer_list<std::pair<const char *, const char *>> values) {
	QMap<QString, QString> out;
	for (const auto &value : values) {
		out.insert(QString::fromLatin1(value.first),
			QString::fromLatin1(value.second));
	}
	return out;
}

static void checkClassify() {
	assert(classifyMedia({}, "sda", {}) == MediaKind::Disk);
	assert(classifyMedia({}, "sda1", {}) == MediaKind::Disk);
	assert(classifyMedia(properties({ { "ID_BUS", "usb" } }), "sdb1", {}) ==
		MediaKind::Usb);
	assert(classifyMedia(properties({ { "ID_BUS", "USB" } }), "sdb1", {}) ==
		MediaKind::Usb);

	/* Optical: cdrom_id's track counts decide data, audio or blank. */
	assert(classifyMedia(properties({
		{ "ID_CDROM", "1" },
		{ "ID_CDROM_MEDIA", "1" },
		{ "ID_CDROM_MEDIA_TRACK_COUNT_AUDIO", "12" },
	}), "sr0", {}) == MediaKind::OpticalAudio);
	assert(classifyMedia(properties({
		{ "ID_CDROM", "1" },
		{ "ID_CDROM_MEDIA", "1" },
		{ "ID_CDROM_MEDIA_TRACK_COUNT_DATA", "1" },
		{ "ID_CDROM_MEDIA_TRACK_COUNT_AUDIO", "3" },
	}), "sr0", {}) == MediaKind::OpticalData);
	assert(classifyMedia(properties({
		{ "ID_CDROM", "1" },
		{ "ID_CDROM_MEDIA", "1" },
		{ "ID_CDROM_MEDIA_STATE", "blank" },
	}), "sr0", {}) == MediaKind::OpticalBlank);
	assert(classifyMedia(properties({
		{ "ID_CDROM", "1" },
		{ "ID_CDROM_MEDIA", "1" },
	}), "sr0", {}) == MediaKind::OpticalBlank);
	assert(classifyMedia(properties({
		{ "ID_CDROM", "1" },
		{ "ID_CDROM_MEDIA", "1" },
		{ "ID_CDROM_MEDIA_TRACK_COUNT_DATA", "not-a-number" },
		{ "ID_CDROM_MEDIA_TRACK_COUNT_AUDIO", "2" },
	}), "sr0", {}) == MediaKind::OpticalAudio);
	assert(classifyMedia({}, "sr0", {}) == MediaKind::OpticalData);

	/* Floppy, Zip and Jaz; the more specific tags win. */
	assert(classifyMedia(properties({ { "ID_DRIVE_FLOPPY", "1" } }),
		"fd0", {}) == MediaKind::Floppy);
	assert(classifyMedia({}, "fd0", {}) == MediaKind::Floppy);
	assert(classifyMedia(properties({ { "ID_DRIVE_FLOPPY_ZIP", "1" } }),
		"sdz", {}) == MediaKind::Zip);
	assert(classifyMedia(properties({
		{ "ID_DRIVE_FLOPPY", "1" },
		{ "ID_DRIVE_FLOPPY_ZIP", "1" },
	}), "sdz", {}) == MediaKind::Zip);
	assert(classifyMedia(properties({ { "ID_DRIVE_FLOPPY_JAZ", "1" } }),
		"sdz", {}) == MediaKind::Jaz);

	/* A loop is an image whatever else its properties say. */
	assert(classifyMedia({}, "loop0", {}) == MediaKind::LoopImage);
	assert(classifyMedia(properties({ { "ID_CDROM", "1" } }), "loop0",
		"/home/me/dvd.iso") == MediaKind::LoopImage);
	assert(classifyMedia({}, "sda1", "/home/me/floppy.img") ==
		MediaKind::LoopImage);

	assert(mediaKindName(MediaKind::Disk) == "disk");
	assert(mediaKindName(MediaKind::Usb) == "usb");
	assert(mediaKindName(MediaKind::OpticalData) == "optical-data");
	assert(mediaKindName(MediaKind::OpticalAudio) == "optical-audio");
	assert(mediaKindName(MediaKind::OpticalVideo) == "optical-video");
	assert(mediaKindName(MediaKind::OpticalBlank) == "optical-blank");
	assert(mediaKindName(MediaKind::Floppy) == "floppy");
	assert(mediaKindName(MediaKind::Zip) == "zip");
	assert(mediaKindName(MediaKind::Jaz) == "jaz");
	assert(mediaKindName(MediaKind::LoopImage) == "loop-image");
}

static void checkDeviceProbe() {
	QString backing = QStringLiteral("not cleared");
	assert(mediaKindForDevice(QStringLiteral("/dev/null"), &backing) ==
		MediaKind::Disk);
	assert(backing.isEmpty());
	assert(mediaKindForDevice(QStringLiteral("/dev/does-not-exist"), &backing) ==
		MediaKind::Disk);
	assert(backing.isEmpty());
	assert(mediaKindForDevice(QStringLiteral("/dev/../etc/passwd"), &backing) ==
		MediaKind::Disk);
	assert(backing.isEmpty());
	assert(mediaKindForDevice(QStringLiteral("relative"), &backing) ==
		MediaKind::Disk);
	assert(backing.isEmpty());
}

int main() {
	checkClassify();
	checkDeviceProbe();
	return 0;
}
