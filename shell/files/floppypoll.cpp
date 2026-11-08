#include "floppypoll.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <linux/fd.h>
#include <linux/fs.h>
#include <scsi/sg.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

struct DriveState {
	bool known = false;
	bool present = false;
	int generation = -1;
	unsigned long flags = 0;
};

bool readMedia(int fd) {
	if (::lseek(fd, 0, SEEK_SET) != 0) {
		return false;
	}
	char byte = 0;
	return ::read(fd, &byte, 1) == 1;
}

/* A PC floppy has a change line and a generation; it is polled without
 * touching the disk until one of those changes. */
bool probeFloppy(const QString &device, DriveState *state) {
	const QByteArray path = device.toUtf8();
	int fd = ::open(path.constData(), O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		const bool changed = state->known && state->present;
		state->known = true;
		state->present = false;
		state->flags = 0;
		return changed;
	}
	struct floppy_drive_struct status {};
	if (::ioctl(fd, FDPOLLDRVSTAT, &status) != 0) {
		::close(fd);
		const bool changed = state->known && state->present;
		state->known = true;
		state->present = false;
		state->flags = 0;
		return changed;
	}
	static constexpr unsigned long changeMask =
		FD_DISK_NEWCHANGE | FD_DISK_CHANGED;
	const bool flagChanged = (status.flags ^ state->flags) & changeMask;
	bool present = state->present;
	if (!state->known || status.generation != state->generation || flagChanged) {
		present = readMedia(fd);
		state->generation = status.generation;
	}
	state->flags = status.flags;
	::close(fd);
	const bool changed = !state->known || state->present != present;
	state->known = true;
	state->present = present;
	return changed;
}

/* USB floppy drives and ATA/USB Zip/Jaz drives have no floppy change line.
 * SCSI TEST UNIT READY says whether a medium is present without reading
 * the medium; if the kernel refuses the command, the size is the fallback. */
bool probeScsi(const QString &device, DriveState *state) {
	const QByteArray path = device.toUtf8();
	int fd = ::open(path.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	bool present = false;
	if (fd >= 0) {
		sg_io_hdr_t io {};
		unsigned char cdb[6] = { 0x00, 0, 0, 0, 0, 0 }; /* TEST UNIT READY */
		unsigned char sense[32] = { 0 };
		io.interface_id = 'S';
		io.dxfer_direction = SG_DXFER_NONE;
		io.cmd_len = sizeof(cdb);
		io.cmdp = cdb;
		io.mx_sb_len = sizeof(sense);
		io.sbp = sense;
		io.timeout = 2000;
		if (::ioctl(fd, SG_IO, &io) == 0) {
			const int status = io.status & 0x7e;
			const int senseKey = sense[2] & 0x0f;
			if (status == 0 && io.host_status == 0) {
				present = true;
			} else if (status == 0x02 && senseKey == 0x02 &&
					sense[12] == 0x3a) {
				present = false; /* medium not present */
			}
		} else {
			uint64_t size = 0;
			if (::ioctl(fd, BLKGETSIZE64, &size) == 0 && size > 0) {
				present = true;
			}
		}
		::close(fd);
	}
	const bool changed = !state->known || state->present != present;
	state->known = true;
	state->present = present;
	return changed;
}

bool hasRemovableTag(const QString &name) {
	QFile number("/sys/class/block/" + name + "/dev");
	if (!number.open(QIODevice::ReadOnly)) {
		return false;
	}
	const QByteArray deviceNumber = number.read(64).trimmed();
	QFile data("/run/udev/data/b" + deviceNumber);
	if (!data.open(QIODevice::ReadOnly)) {
		return false;
	}
	for (const QByteArray &line : data.read(64 * 1024).split('\n')) {
		if (line == "E:ID_DRIVE_FLOPPY=1" ||
				line == "E:ID_DRIVE_FLOPPY_ZIP=1" ||
				line == "E:ID_DRIVE_FLOPPY_JAZ=1") {
			return true;
		}
	}
	return false;
}

} // namespace

void floppyPollStart(std::function<void(const QString &device, bool present)> changed) {
	static bool started = false;
	if (started || !QCoreApplication::instance()) {
		return;
	}
	started = true;
	auto *states = new QHash<QString, DriveState>;
	auto poll = [states, changed] {
		QSet<QString> seen;
		const QDir sys("/sys/class/block");
		static const QRegularExpression wholeDisk(QStringLiteral("^sd[a-z]+$"));
		for (const QString &name : sys.entryList({ "fd*", "sd*" },
				QDir::AllEntries | QDir::NoDotAndDotDot)) {
			if (!QFileInfo(sys.filePath(name)).isDir()) {
				continue;
			}
			const bool floppy = name.startsWith(QLatin1String("fd"));
			if (!floppy && (!wholeDisk.match(name).hasMatch() ||
					!hasRemovableTag(name))) {
				continue;
			}
			const QString device = "/dev/" + name;
			if (!QFileInfo(device).exists()) {
				continue;
			}
			seen.insert(device);
			auto it = states->find(device);
			if (it == states->end()) {
				it = states->insert(device, DriveState{});
			}
			const bool changedNow = floppy ? probeFloppy(device, &it.value())
				: probeScsi(device, &it.value());
			if (changedNow) {
				changed(device, it.value().present);
			}
		}
		for (auto it = states->begin(); it != states->end();) {
			if (seen.contains(it.key())) {
				++it;
			} else {
				changed(it.key(), false);
				it = states->erase(it);
			}
		}
	};
	auto *timer = new QTimer(QCoreApplication::instance());
	timer->setInterval(2000);
	QObject::connect(timer, &QTimer::timeout, timer, poll);
	timer->start();
	poll();
}
