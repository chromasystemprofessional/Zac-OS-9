/* Before anything of Qt's: Qt defines `signals` as a keyword macro, and
 * GIO's D-Bus headers have a struct field of that name. */
#include <gio/gio.h>

#include "localvolumes.h"
#include "programs.h"

#include <cstring>

#include <QFileInfo>
#include <QStorageInfo>
#include <QSet>
#include <QCoreApplication>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QDebug>
#include <QRegularExpression>
#include <unistd.h>
#include <cstdio>

#include "mountpolicy.h"
#include "netvolumes.h"
#include "autoplay.h"
#include "mediaeject.h"
#include "cueimage.h"
#include "floppypoll.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>

/* Where the Memory control panel's RAM Disk is mounted (a tmpfs:
 * system/memory/zacos9-memory-helper). */
static const QString RAM_DISK = QStringLiteral("/run/zacos9-ramdisk");

static void deleteLoopDevice(const QString &device, const QString &backingImage = {});

static bool ramDiskMounted() {
	QFile table("/proc/self/mountinfo");
	if (!table.open(QIODevice::ReadOnly)) {
		return false;
	}
	for (const QByteArray &line : table.readAll().split('\n')) {
		const QList<QByteArray> fields = line.split(' ');
		if (fields.size() > 5 && QString::fromUtf8(fields[4]) == RAM_DISK) {
			return true;
		}
	}
	return false;
}

/* Mount-point prefixes that are never user-browsable storage: kernel
 * pseudo-filesystems, container overlays, snap loops, etc. */
bool localVolumePathShown(const QString &path) {
	if (path == "/") {
		return false;
	}
	if (path.startsWith("/run/media/")) {
		return true;
	}
	/* /run, /sys, /proc, /dev: kernel/runtime paths. */
	for (const char *prefix : { "/run", "/sys", "/proc", "/dev", "/snap" }) {
		if (path == QLatin1String(prefix) || path.startsWith(QString::fromLatin1(prefix) + '/')) {
			return false;
		}
	}
	return true;
}

/* Is this mount point already covered by netVolumes()? */
static bool isNetworkMount(const QString &path) {
	for (const NetVolume &v : netVolumes()) {
		if (v.path == path) {
			return true;
		}
	}
	return false;
}

QString localVolumeUsbDevice(const QString &mountPath) {
	QString device;
	GVolumeMonitor *monitor = g_volume_monitor_get();
	GList *mounts = g_volume_monitor_get_mounts(monitor);
	for (GList *item = mounts; item; item = item->next) {
		auto *mount = G_MOUNT(item->data);
		GFile *root = g_mount_get_root(mount);
		char *path = g_file_get_path(root);
		g_object_unref(root);
		const bool matches = path && mountPath == QString::fromUtf8(path);
		g_free(path);
		if (!matches) {
			continue;
		}
		GVolume *volume = g_mount_get_volume(mount);
		if (!volume) {
			break;
		}
		char *rawDevice = g_volume_get_identifier(volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
		const QString candidate = QString::fromUtf8(rawDevice ? rawDevice : "");
		g_free(rawDevice);
		g_object_unref(volume);
		if (!candidate.startsWith("/dev/") || candidate.contains("/../")) {
			break;
		}
		QString sysPath = QFileInfo("/sys/class/block/" + QFileInfo(candidate).fileName()).canonicalFilePath();
		while (!sysPath.isEmpty() && sysPath != "/") {
			if (QFileInfo(sysPath + "/subsystem").canonicalFilePath() == "/sys/bus/usb") {
				device = candidate;
				break;
			}
			sysPath = QFileInfo(sysPath).absolutePath();
		}
		break;
	}
	g_list_free_full(mounts, g_object_unref);
	g_object_unref(monitor);
	return device;
}

std::vector<LocalVolume> localVolumeMacNames(const QByteArray &record,
		const QString &container, const QSet<QString> &mounted) {
	const QJsonObject info = QJsonDocument::fromJson(record).object();
	std::vector<LocalVolume> volumes;
	for (const QJsonValue &value : info.value("volumes").toArray()) {
		const QJsonObject named = value.toObject();
		const QString path = named.value("path").toString();
		const QString label = named.value("name").toString();
		if (QFileInfo(path).absolutePath() != container || !mounted.contains(path) || label.isEmpty()) {
			continue;
		}
		LocalVolume volume;
		volume.name = label;
		volume.path = path;
		volume.ejectable = true;
		volume.macDevice = info.value("device").toString();
		volume.device = volume.macDevice;
		volumes.push_back(std::move(volume));
	}
	return volumes;
}

/* A small, bounded read from a mounted disk. The disk is untrusted: never
 * read an unbounded file from it, and use only these two known paths. */
static QString readMountedFile(const QString &path, qsizetype limit) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return QString::fromUtf8(file.read(limit));
}

bool localVolumeZacosInfo(const QString &mountPath, QString *name, QString *version) {
	const QString root = QDir::cleanPath(mountPath);
	const QString named = readMountedFile(root + "/etc/zacos9/disk-name", 512);
	const QString candidate = named.section('\n', 0, 0).trimmed();
	if (candidate.isEmpty()) {
		return false;
	}
	/* The installer writes disk-name only after installing the zacos9
	 * package, so it is the cheap marker. Confirm it with dpkg's own record
	 * rather than trusting a name alone. */
	const QString status = readMountedFile(root + "/var/lib/dpkg/status", 16 * 1024 * 1024);
	QString package, state, packageVersion;
	const auto installed = [&] {
		return package == "zacos9" && state == "install ok installed" &&
			!packageVersion.isEmpty();
	};
	for (const QString &line : status.split('\n')) {
		if (line.startsWith("Package: ")) {
			if (installed()) {
				break;
			}
			package = line.mid(9).trimmed();
			state.clear();
			packageVersion.clear();
		} else if (package == "zacos9" && line.startsWith("Version: ")) {
			packageVersion = line.mid(9).trimmed();
		} else if (package == "zacos9" && line.startsWith("Status: ")) {
			state = line.mid(8).trimmed();
		}
	}
	if (!installed()) {
		return false;
	}
	if (name) {
		*name = candidate.left(128);
	}
	if (version) {
		*version = packageVersion;
	}
	return true;
}

std::vector<LocalVolume> localVolumes() {
	std::vector<LocalVolume> out;

	GVolumeMonitor *monitor = g_volume_monitor_get();
	GList *mounts = g_volume_monitor_get_mounts(monitor);

	for (GList *l = mounts; l; l = l->next) {
		auto *mount = G_MOUNT(l->data);

		/* Skip mounts without a real block-device volume backing (most
		 * virtual filesystems have no GVolume). */
		GVolume *vol = g_mount_get_volume(mount);
		if (!vol) {
			continue;
		}

		GFile *root = g_mount_get_root(mount);
		char *rawPath = g_file_get_path(root);
		g_object_unref(root);
		if (!rawPath) {
			g_object_unref(vol);
			continue;
		}
		const QString path = QString::fromUtf8(rawPath);
		g_free(rawPath);

		/* Skip / (shown as the Unix disk via vfsUnixVolumeShown), known
		 * virtual/kernel mount paths, and mounts already shown as
		 * network volumes. */
		if (!localVolumePathShown(path) || isNetworkMount(path)) {
			g_object_unref(vol);
			continue;
		}

		LocalVolume v;
		v.path = path;
		/* GMount's own name is already human-readable ("500 GB SSD",
		 * the volume label, or the share name). Fall back to the
		 * mount-point basename only when the name is empty. */
		char *rawName = g_mount_get_name(mount);
		v.name = rawName && *rawName ? QString::fromUtf8(rawName)
			: QFileInfo(path).fileName();
		g_free(rawName);
		v.ejectable = g_mount_can_eject(mount) || g_volume_can_eject(vol);
		char *rawDevice = g_volume_get_identifier(vol,
			G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
		if (rawDevice && *rawDevice) {
			v.device = QString::fromUtf8(rawDevice);
			v.kind = mediaKindForDevice(v.device, &v.backingImage);
		}
		g_free(rawDevice);
		if (GDrive *drive = g_volume_get_drive(vol)) {
			char *rawDrive = g_drive_get_identifier(drive,
				G_DRIVE_IDENTIFIER_KIND_UNIX_DEVICE);
			if (rawDrive && *rawDrive) {
				v.drive = QString::fromUtf8(rawDrive);
			}
			g_free(rawDrive);
			g_object_unref(drive);
		}
		QString zacosName;
		if (localVolumeZacosInfo(path, &zacosName, &v.zacosVersion)) {
			v.zacos = true;
			v.name = zacosName;
		}
		g_object_unref(vol);
		out.push_back(std::move(v));
	}

	g_list_free_full(mounts, g_object_unref);

	/* Audio CDs and blank discs have GVolumes but no filesystem mount
	 * (or a GIO mount with no native path). Put them on the desktop too,
	 * so they can be opened and put away. Other unmounted media are left
	 * to their own discovery/polling paths. */
	QSet<QString> shownPaths;
	for (const LocalVolume &volume : out) {
		shownPaths.insert(volume.path);
	}
	GList *volumes = g_volume_monitor_get_volumes(monitor);
	for (GList *l = volumes; l; l = l->next) {
		auto *volume = G_VOLUME(l->data);
		GMount *mount = g_volume_get_mount(volume);
		QString mountPath;
		if (mount) {
			GFile *root = g_mount_get_root(mount);
			char *rawPath = root ? g_file_get_path(root) : nullptr;
			if (rawPath) {
				mountPath = QString::fromUtf8(rawPath);
			}
			g_free(rawPath);
			if (root) {
				g_object_unref(root);
			}
		}
		if (!mountPath.isEmpty() && shownPaths.contains(mountPath)) {
			g_clear_object(&mount);
			continue;
		}

		char *rawDevice = g_volume_get_identifier(volume,
			G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
		const QString device = rawDevice ? QString::fromUtf8(rawDevice) : QString();
		g_free(rawDevice);
		if (device.isEmpty()) {
			g_clear_object(&mount);
			continue;
		}
		const MediaKind kind = mediaKindForDevice(device);
		if (kind != MediaKind::OpticalData &&
				kind != MediaKind::OpticalAudio &&
				kind != MediaKind::OpticalVideo &&
				kind != MediaKind::OpticalBlank) {
			g_clear_object(&mount);
			continue;
		}

		GDrive *drive = g_volume_get_drive(volume);
		if (drive && !g_drive_has_media(drive)) {
			g_clear_object(&drive);
			g_clear_object(&mount);
			continue;
		}

		LocalVolume v;
		v.kind = kind;
		v.device = device;
		v.path = mountPath.isEmpty() ? "media:" + device : mountPath;
		v.mounted = mount != nullptr;
		v.ejectable = g_volume_can_eject(volume) ||
			(drive && g_drive_can_eject(drive));
		char *rawName = g_volume_get_name(volume);
		v.name = rawName && *rawName ? QString::fromUtf8(rawName)
			: kind == MediaKind::OpticalAudio ? QStringLiteral("Audio CD")
			: kind == MediaKind::OpticalBlank ? QStringLiteral("Blank Disc")
			: QStringLiteral("Disc");
		g_free(rawName);
		GFile *activation = g_volume_get_activation_root(volume);
		if (activation) {
			char *uri = g_file_get_uri(activation);
			if (uri) {
				v.activationUri = QString::fromUtf8(uri);
			}
			g_free(uri);
			g_object_unref(activation);
		}
		if (drive) {
			char *rawDrive = g_drive_get_identifier(drive,
				G_DRIVE_IDENTIFIER_KIND_UNIX_DEVICE);
			if (rawDrive && *rawDrive) {
				v.drive = QString::fromUtf8(rawDrive);
			}
			g_free(rawDrive);
		}
		shownPaths.insert(v.path);
		out.push_back(std::move(v));
		g_clear_object(&drive);
		g_clear_object(&mount);
	}
	g_list_free_full(volumes, g_object_unref);
	g_object_unref(monitor);
	QFile table("/proc/self/mountinfo");
	QSet<QString> mounted;
	if (table.open(QIODevice::ReadOnly)) {
		for (const QByteArray &line : table.readAll().split('\n')) {
			const QList<QByteArray> fields = line.split(' ');
			if (fields.size() > 5) {
				mounted.insert(QString::fromUtf8(fields[4]));
			}
		}
	}
	/* The Memory control panel's RAM Disk, for the user it belongs to. */
	if (mounted.contains(RAM_DISK) && QFileInfo(RAM_DISK).isReadable()) {
		LocalVolume ram;
		ram.name = "RAM Disk";
		ram.path = RAM_DISK;
		ram.ejectable = false;
		out.push_back(std::move(ram));
	}
	const QDir records("/run/media/zacos9-mac/" + QString::number(getuid()));
	for (const QString &name : records.entryList({ "*.json" }, QDir::Files | QDir::NoSymLinks)) {
		QFile record(records.filePath(name));
		if (!record.open(QIODevice::ReadOnly)) {
			qWarning() << "Could not read Mac mount record:" << record.fileName();
			continue;
		}
		const QByteArray data = record.readAll();
		const QJsonObject info = QJsonDocument::fromJson(data).object();
		const QString path = info.value("path").toString();
		bool active = mounted.contains(path);
		for (const QJsonValue &value : info.value("paths").toArray()) {
			const QString child = value.toString();
			if (QFileInfo(child).absolutePath() == path && mounted.contains(child)) {
				active = true;
			}
		}
		if (path != records.filePath(name.chopped(5)) || !active) {
			continue;
		}
		const QJsonArray namedVolumes = info.value("volumes").toArray();
		if (!namedVolumes.isEmpty()) {
			for (const LocalVolume &named : localVolumeMacNames(data, path, mounted)) {
				bool shown = false;
				for (LocalVolume &volume : out) {
					if (volume.path == named.path) {
						volume.name = named.name;
						volume.macDevice = named.macDevice;
						shown = true;
					}
				}
				if (!shown) {
					out.push_back(named);
				}
			}
			continue;
		}
		bool shown = false;
		for (LocalVolume &volume : out) {
			if (volume.path == path) {
				volume.macDevice = info.value("device").toString();
				shown = true;
			}
		}
		if (!shown) {
			const QString device = info.value("device").toString();
			LocalVolume volume;
			volume.name = "Mac Disk (" + QFileInfo(device).fileName() + ")";
			volume.path = path;
			volume.ejectable = true;
			volume.macDevice = device;
			volume.device = device;
			out.push_back(std::move(volume));
		}
	}

	/* GVolumeMonitor only knows the volumes UDisks manages. A filesystem
	 * mounted from /etc/fstab — an internal data disk added later — has no
	 * GVolume and would never reach the desktop, though it is as much a
	 * mounted disk as any other. Add block-device mounts it doesn't list;
	 * /, /boot, pseudo filesystems, snap packages and file bind mounts
	 * are not disks. */
	const QString rootDevice = QStorageInfo(QStringLiteral("/")).device();
	for (const QStorageInfo &storage : QStorageInfo::mountedVolumes()) {
		if (!storage.isValid() || !storage.isReady()) {
			continue;
		}
		const QString path = QDir::cleanPath(storage.rootPath());
		if (!localVolumePathShown(path) || isNetworkMount(path) ||
				!QFileInfo(path).isDir() ||
				path == "/boot" || path.startsWith("/boot/") ||
				storage.fileSystemType() == "squashfs") {
			continue;
		}
		const QString device = storage.device();
		if (!device.startsWith("/dev/") || device == rootDevice) {
			continue;
		}
		bool shown = false;
		for (const LocalVolume &volume : out) {
			if (volume.path == path) {
				shown = true;
				break;
			}
		}
		if (shown) {
			continue;
		}
		LocalVolume v;
		v.path = path;
		v.device = device;
		v.kind = mediaKindForDevice(device, &v.backingImage);
		v.name = storage.name().isEmpty() ? QFileInfo(path).fileName() : storage.name();
		v.ejectable = false;
		v.mounted = true;
		v.mediaPresent = true;
		QString zacosName;
		if (localVolumeZacosInfo(path, &zacosName, &v.zacosVersion)) {
			v.zacos = true;
			v.name = zacosName;
		}
		out.push_back(std::move(v));
	}
	return out;
}

/* ---- change notifications ----------------------------------------------- */

static std::vector<std::function<void()>> g_cbs;
static std::vector<std::function<void(const QString &)>> g_errors;
static std::vector<std::function<void(const QString &)>> g_autoplayMissing;
static GVolumeMonitor *g_monitor = nullptr;

static void dispatchChange(GVolumeMonitor *, gpointer, gpointer) {
	for (auto &f : g_cbs) {
		f();
	}
}

void localVolumesOnChange(std::function<void()> f) {
	g_cbs.push_back(std::move(f));
	if (g_monitor) {
		return;
	}

	g_monitor = g_volume_monitor_get();
	/* The mount-added and mount-removed signals cover plug/unplug of USB
	 * drives and mounting/unmounting any volume. The second parameter is
	 * a GMount *; we ignore it and just re-query the full list. */
	g_signal_connect(g_monitor, "mount-added",
		G_CALLBACK(dispatchChange), nullptr);
	g_signal_connect(g_monitor, "mount-removed",
		G_CALLBACK(dispatchChange), nullptr);
}

void localVolumesOnError(std::function<void(const QString &)> f) {
	g_errors.push_back(std::move(f));
}

void localVolumesOnAutoplayMissing(std::function<void(const QString &)> f) {
	g_autoplayMissing.push_back(std::move(f));
}

static void reportMacError(const QString &error) {
	qWarning().noquote() << "zacos9-finder:" << error;
	for (auto &callback : g_errors) {
		callback(error);
	}
}

/* ---- mounting ----------------------------------------------------------- */

static QSet<QString> g_tried; /* volumes already mounted once, by device */
static QSet<QString> g_retried; /* tried a second time after failing */
static QSet<QString> g_mounting;
static QSet<QString> g_erasing;
static std::vector<std::function<void(const QString &)>> g_mountFailed;
static bool belongsToDevice(const QString &path, const QString &device);

static void macTool(const QStringList &arguments,
		std::function<void(const QByteArray &, const QString &)> completed) {
	auto *process = new QProcess(QCoreApplication::instance());
	QObject::connect(process, &QProcess::errorOccurred, process,
		[process, completed](QProcess::ProcessError error) {
			if (error == QProcess::FailedToStart) {
				completed({}, "The Mac disk helper could not start: " + process->errorString());
				process->deleteLater();
			}
		});
	QObject::connect(process, &QProcess::finished, process,
		[process, completed](int code, QProcess::ExitStatus status) {
			/* Quitting kills a helper still running: what asked for it
			 * (the Mac disk poll's timer) may already be gone. */
			if (QCoreApplication::closingDown()) {
				return;
			}
			const QString error = QString::fromUtf8(process->readAllStandardError()).trimmed();
			completed(process->readAllStandardOutput(),
				status == QProcess::NormalExit && code == 0 ? QString()
					: error.isEmpty() ? "The Mac disk helper stopped." : error);
			process->deleteLater();
		});
	process->start(siblingProgram("zacos9-mac-disks"), arguments);
}

void localVolumeMountMacImage(const QString &path,
		std::function<void(const QString &, const QString &)> completed) {
	macTool({ "mount", path }, [completed](const QByteArray &out, const QString &failure) {
		QString error = failure;
		QString mountedPath;
		if (error.isEmpty()) {
			const QJsonObject result = QJsonDocument::fromJson(out).object();
			mountedPath = result.value("path").toString();
			if (mountedPath.isEmpty() || !result.value("volumes").toArray().isEmpty()) {
				const QJsonArray paths = result.value("paths").toArray();
				if (!paths.isEmpty()) {
					mountedPath = paths[0].toString();
				}
			}
			if (!mountedPath.startsWith("/run/media/zacos9-mac/") || !QFileInfo(mountedPath).isDir()) {
				error = "The Mac disk helper returned an invalid mount point.";
				mountedPath.clear();
			}
		}
		for (auto &callback : g_cbs) {
			callback();
		}
		completed(mountedPath, error);
	});
}

static bool sameImageFile(const QString &a, const QString &b) {
	if (a.isEmpty() || b.isEmpty()) {
		return false;
	}
	if (a == b) {
		return true;
	}
	const QString canonicalA = QFileInfo(a).canonicalFilePath();
	const QString canonicalB = QFileInfo(b).canonicalFilePath();
	return !canonicalA.isEmpty() && canonicalA == canonicalB;
}

static QString loopDeviceFromOutput(const QByteArray &output) {
	static const QRegularExpression pattern(QStringLiteral("/dev/loop\\d+"));
	const QRegularExpressionMatch match = pattern.match(QString::fromUtf8(output));
	return match.hasMatch() ? match.captured(0) : QString();
}

static void waitForImageMount(const QString &image, const QString &loopDevice,
		int tries, std::function<void(const QString &, const QString &)> completed) {
	for (const LocalVolume &volume : localVolumes()) {
		if (volume.kind != MediaKind::LoopImage || volume.path.isEmpty() ||
				volume.path.startsWith(QLatin1String("media:"))) {
			continue;
		}
		if (sameImageFile(volume.backingImage, image) ||
				volume.device == loopDevice ||
				volume.device.startsWith(loopDevice + QLatin1Char('p'))) {
			completed(volume.path, {});
			return;
		}
	}
	if (tries <= 0) {
		deleteLoopDevice(loopDevice, image);
		completed({}, QStringLiteral("The image was set up but its filesystem could not be mounted."));
		return;
	}
	QTimer::singleShot(100, QCoreApplication::instance(), [=] {
		waitForImageMount(image, loopDevice, tries - 1, completed);
	});
}

void localVolumeMountImage(const QString &path,
		std::function<void(const QString &, const QString &)> completed) {
	for (const LocalVolume &volume : localVolumes()) {
		if (volume.kind == MediaKind::LoopImage && !volume.path.isEmpty() &&
				!volume.path.startsWith(QLatin1String("media:")) &&
				sameImageFile(volume.backingImage, path)) {
			completed(volume.path, {});
			return;
		}
	}
	auto *process = new QProcess(QCoreApplication::instance());
	QObject::connect(process, &QProcess::errorOccurred, process,
		[process, completed](QProcess::ProcessError error) {
			if (error == QProcess::FailedToStart) {
				completed({}, QStringLiteral("udisksctl isn't installed."));
				process->deleteLater();
			}
		});
	QObject::connect(process, &QProcess::finished, process,
		[process, path, completed](int code, QProcess::ExitStatus status) {
			const QByteArray output = process->readAllStandardOutput() +
				process->readAllStandardError();
			process->deleteLater();
			if (status != QProcess::NormalExit || code != 0) {
				const QString error = QString::fromUtf8(output).trimmed().section('\n', -1);
				completed({}, error.isEmpty()
					? QStringLiteral("udisksctl could not set up the image.") : error);
				return;
			}
			const QString loopDevice = loopDeviceFromOutput(output);
			if (loopDevice.isEmpty()) {
				completed({}, QStringLiteral("udisksctl did not name the loop device."));
				return;
			}
			waitForImageMount(path, loopDevice, 150, completed);
		});
	process->start("udisksctl", { "loop-setup", "--read-only", "--no-user-interaction",
		"-f", path });
}

static QString discImageCacheDir() {
	const QString base = QStandardPaths::writableLocation(
		QStandardPaths::GenericCacheLocation);
	return (base.isEmpty() ? QDir::homePath() + "/.cache" : base) +
		"/zacos9/disc-images";
}

static QString cachedIsoPath(const QString &cuePath, const QString &binPath) {
	const QFileInfo cue(cuePath);
	const QFileInfo bin(binPath);
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData(cue.canonicalFilePath().toUtf8());
	hash.addData(QByteArray::number(cue.size()));
	hash.addData(QByteArray::number(cue.lastModified().toMSecsSinceEpoch()));
	hash.addData(bin.canonicalFilePath().toUtf8());
	hash.addData(QByteArray::number(bin.size()));
	hash.addData(QByteArray::number(bin.lastModified().toMSecsSinceEpoch()));
	return discImageCacheDir() + "/" +
		QString::fromLatin1(hash.result().toHex().left(32)) + ".iso";
}

void localVolumeMountCueImage(const QString &path,
		std::function<void(const QString &, const QString &)> completed) {
	const CueImage cue = cueImageInfo(path);
	if (!cue.error.isEmpty()) {
		completed({}, cue.error);
		return;
	}
	if (cue.tracks > 1) {
		completed({}, QStringLiteral(
			"Only single-track data CUE/BIN images are supported."));
		return;
	}
	if (!cue.data || cue.audio) {
		completed({}, QStringLiteral("Audio CD images cannot be mounted."));
		return;
	}

	const QString iso = cachedIsoPath(path, cue.binPath);
	const auto mountIso = [iso, completed] {
		localVolumeMountImage(iso, [iso, completed](const QString &mounted,
				const QString &error) {
			/* A failed mount leaves a converted image for a later retry;
			 * a failed conversion must not. */
			if (!error.isEmpty()) {
				QFile::remove(iso);
			}
			completed(mounted, error);
		});
	};
	if (QFileInfo(iso).isFile()) {
		mountIso();
		return;
	}

	const QString cache = discImageCacheDir();
	if (!QDir().mkpath(cache)) {
		completed({}, QStringLiteral("The disc-image cache folder could not be created."));
		return;
	}
	auto *process = new QProcess(QCoreApplication::instance());
	QObject::connect(process, &QProcess::errorOccurred, process,
		[process, completed](QProcess::ProcessError error) {
			if (error == QProcess::FailedToStart) {
				completed({}, QStringLiteral("bchunk isn't installed."));
				process->deleteLater();
			}
		});
	QObject::connect(process, &QProcess::finished, process,
		[process, iso, mountIso, completed](int code, QProcess::ExitStatus status) {
			const QByteArray output = process->readAllStandardOutput() +
				process->readAllStandardError();
			process->deleteLater();
			if (status != QProcess::NormalExit || code != 0) {
				QFile::remove(iso);
				const QString error = QString::fromUtf8(output).trimmed().section('\n', -1);
				completed({}, error.isEmpty()
					? QStringLiteral("bchunk could not convert the image.") : error);
				return;
			}
			if (!QFileInfo(iso).isFile() || QFileInfo(iso).size() == 0) {
				QFile::remove(iso);
				completed({}, QStringLiteral("bchunk produced no ISO image."));
				return;
			}
			mountIso();
		});
	process->start("bchunk", { cue.binPath, path, iso.chopped(4) });
}

static void mountMacDevice(const QString &device) {
	if (g_tried.contains(device) || g_mounting.contains(device) || g_erasing.contains(device)) {
		return;
	}
	for (const QString &erasing : g_erasing) {
		if (belongsToDevice(device, erasing)) {
			return;
		}
	}
	g_tried.insert(device);
	g_mounting.insert(device);
	localVolumeMountMacImage(device, [device](const QString &, const QString &error) {
		g_mounting.remove(device);
		if (!error.isEmpty()) {
			reportMacError(device + " could not be mounted read-only. " + error);
		}
	});
}

static bool macFilesystem(const QString &device) {
	QFile number("/sys/class/block/" + QFileInfo(device).fileName() + "/dev");
	if (!number.open(QIODevice::ReadOnly)) {
		return false;
	}
	QFile properties("/run/udev/data/b" + QString::fromUtf8(number.readAll()).trimmed());
	if (!properties.open(QIODevice::ReadOnly)) {
		return false;
	}
	const QByteArray data = properties.readAll();
	for (const char *type : { "hfs", "hfsplus", "apfs" }) {
		if (data.split('\n').contains(QByteArray("E:ID_FS_TYPE=") + type)) {
			return true;
		}
	}
	return false;
}

static bool belongsToDevice(const QString &path, const QString &device) {
	const QString suffix = path.mid(device.size());
	const QString number = suffix.startsWith('p') ? suffix.mid(1) : suffix;
	bool partition = path.startsWith(device) && !number.isEmpty();
	for (QChar c : number) {
		partition = partition && c.isDigit();
	}
	return path == device || partition;
}

void localVolumeInhibitMount(const QString &device, bool inhibit) {
	if (inhibit) {
		g_erasing.insert(device);
	} else {
		g_erasing.remove(device);
		localVolumeMountDevice(device);
	}
}

void localVolumesOnMountFailed(std::function<void(const QString &)> f) {
	g_mountFailed.push_back(std::move(f));
}

static QString volumeName(GVolume *v) {
	char *raw = g_volume_get_name(v);
	const QString name = QString::fromUtf8(raw ? raw : "");
	g_free(raw);
	return name;
}

static void mountIfNeeded(GVolume *v);

static void reportMountFailed(const QString &device) {
	for (auto &callback : g_mountFailed) {
		callback(device);
	}
}

/* The second try failed too: read-only, through UDisks (GIO can't ask for
 * it). A volume a crash left marked in use (NTFS not closed cleanly)
 * mounts so; Disk Setup's Repair clears the mark. Only if this fails too
 * is the volume unreadable, which may offer to initialize it. */
static void mountReadOnly(const QString &device, const QString &name) {
	if (!QDBusConnection::systemBus().isConnected()) {
		reportMountFailed(device); /* no UDisks to ask */
		return;
	}
	const QString udisks = "org.freedesktop.UDisks2";
	QDBusMessage resolve = QDBusMessage::createMethodCall(udisks, "/org/freedesktop/UDisks2/Manager",
		"org.freedesktop.UDisks2.Manager", "ResolveDevice");
	resolve.setArguments({ QVariantMap{ { "path", device } }, QVariantMap() });
	auto *app = QCoreApplication::instance();
	auto *found = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(resolve), app);
	QObject::connect(found, &QDBusPendingCallWatcher::finished, app, [=] {
		found->deleteLater();
		const QDBusReply<QList<QDBusObjectPath>> objects = *found;
		if (!objects.isValid() || objects.value().isEmpty()) {
			reportMountFailed(device);
			return;
		}
		QDBusMessage mount = QDBusMessage::createMethodCall(udisks, objects.value().front().path(),
			"org.freedesktop.UDisks2.Filesystem", "Mount");
		mount.setArguments({ QVariantMap{ { "options", "ro" } } });
		/* Automatic, like the first try: never a password prompt, which at
		 * login (or for a first-run account, not yet an administrator)
		 * asked for root's password for another system's partition. */
		mount.setInteractiveAuthorizationAllowed(false);
		auto *mounted = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(mount), app);
		QObject::connect(mounted, &QDBusPendingCallWatcher::finished, app, [=] {
			mounted->deleteLater();
			if (mounted->isError()) {
				fprintf(stderr, "zacos9-finder: \"%s\" couldn't be mounted read-only either: %s\n",
					qPrintable(name), qPrintable(mounted->error().message()));
				reportMountFailed(device);
				return;
			}
			const QString notice = QString("“%1” wasn't put away properly, so it has been opened "
				"read-only. To use it normally again, open Disk Setup (Applications ▸ Utilities) "
				"and click Repair.").arg(name);
			for (auto &callback : g_errors) {
				callback(notice);
			}
		});
	});
}

static void onMounted(GObject *src, GAsyncResult *res, gpointer) {
	char *rawDevice = g_volume_get_identifier(G_VOLUME(src), G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
	const QString mountedDevice = QString::fromUtf8(rawDevice ? rawDevice : "");
	g_free(rawDevice);
	g_mounting.remove(mountedDevice);
	GError *err = nullptr;
	if (!g_volume_mount_finish(G_VOLUME(src), res, &err) && err) {
		const QString name = volumeName(G_VOLUME(src));
		fprintf(stderr, "zacos9-finder: \"%s\" couldn't be mounted: %s\n", qPrintable(name), err->message);
		/* Not allowed (UDisks says so in words, through GIO): not a
		 * damaged volume, so no read-only try. */
		const bool refused = strstr(err->message, "Not authorized") || strstr(err->message, "not authorized");
		if (!g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
				!g_error_matches(err, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED) &&
				!g_error_matches(err, G_IO_ERROR, G_IO_ERROR_FAILED_HANDLED) &&
				!refused && !mountedDevice.isEmpty()) {
			/* At login a drive can fail while UDisks is still starting:
			 * once more a moment later, then read-only. */
			if (!g_retried.contains(mountedDevice)) {
				g_retried.insert(mountedDevice);
				GVolume *volume = G_VOLUME(g_object_ref(src));
				QTimer::singleShot(3000, QCoreApplication::instance(), [volume, mountedDevice] {
					g_tried.remove(mountedDevice);
					mountIfNeeded(volume);
					g_object_unref(volume);
				});
			} else {
				mountReadOnly(mountedDevice, name);
			}
		}
		g_error_free(err);
	}
	for (auto &callback : g_cbs) {
		callback();
	}
	/* Success: mount-added follows, and the desktop shows the disk. */
}

static void mountIfNeeded(GVolume *v) {
	if (GMount *m = g_volume_get_mount(v)) {
		g_object_unref(m);
		return;
	}
	char *dev = g_volume_get_identifier(v, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
	if (!dev) {
		return; /* not a drive: a network share, a phone, ... */
	}
	const QString key = QString::fromUtf8(dev);
	g_free(dev);
	for (const QString &device : g_erasing) {
		if (belongsToDevice(key, device)) {
			return;
		}
	}
	/* The computer's own disks: not USB, not a card or disc. */
	bool internal = false;
	if (GDrive *drive = g_volume_get_drive(v)) {
		internal = !g_drive_is_removable(drive) && !g_drive_is_media_removable(drive);
		g_object_unref(drive);
	}
	char *uuid = g_volume_get_identifier(v, G_VOLUME_IDENTIFIER_KIND_UUID);
	/* Disk Setup's "Mount automatically", off for this volume; and in the
	 * live session, off for internal disks until turned on (mountpolicy.h). */
	const bool automatic = volumeMountsAutomatically(QString::fromUtf8(uuid ? uuid : ""), internal);
	g_free(uuid);
	if (internal && !automatic && liveSession()) {
		return;
	}
	if (macFilesystem(key)) {
		// Image loops are mounted by their owning image operation.
		if (!key.startsWith("/dev/loop")) {
			mountMacDevice(key);
		}
		return;
	}
	if (!g_volume_can_mount(v)) {
		return;
	}
	if (!automatic) {
		return;
	}
	if (g_tried.contains(key) || g_mounting.contains(key)) {
		return;
	}
	g_tried.insert(key);
	g_mounting.insert(key);
	g_volume_mount(v, G_MOUNT_MOUNT_NONE, nullptr, nullptr, onMounted, nullptr);
}

/* Mac OS 9's AutoPlay (Sniffer Preferences > Media, "media-autoplay", on
 * unless "false"): an audio CD or a video DVD just inserted plays in the
 * program that plays that kind of disc. The guess runs asynchronously, and
 * only physical optical media are considered. Programs on a disc never
 * start. */
static MediaKind mountMediaKind(GMount *mount) {
	GVolume *volume = g_mount_get_volume(mount);
	if (!volume) {
		return MediaKind::Disk;
	}
	char *raw = g_volume_get_identifier(volume,
		G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
	const QString device = raw ? QString::fromUtf8(raw) : QString();
	g_free(raw);
	g_object_unref(volume);
	return device.isEmpty() ? MediaKind::Disk : mediaKindForDevice(device);
}

static void onMountAdded(GVolumeMonitor *, GMount *mount, gpointer) {
	if (!mediaAutoplayEnabled()) {
		return;
	}
	mediaAutoplayMount(mount, mountMediaKind(mount),
		[](const QString &type) {
			for (auto &callback : g_autoplayMissing) {
				callback(type);
			}
		});
}

static void onMountRemoved(GVolumeMonitor *, GMount *mount, gpointer) {
	mediaAutoplayForget(mount);
}

static void onVolumeAdded(GVolumeMonitor *, GVolume *v, gpointer) {
	mountIfNeeded(v);
	for (auto &callback : g_cbs) {
		callback();
	}
}

static void onVolumeRemoved(GVolumeMonitor *, GVolume *v, gpointer) {
	char *device = g_volume_get_identifier(v, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
	if (device) {
		g_tried.remove(QString::fromUtf8(device));
		g_retried.remove(QString::fromUtf8(device));
		g_free(device);
	}
	for (auto &callback : g_cbs) {
		callback();
	}
}

void localVolumeMountDevice(const QString &device) {
	GVolumeMonitor *monitor = g_volume_monitor_get();
	GList *volumes = g_volume_monitor_get_volumes(monitor);
	for (GList *item = volumes; item; item = item->next) {
		auto *volume = G_VOLUME(item->data);
		char *raw = g_volume_get_identifier(volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
		const QString path = QString::fromUtf8(raw ? raw : "");
		g_free(raw);
		if (belongsToDevice(path, device)) {
			g_tried.remove(path);
			mountIfNeeded(volume);
		}
	}
	g_list_free_full(volumes, g_object_unref);
	g_object_unref(monitor);
}

void localVolumesMountAll() {
	static GVolumeMonitor *monitor = nullptr;
	if (monitor) {
		return;
	}
	/* Qt's Wayland dispatcher need not service GLib's default context.
	 * Bound each pass so a busy GIO source cannot monopolize the GUI. */
	auto *events = new QTimer(QCoreApplication::instance());
	events->setInterval(25);
	QObject::connect(events, &QTimer::timeout, [] {
		for (int i = 0; i < 32 && g_main_context_iteration(nullptr, FALSE); ++i) {}
	});
	events->start();
	monitor = g_volume_monitor_get();
	g_signal_connect(monitor, "volume-added", G_CALLBACK(onVolumeAdded), nullptr);
	g_signal_connect(monitor, "volume-changed", G_CALLBACK(onVolumeAdded), nullptr);
	g_signal_connect(monitor, "volume-removed", G_CALLBACK(onVolumeRemoved), nullptr);
	g_signal_connect(monitor, "mount-added", G_CALLBACK(onMountAdded), nullptr);
	g_signal_connect(monitor, "mount-removed", G_CALLBACK(onMountRemoved), nullptr);
	GList *volumes = g_volume_monitor_get_volumes(monitor);
	for (GList *l = volumes; l; l = l->next) {
		mountIfNeeded(G_VOLUME(l->data));
	}
	g_list_free_full(volumes, g_object_unref);
	/* The RAM Disk has no GVolume, so no mount-added: looked for. */
	auto *ramPoll = new QTimer(QCoreApplication::instance());
	ramPoll->setInterval(3000);
	ramPoll->setProperty("mounted", ramDiskMounted());
	QObject::connect(ramPoll, &QTimer::timeout, ramPoll, [ramPoll] {
		const bool now = ramDiskMounted();
		if (now != ramPoll->property("mounted").toBool()) {
			ramPoll->setProperty("mounted", now);
			for (auto &callback : g_cbs) {
				callback();
			}
		}
	});
	ramPoll->start();
	auto *macPoll = new QTimer(QCoreApplication::instance());
	macPoll->setInterval(3000);
	QObject::connect(macPoll, &QTimer::timeout, macPoll, [macPoll] {
		if (macPoll->property("busy").toBool()) {
			return;
		}
		macPoll->setProperty("busy", true);
		macTool({ "scan" }, [macPoll](const QByteArray &out, const QString &error) {
			macPoll->setProperty("busy", false);
			if (!error.isEmpty()) {
				qWarning().noquote() << "Mac disk discovery:" << error;
				macPoll->stop();
				return;
			}
			const QJsonDocument document = QJsonDocument::fromJson(out);
			if (!document.isArray()) {
				qWarning() << "Invalid Mac disk discovery response";
				macPoll->stop();
				return;
			}
			QSet<QString> present;
			for (const QJsonValue &value : document.array()) {
				const QString device = value.toString();
				if (device.startsWith("/dev/") && !device.startsWith("/dev/loop")) {
					present.insert(device);
					mountMacDevice(device);
				}
			}
			const QSet<QString> previous = macPoll->property("devices").value<QSet<QString>>();
			for (const QString &device : previous - present) {
				g_tried.remove(device);
			}
			macPoll->setProperty("devices", QVariant::fromValue(present));
			for (auto &callback : g_cbs) {
				callback();
			}
		});
	});
	macPoll->start();
	/* PC floppy drives cannot report media changes: poll their change
	 * line, and mount or unmount when a disk arrives or leaves. */
	floppyPollStart([](const QString &device, bool present) {
		if (present) {
			g_tried.remove(device);
			g_retried.remove(device);
			localVolumeMountDevice(device);
		} else {
			for (const LocalVolume &volume : localVolumes()) {
				if (volume.mounted && belongsToDevice(volume.device, device)) {
					localVolumeEject(volume);
				}
			}
		}
		for (auto &callback : g_cbs) {
			callback();
		}
	});
}

/* ---- unmounted media ---------------------------------------------------- */

/* A GVolume for a LocalVolume that has no native mount path (an audio CD
 * or blank disc), or for one that is not mounted yet. Matched by device,
 * then by activation URI. */
static GVolume *findVolumeForMedia(const LocalVolume &media) {
	GVolumeMonitor *monitor = g_volume_monitor_get();
	GList *volumes = g_volume_monitor_get_volumes(monitor);
	GVolume *found = nullptr;
	for (GList *l = volumes; l && !found; l = l->next) {
		auto *volume = G_VOLUME(l->data);
		char *rawDevice = g_volume_get_identifier(volume,
			G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
		const QString device = rawDevice ? QString::fromUtf8(rawDevice) : QString();
		g_free(rawDevice);
		bool matches = !media.device.isEmpty() && media.device == device;
		if (!matches && !media.activationUri.isEmpty()) {
			GFile *activation = g_volume_get_activation_root(volume);
			if (activation) {
				char *uri = g_file_get_uri(activation);
				matches = uri && media.activationUri == QString::fromUtf8(uri);
				g_free(uri);
				g_object_unref(activation);
			}
		}
		if (matches) {
			found = G_VOLUME(g_object_ref(volume));
		}
	}
	g_list_free_full(volumes, g_object_unref);
	g_object_unref(monitor);
	return found;
}

struct MediaTargets {
	GMount *mount = nullptr;
	GDrive *drive = nullptr;
};

/* The GIO mount and drive for a LocalVolume, by its volume (works for a
 * pseudo media path) or, as a fallback, its native mount path. */
static MediaTargets findMediaTargets(const LocalVolume &media) {
	MediaTargets targets;
	GVolume *volume = findVolumeForMedia(media);
	if (volume) {
		targets.mount = g_volume_get_mount(volume);
		targets.drive = g_volume_get_drive(volume);
		g_object_unref(volume);
	}
	if (!targets.mount && media.mounted &&
			!media.path.startsWith(QLatin1String("media:"))) {
		GVolumeMonitor *monitor = g_volume_monitor_get();
		GList *mounts = g_volume_monitor_get_mounts(monitor);
		for (GList *l = mounts; l && !targets.mount; l = l->next) {
			auto *mount = G_MOUNT(l->data);
			GFile *root = g_mount_get_root(mount);
			char *rawPath = root ? g_file_get_path(root) : nullptr;
			if (rawPath && media.path == QString::fromUtf8(rawPath)) {
				targets.mount = G_MOUNT(g_object_ref(mount));
				GVolume *mounted = g_mount_get_volume(mount);
				if (mounted) {
					targets.drive = g_volume_get_drive(mounted);
					g_object_unref(mounted);
				}
			}
			g_free(rawPath);
			if (root) {
				g_object_unref(root);
			}
		}
		g_list_free_full(mounts, g_object_unref);
		g_object_unref(monitor);
	}
	return targets;
}

static QString mountRootPath(GMount *mount) {
	GFile *root = g_mount_get_root(mount);
	char *rawPath = root ? g_file_get_path(root) : nullptr;
	const QString path = rawPath ? QString::fromUtf8(rawPath) : QString();
	g_free(rawPath);
	if (root) {
		g_object_unref(root);
	}
	return path;
}

struct OpenContext {
	std::function<void(const QString &, const QString &)> completed;
};

static void onOpenMounted(GObject *source, GAsyncResult *result, gpointer data) {
	auto *context = static_cast<OpenContext *>(data);
	GError *error = nullptr;
	if (!g_volume_mount_finish(G_VOLUME(source), result, &error)) {
		context->completed({}, QString::fromUtf8(error ? error->message
			: "The disc could not be mounted."));
		g_clear_error(&error);
		delete context;
		return;
	}
	GMount *mount = g_volume_get_mount(G_VOLUME(source));
	const QString path = mount ? mountRootPath(mount) : QString();
	if (mount) {
		g_object_unref(mount);
	}
	context->completed(path, path.isEmpty()
		? QStringLiteral("The disc mounted but has no files to open.")
		: QString());
	delete context;
}

void localVolumeOpen(const LocalVolume &media,
		std::function<void(const QString &, const QString &)> completed) {
	GVolume *volume = findVolumeForMedia(media);
	if (!volume) {
		completed({}, QStringLiteral("The disc is no longer inserted."));
		return;
	}
	if (GMount *mount = g_volume_get_mount(volume)) {
		const QString path = mountRootPath(mount);
		g_object_unref(mount);
		g_object_unref(volume);
		completed(path, path.isEmpty()
			? QStringLiteral("The disc has no files to open.")
			: QString());
		return;
	}
	if (!g_volume_can_mount(volume)) {
		g_object_unref(volume);
		completed({}, QStringLiteral("This disc cannot be opened."));
		return;
	}
	auto *context = new OpenContext{ std::move(completed) };
	g_volume_mount(volume, G_MOUNT_MOUNT_NONE, nullptr, nullptr,
		onOpenMounted, context);
	g_object_unref(volume);
}

/* ---- eject -------------------------------------------------------------- */

static void onEjectDone(GObject *src, GAsyncResult *res, gpointer wasEject) {
	GMount *mount = G_MOUNT(src);
	GError *err = nullptr;
	if (wasEject) {
		g_mount_eject_with_operation_finish(mount, res, &err);
	} else {
		g_mount_unmount_with_operation_finish(mount, res, &err);
	}
	if (err) {
		reportMacError(QStringLiteral("The disk could not be unmounted. ") +
			QString::fromUtf8(err->message));
		g_error_free(err);
	}
	/* GVolumeMonitor fires mount-removed on success, which triggers
	 * the registered localVolumesOnChange callbacks automatically. */
}

static void onDriveEjectDone(GObject *source, GAsyncResult *result, gpointer) {
	GError *error = nullptr;
	g_drive_eject_with_operation_finish(G_DRIVE(source), result, &error);
	if (error) {
		reportMacError(QStringLiteral("The disc could not be ejected. ") +
			QString::fromUtf8(error->message));
		g_error_free(error);
	}
	for (auto &callback : g_cbs) {
		callback();
	}
}

static QString loopBackingFile(const QString &device) {
	QFile file("/sys/class/block/" + QFileInfo(device).fileName() + "/loop/backing_file");
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return QString::fromUtf8(file.read(4096)).trimmed();
}

static bool loopDeviceExists(const QString &device) {
	const QString name = QFileInfo(device).fileName();
	return QFileInfo::exists("/sys/class/block/" + name + "/loop/backing_file");
}

/* The loop's filesystem is gone: remove the loop device too. gvfs may
 * have already done this through autoclear, so a failure after it has
 * disappeared is not an error. A converted CUE/BIN image in our cache is
 * deleted with its loop. */
static void deleteLoopDevice(const QString &device, const QString &backingImage) {
	QString backing = backingImage;
	if (backing.isEmpty()) {
		backing = loopBackingFile(device);
	}
	const auto finish = [backing] {
		if (!backing.isEmpty()) {
			const QString cache = discImageCacheDir();
			const QFileInfo file(backing);
			if (file.isFile() && !file.isSymLink() &&
					file.absoluteFilePath().startsWith(cache + QLatin1Char('/'))) {
				QFile::remove(backing);
			}
		}
		for (auto &callback : g_cbs) {
			callback();
		}
	};
	if (!device.startsWith(QLatin1String("/dev/loop")) || !loopDeviceExists(device)) {
		finish();
		return;
	}
	auto *process = new QProcess(QCoreApplication::instance());
	QObject::connect(process, &QProcess::errorOccurred, process,
		[process, finish](QProcess::ProcessError error) {
			if (error == QProcess::FailedToStart) {
				qWarning().noquote() << "zacos9-finder: udisksctl could not remove the loop device:"
					<< process->errorString();
				process->deleteLater();
				finish();
			}
		});
	QObject::connect(process, &QProcess::finished, process,
		[process, device, finish](int code, QProcess::ExitStatus status) {
			if (status != QProcess::NormalExit || code != 0) {
				const QString error = QString::fromUtf8(
					process->readAllStandardError()).trimmed();
				if (loopDeviceExists(device)) {
					qWarning().noquote() << "zacos9-finder: the loop device could not be removed:"
						<< (error.isEmpty() ? QStringLiteral("udisksctl failed") : error);
				}
			}
			process->deleteLater();
			finish();
		});
	process->start("udisksctl", { "loop-delete", "--no-user-interaction", "-b", device });
}

struct EjectContext {
	LocalVolume volume;
	GDrive *drive = nullptr;
};

static void onUnmountForEject(GObject *source, GAsyncResult *result, gpointer data) {
	auto *context = static_cast<EjectContext *>(data);
	GError *error = nullptr;
	if (!g_mount_unmount_with_operation_finish(G_MOUNT(source), result, &error)) {
		reportMacError(QStringLiteral("The disk could not be unmounted. ") +
			QString::fromUtf8(error ? error->message : "Unknown error."));
		g_clear_error(&error);
	} else if (context->drive && g_drive_can_eject(context->drive)) {
		g_drive_eject_with_operation(context->drive, G_MOUNT_UNMOUNT_NONE, nullptr,
			nullptr, onDriveEjectDone, nullptr);
	} else if (context->volume.kind == MediaKind::LoopImage ||
			!context->volume.backingImage.isEmpty()) {
		deleteLoopDevice(context->volume.device, context->volume.backingImage);
	} else {
		for (auto &callback : g_cbs) {
			callback();
		}
	}
	g_clear_object(&context->drive);
	delete context;
}

bool localVolumeEject(const LocalVolume &v) {
	if (!v.macDevice.isEmpty()) {
		macTool({ "unmount", v.macDevice }, [](const QByteArray &, const QString &error) {
			if (!error.isEmpty()) {
				reportMacError("The Mac disk could not be ejected. " + error);
			}
			for (auto &callback : g_cbs) {
				callback();
			}
		});
		return true;
	}

	const bool loopImage = v.kind == MediaKind::LoopImage || !v.backingImage.isEmpty();
	const EjectPlan plan = mediaEjectPlan(v.kind, v.mounted, v.ejectable, loopImage);
	if (plan.action == EjectAction::None) {
		return false;
	}

	MediaTargets targets = findMediaTargets(v);
	bool started = false;
	switch (plan.action) {
	case EjectAction::None:
		break;
	case EjectAction::DriveEjectOnly:
		if (targets.drive && g_drive_can_eject(targets.drive)) {
			g_drive_eject_with_operation(targets.drive, G_MOUNT_UNMOUNT_NONE,
				nullptr, nullptr, onDriveEjectDone, nullptr);
			started = true;
		} else if (targets.mount) {
			g_mount_unmount_with_operation(targets.mount, G_MOUNT_UNMOUNT_NONE,
				nullptr, nullptr, onEjectDone, nullptr);
			started = true;
		}
		break;
	case EjectAction::UnmountThenDriveEject:
		if (targets.mount) {
			auto *context = new EjectContext{ v, targets.drive
				? G_DRIVE(g_object_ref(targets.drive)) : nullptr };
			g_mount_unmount_with_operation(targets.mount, G_MOUNT_UNMOUNT_NONE,
				nullptr, nullptr, onUnmountForEject, context);
			started = true;
		} else if (targets.drive && g_drive_can_eject(targets.drive)) {
			g_drive_eject_with_operation(targets.drive, G_MOUNT_UNMOUNT_NONE,
				nullptr, nullptr, onDriveEjectDone, nullptr);
			started = true;
		}
		break;
	case EjectAction::UnmountOnly:
		if (targets.mount) {
			g_mount_unmount_with_operation(targets.mount, G_MOUNT_UNMOUNT_NONE,
				nullptr, nullptr, onEjectDone, nullptr);
			started = true;
		}
		break;
	case EjectAction::UnmountThenLoopDelete:
		if (targets.mount) {
			auto *context = new EjectContext{ v, nullptr };
			g_mount_unmount_with_operation(targets.mount, G_MOUNT_UNMOUNT_NONE,
				nullptr, nullptr, onUnmountForEject, context);
			started = true;
		} else if (v.device.startsWith(QLatin1String("/dev/loop"))) {
			deleteLoopDevice(v.device, v.backingImage);
			started = true;
		}
		break;
	}
	g_clear_object(&targets.drive);
	g_clear_object(&targets.mount);
	return started;
}
