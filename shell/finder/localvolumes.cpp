/* Before anything of Qt's: Qt defines `signals` as a keyword macro, and
 * GIO's D-Bus headers have a struct field of that name. */
#include <gio/gio.h>

#include "localvolumes.h"

#include <QFileInfo>
#include <QSet>
#include <QCoreApplication>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QDebug>
#include <unistd.h>
#include <cstdio>

#include "netvolumes.h"

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
		volumes.push_back({ label, path, true, info.value("device").toString() });
	}
	return volumes;
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
		g_object_unref(vol);
		out.push_back(std::move(v));
	}

	g_list_free_full(mounts, g_object_unref);
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
			out.push_back({ "Mac Disk (" + QFileInfo(device).fileName() + ")", path, true, device });
		}
	}
	return out;
}

/* ---- change notifications ----------------------------------------------- */

static std::vector<std::function<void()>> g_cbs;
static std::vector<std::function<void(const QString &)>> g_errors;
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

static void reportMacError(const QString &error) {
	qWarning().noquote() << "zacos9-finder:" << error;
	for (auto &callback : g_errors) {
		callback(error);
	}
}

/* ---- mounting ----------------------------------------------------------- */

static QSet<QString> g_tried; /* volumes already mounted once, by device */
static QSet<QString> g_mounting;
static QSet<QString> g_erasing;
static std::vector<std::function<void(const QString &)>> g_mountFailed;
static bool belongsToDevice(const QString &path, const QString &device);

static void macTool(const QStringList &arguments,
		std::function<void(const QByteArray &, const QString &)> completed) {
	auto *process = new QProcess(QCoreApplication::instance());
	const QString local = QCoreApplication::applicationDirPath() + "/zacos9-mac-disks";
	QObject::connect(process, &QProcess::errorOccurred, process,
		[process, completed](QProcess::ProcessError error) {
			if (error == QProcess::FailedToStart) {
				completed({}, "The Mac disk helper could not start: " + process->errorString());
				process->deleteLater();
			}
		});
	QObject::connect(process, &QProcess::finished, process,
		[process, completed](int code, QProcess::ExitStatus status) {
			const QString error = QString::fromUtf8(process->readAllStandardError()).trimmed();
			completed(process->readAllStandardOutput(),
				status == QProcess::NormalExit && code == 0 ? QString()
					: error.isEmpty() ? "The Mac disk helper stopped." : error);
			process->deleteLater();
		});
	process->start(QFileInfo(local).isExecutable() ? local : QStringLiteral("zacos9-mac-disks"), arguments);
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

static void onMounted(GObject *src, GAsyncResult *res, gpointer) {
	char *rawDevice = g_volume_get_identifier(G_VOLUME(src), G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
	const QString mountedDevice = QString::fromUtf8(rawDevice ? rawDevice : "");
	g_free(rawDevice);
	g_mounting.remove(mountedDevice);
	GError *err = nullptr;
	if (!g_volume_mount_finish(G_VOLUME(src), res, &err) && err) {
		fprintf(stderr, "zacos9-finder: \"%s\" couldn't be mounted: %s\n",
			qPrintable(volumeName(G_VOLUME(src))), err->message);
		if (!g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
				!g_error_matches(err, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED) &&
				!g_error_matches(err, G_IO_ERROR, G_IO_ERROR_FAILED_HANDLED)) {
			char *device = g_volume_get_identifier(G_VOLUME(src), G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
			if (device) {
				for (auto &callback : g_mountFailed) {
					callback(QString::fromUtf8(device));
				}
				g_free(device);
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
	if (g_tried.contains(key) || g_mounting.contains(key)) {
		return;
	}
	g_tried.insert(key);
	g_mounting.insert(key);
	g_volume_mount(v, G_MOUNT_MOUNT_NONE, nullptr, nullptr, onMounted, nullptr);
}

static void onVolumeAdded(GVolumeMonitor *, GVolume *v, gpointer) {
	mountIfNeeded(v);
}

static void onVolumeRemoved(GVolumeMonitor *, GVolume *v, gpointer) {
	char *device = g_volume_get_identifier(v, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
	if (device) {
		g_tried.remove(QString::fromUtf8(device));
		g_free(device);
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
	GList *volumes = g_volume_monitor_get_volumes(monitor);
	for (GList *l = volumes; l; l = l->next) {
		mountIfNeeded(G_VOLUME(l->data));
	}
	g_list_free_full(volumes, g_object_unref);
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
	GVolumeMonitor *monitor = g_volume_monitor_get();
	GList *mounts = g_volume_monitor_get_mounts(monitor);
	GMount *found = nullptr;
	for (GList *l = mounts; l; l = l->next) {
		auto *mount = G_MOUNT(l->data);
		GFile *root = g_mount_get_root(mount);
		char *path = g_file_get_path(root);
		g_object_unref(root);
		if (path && v.path == QString::fromUtf8(path)) {
			found = G_MOUNT(g_object_ref(mount));
		}
		g_free(path);
		if (found) {
			break;
		}
	}
	g_list_free_full(mounts, g_object_unref);
	g_object_unref(monitor);
	if (!found) {
		return false;
	}
	if (v.ejectable && g_mount_can_eject(found)) {
		g_mount_eject_with_operation(found, G_MOUNT_UNMOUNT_NONE, nullptr,
			nullptr, onEjectDone, reinterpret_cast<gpointer>(1));
	} else {
		g_mount_unmount_with_operation(found, G_MOUNT_UNMOUNT_NONE, nullptr,
			nullptr, onEjectDone, nullptr);
	}
	g_object_unref(found);
	return true;
}
