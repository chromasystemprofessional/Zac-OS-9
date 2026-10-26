#include "gpuinfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <cctype>

namespace {

QString read(const QString &path, qint64 limit = 64 * 1024) {
	QFile f(path);
	return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.read(limit)).trimmed() : QString();
}

bool isHex4(const QString &line) {
	if (line.size() < 4) {
		return false;
	}
	for (int i = 0; i < 4; ++i) {
		if (!std::isxdigit(static_cast<unsigned char>(line[i].toLatin1()))) {
			return false;
		}
	}
	return true;
}

struct Ids {
	QHash<QString, QString> vendors, devices;
};

const Ids &ids(const QString &file) {
	static QHash<QString, Ids> cache;
	auto it = cache.find(file);
	if (it != cache.end()) {
		return *it;
	}
	Ids loaded;
	QFile f(file);
	if (!file.isEmpty() && f.open(QIODevice::ReadOnly)) {
		QString vendor;
		while (!f.atEnd()) {
			const QString line = QString::fromUtf8(f.readLine()).chopped(1);
			if (line.isEmpty() || line.startsWith('#')) {
				continue;
			}
			if (line.startsWith("\t\t")) {
				continue;
			}
			if (line.startsWith('\t')) {
				if (!vendor.isEmpty() && line.size() > 6) {
					loaded.devices.insert(vendor + ":" + line.mid(1, 4).toLower(), line.mid(7).trimmed());
				}
				continue;
			}
			if (line.size() > 6 && line[4] == ' ' && isHex4(line)) {
				vendor = line.left(4).toLower();
				loaded.vendors.insert(vendor, line.mid(6).trimmed());
			} else {
				vendor.clear();
			}
		}
	}
	return *cache.insert(file, loaded);
}

QString idsFile(const QString &root) {
	for (const char *dir : { "/usr/share/misc/", "/usr/share/hwdata/", "/var/lib/usbutils/" }) {
		const QString path = root + dir + "pci.ids";
		if (QFileInfo::exists(path)) {
			return path;
		}
	}
	return QString();
}

QString id(const QString &path) {
	QString value = read(path).toLower();
	return value.startsWith("0x") ? value.mid(2) : value;
}

QString driverAt(const QString &path) {
	const QFileInfo info(path);
	QString target = info.canonicalFilePath();
	if (target.isEmpty()) {
		target = info.symLinkTarget();
	}
	return target.isEmpty() ? QString() : QFileInfo(target).fileName();
}

QString nvidiaModel(const QString &root, const QString &slot) {
	if (slot.isEmpty()) {
		return QString();
	}
	for (const QString &line : read(root + "/proc/driver/nvidia/gpus/" + slot + "/information").split('\n')) {
		if (line.startsWith("Model:")) {
			return line.mid(6).trimmed();
		}
	}
	return QString();
}

QString genericHardware(const QString &driver) {
	if (driver == "i915" || driver == "xe") {
		return "Intel GPU";
	}
	if (driver == "amdgpu" || driver == "radeon") {
		return "AMD GPU";
	}
	if (driver == "nvidia" || driver == "nouveau") {
		return "NVIDIA GPU";
	}
	if (driver == "virtio_gpu" || driver == "virtio-pci") {
		return "Virtio GPU";
	}
	if (driver == "vmwgfx") {
		return "VMware SVGA GPU";
	}
	if (driver == "qxl") {
		return "QXL GPU";
	}
	if (driver == "vboxvideo") {
		return "VirtualBox GPU";
	}
	return QString();
}

QString withDriver(const QString &hardware, const QString &driver) {
	return driver.isEmpty() ? hardware : hardware + " (" + driver + ")";
}

} // namespace

QString gpuDescription(const QString &root) {
	const QString file = idsFile(root);
	const Ids &table = ids(file);
	QStringList descriptions;
	for (const QString &card : QDir(root + "/sys/class/drm").entryList(
			QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
		if (!card.startsWith("card") || card.contains('-')) {
			continue;
		}
		bool numeric = false;
		card.mid(4).toInt(&numeric);
		if (!numeric) {
			continue;
		}
		const QString base = root + "/sys/class/drm/" + card;
		const QString driver = driverAt(base + "/device/driver");
		const QString vendor = id(base + "/device/vendor");
		const QString device = id(base + "/device/device");
		QString hardware = table.devices.value(vendor + ":" + device);
		if (hardware.isEmpty()) {
			hardware = nvidiaModel(root, QFileInfo(base + "/device").canonicalFilePath().section('/', -1));
		}
		if (hardware.isEmpty()) {
			hardware = table.vendors.value(vendor);
			if (!hardware.isEmpty()) {
				hardware += " GPU";
			}
		}
		if (hardware.isEmpty()) {
			hardware = genericHardware(driver);
		}
		if (hardware.isEmpty() && !driver.isEmpty()) {
			hardware = "PCI graphics adapter";
		}
		if (hardware.isEmpty()) {
			continue;
		}
		const QString description = withDriver(hardware, driver);
		if (!descriptions.contains(description)) {
			descriptions << description;
		}
	}
	if (descriptions.isEmpty()) {
		for (const QString &slot : QDir(root + "/proc/driver/nvidia/gpus").entryList(
				QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
			const QString model = nvidiaModel(root, slot);
			if (!model.isEmpty()) {
				descriptions << withDriver(model, "nvidia");
			}
		}
	}
	return descriptions.join(", ");
}
