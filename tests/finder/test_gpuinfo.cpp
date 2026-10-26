#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include "gpuinfo.h"

static int fails;
static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

static void put(const QString &root, const QString &path, const QByteArray &data) {
	QDir().mkpath(QFileInfo(root + path).path());
	QFile f(root + path);
	f.open(QIODevice::WriteOnly);
	f.write(data);
}

static void link(const QString &root, const QString &target, const QString &path) {
	QDir().mkpath(QFileInfo(root + path).path());
	QFile::link(root + target, root + path);
}

static void card(const QString &root, const QString &name, const QString &vendor,
		const QString &device, const QString &driver) {
	put(root, "/sys/class/drm/" + name + "/device/vendor", vendor.toUtf8() + "\n");
	put(root, "/sys/class/drm/" + name + "/device/device", device.toUtf8() + "\n");
	QDir().mkpath(root + "/sys/bus/pci/drivers/" + driver);
	link(root, "/sys/bus/pci/drivers/" + driver, "/sys/class/drm/" + name + "/device/driver");
}

int main() {
	QTemporaryDir first;
	put(first.path(), "/usr/share/misc/pci.ids",
		"10de  NVIDIA Corporation\n\t2484  GA104 [GeForce RTX 3070]\n"
		"8086  Intel Corporation\n\t0d26  Crystal Well Integrated Graphics\n");
	card(first.path(), "card0", "0x10de", "0x2484", "nvidia");
	card(first.path(), "card1", "0x8086", "0x0d26", "i915");
	QDir().mkpath(first.path() + "/sys/class/drm/card0-DP-1");
	check(gpuDescription(first.path()) ==
		"GA104 [GeForce RTX 3070] (nvidia), Crystal Well Integrated Graphics (i915)",
		"names each GPU from pci.ids with its driver in brackets");

	QTemporaryDir second;
	put(second.path(), "/sys/bus/pci/devices/0000:01:00.0/vendor", "0x10de\n");
	put(second.path(), "/sys/bus/pci/devices/0000:01:00.0/device", "0x2484\n");
	link(second.path(), "/sys/bus/pci/devices/0000:01:00.0", "/sys/class/drm/card0/device");
	put(second.path(), "/proc/driver/nvidia/gpus/0000:01:00.0/information",
		"Model:           NVIDIA GeForce RTX 3070\n");
	card(second.path(), "card0", "0x10de", "0x2484", "nvidia");
	check(gpuDescription(second.path()) == "NVIDIA GeForce RTX 3070 (nvidia)",
		"falls back to the NVIDIA model when pci.ids is absent");

	QTemporaryDir third;
	put(third.path(), "/proc/driver/nvidia/gpus/0000:01:00.0/information",
		"Model:           NVIDIA GeForce RTX 3070\n");
	check(gpuDescription(third.path()) == "NVIDIA GeForce RTX 3070 (nvidia)",
		"reports an NVIDIA GPU with no DRM card");

	QTemporaryDir fourth;
	card(fourth.path(), "card0", "0x8086", "0xa7a0", "i915");
	check(gpuDescription(fourth.path()) == "Intel GPU (i915)",
		"falls back to the driver's family when the hardware is unnamed");

	QTemporaryDir fifth;
	check(gpuDescription(fifth.path()).isEmpty(), "returns nothing when no GPU is reported");
	return fails ? 1 : 0;
}
