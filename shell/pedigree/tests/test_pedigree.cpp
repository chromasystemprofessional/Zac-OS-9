/* Pedigree (shell/pedigree) against a fixture tree in place of /: what each
 * section reads from sysfs, /proc, os-release, dpkg and the ids files, and
 * the window's outline (sections open, things closed, triangles, tabs,
 * the saved report). */
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "pedigreewindow.h"
#include "profile.h"

static int fails;
static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

static QString root;
static void put(const QString &path, const QByteArray &data) {
	QDir().mkpath(QFileInfo(root + path).path());
	QFile f(root + path);
	f.open(QIODevice::WriteOnly);
	f.write(data);
}
static void link(const QString &target, const QString &path) {
	QDir().mkpath(QFileInfo(root + path).path());
	QFile::link(target.startsWith('/') ? root + target : target, root + path);
}

static const ProfileItem *find(const QList<ProfileItem> &items, const QString &label) {
	for (const ProfileItem &i : items) {
		if (i.label == label) {
			return &i;
		}
	}
	return nullptr;
}
static QString value(const ProfileItem *section, const QString &label) {
	const ProfileItem *i = section ? find(section->children, label) : nullptr;
	return i ? i->value : QStringLiteral("<missing>");
}

int main(int argc, char **argv) {
	QTemporaryDir dir;
	root = dir.path();
	qputenv("ZACOS9_PEDIGREE_ROOT", root.toUtf8());

	put("/var/lib/dpkg/status", "Package: foo\nStatus: install ok installed\nVersion: 1\n\n"
		"Package: zacos9\nStatus: install ok installed\nVersion: 0.1.56\n\n"
		"Package: gone\nStatus: deinstall ok config-files\nVersion: 2\n");
	put("/etc/os-release", "NAME=\"Debian GNU/Linux\"\nPRETTY_NAME=\"Debian GNU/Linux 13 (trixie)\"\n");
	put("/etc/hostname", "zac-book\n");
	put("/proc/sys/kernel/osrelease", "6.12.111+deb13-amd64\n");
	put("/proc/uptime", "90061.5 1234.0\n");
	put("/sys/class/dmi/id/sys_vendor", "Apple Inc.\n");
	put("/sys/class/dmi/id/product_name", "MacBookPro11,3\n");
	put("/sys/class/dmi/id/chassis_type", "10\n");
	put("/sys/class/dmi/id/bios_vendor", "Apple Inc.\n");
	put("/sys/class/dmi/id/bios_version", "478.0.0.0.0\n");
	put("/sys/class/dmi/id/bios_date", "06/09/2020\n");
	put("/proc/cpuinfo", "processor\t: 0\nmodel name\t: Intel(R) Core(TM) i7-4870HQ CPU @ 2.50GHz\ncpu cores\t: 4\n\n"
		"processor\t: 1\nmodel name\t: Intel(R) Core(TM) i7-4870HQ CPU @ 2.50GHz\ncpu cores\t: 4\n");
	put("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "3700000\n");
	put("/proc/meminfo", "MemTotal:       16384000 kB\nSwapTotal:             0 kB\n");
	put("/sys/class/power_supply/BAT0/type", "Battery\n");
	put("/sys/class/power_supply/BAT0/capacity", "87\n");
	put("/sys/class/power_supply/BAT0/status", "Discharging\n");
	put("/sys/class/power_supply/BAT0/energy_full", "80000000\n");
	put("/sys/class/power_supply/BAT0/energy_full_design", "100000000\n");
	put("/sys/class/power_supply/ADP1/type", "Mains\n");

	// A graphics card on PCI with a built-in display and a named monitor.
	put("/usr/share/misc/pci.ids", "# comment\n8086  Intel Corporation\n\t0d26  Crystal Well Integrated Graphics\n"
		"\t\t1234 5678  a subsystem\n106b  Apple Inc.\nC 03  Display controller\n\t00  VGA compatible controller\n");
	put("/usr/share/misc/usb.ids", "05ac  Apple, Inc.\n\t0274  Internal Keyboard/Trackpad\n");
	put("/sys/bus/pci/devices/0000:00:02.0/vendor", "0x8086\n");
	put("/sys/bus/pci/devices/0000:00:02.0/device", "0x0d26\n");
	link("/sys/bus/pci/drivers/i915", "/sys/bus/pci/devices/0000:00:02.0/driver");
	QDir().mkpath(root + "/sys/bus/pci/drivers/i915");
	link("/sys/bus/pci/devices/0000:00:02.0", "/sys/class/drm/card0/device");
	QByteArray edid(128, '\0');
	edid.replace(72, 18, QByteArray("\0\0\0\xFC\0Studio View\n  ", 18));
	put("/sys/class/drm/card0-eDP-1/status", "connected\n");
	put("/sys/class/drm/card0-eDP-1/modes", "2880x1800\n1920x1200\n");
	put("/sys/class/drm/card0-HDMI-A-1/status", "connected\n");
	put("/sys/class/drm/card0-HDMI-A-1/edid", edid);
	put("/sys/class/drm/card0-HDMI-A-1/modes", "2560x1440\n");
	put("/sys/class/drm/card0-DP-1/status", "disconnected\n");

	put("/sys/class/net/lo/operstate", "unknown\n");
	put("/sys/class/net/wlan0/operstate", "up\n");
	put("/sys/class/net/wlan0/address", "aa:bb:cc:dd:ee:ff\n");
	put("/sys/class/net/wlan0/device/x", "");
	QDir().mkpath(root + "/sys/class/net/wlan0/wireless");

	// A disk with two partitions, one mounted through an encrypted volume.
	put("/sys/block/sda/size", "488397168\n");
	put("/sys/block/sda/device/model", "APPLE SSD SM0256F\n");
	put("/sys/block/sda/queue/rotational", "0\n");
	put("/sys/block/sda/removable", "0\n");
	put("/sys/block/sda/sda1/partition", "1\n");
	put("/sys/block/sda/sda1/size", "409600\n");
	put("/sys/block/sda/sda2/partition", "2\n");
	put("/sys/block/sda/sda2/size", "487987200\n");
	QDir().mkpath(root + "/sys/block/sda/sda2/holders/dm-0");
	put("/sys/block/dm-0/dm/name", "root_crypt\n");
	put("/sys/block/loop0/size", "100\n");
	put("/proc/mounts", "/dev/sda1 /boot/efi vfat rw 0 0\n/dev/mapper/root_crypt / ext4 rw 0 0\n"
		"proc /proc proc rw 0 0\n");
	put("/sys/bus/usb/devices/usb1/idVendor", "1d6b\n");
	put("/sys/bus/usb/devices/1-12/idVendor", "05ac\n");
	put("/sys/bus/usb/devices/1-12/idProduct", "0274\n");
	put("/sys/bus/usb/devices/1-12/speed", "12\n");
	put("/sys/bus/usb/devices/1-12:1.0/x", "");
	put("/proc/asound/cards", " 0 [PCH            ]: HDA-Intel - HDA Intel PCH\n"
		"                      HDA Intel PCH at 0xc1610000 irq 51\n");

	QApplication app(argc, argv);

	check(profileSize(1536) == "1.5 KB" && profileSize(16384000.0 * 1024) == "16 GB" &&
		profileSize(250059350016.0) == "233 GB" && profileSize(12) == "12 bytes" && profileSize(2048) == "2 KB",
		"sizes in binary units, one decimal below 10");
	check(edidName(edid) == "Studio View" && edidName(QByteArray(10, 'x')).isEmpty(), "a monitor's name from its EDID");

	const QList<ProfileItem> sys = systemProfile();
	const ProfileItem *overview = find(sys, "System overview");
	check(value(overview, "ZacOS version") == "0.1.56" && value(overview, "Installed packages") == "2",
		"ZacOS's version and the installed packages, from dpkg");
	check(value(overview, "Distribution") == "Debian GNU/Linux 13 (trixie)" &&
		value(overview, "Kernel") == "Linux 6.12.111+deb13-amd64" && value(overview, "Computer name") == "zac-book",
		"distribution, kernel and computer name");
	check(value(overview, "Time since startup") == "1 day, 1 hour, 1 minute", "time since startup");
	const ProfileItem *hw = find(sys, "Hardware overview");
	check(value(hw, "Machine") == "Apple Inc. MacBookPro11,3" && value(hw, "Kind") == "Laptop" &&
		value(hw, "Firmware") == "Apple Inc. 478.0.0.0.0 (06/09/2020)", "the machine, its kind and firmware");
	check(value(hw, "Processor") == "Intel(R) Core(TM) i7-4870HQ CPU @ 2.50GHz" &&
		value(hw, "Processors") == "4 cores, 2 threads" && value(hw, "Speed") == "3.70 GHz" &&
		value(hw, "Memory") == "16 GB" && value(hw, "Swap space") == "<missing>",
		"processor, speed and memory; no swap, no row");
	const ProfileItem *battery = hw ? find(hw->children, "Battery") : nullptr;
	check(battery && value(battery, "Charge") == "87%" &&
		value(battery, "Condition") == "80% of its original capacity", "the battery's charge and condition");
	const ProfileItem *displays = find(sys, "Displays");
	const ProfileItem *card = displays ? find(displays->children, "Intel Corporation Crystal Well Integrated Graphics")
		: nullptr;
	check(card && value(card, "Driver") == "i915", "the graphics card named from pci.ids, with its driver");
	check(card && find(card->children, "Built-in display") && find(card->children, "Studio View") &&
		value(find(card->children, "Studio View"), "Resolution") == "2560x1440" && card->children.size() == 3,
		"connected displays only: the built-in one and a named monitor");
	const ProfileItem *net = find(sys, "Network overview");
	check(net && net->children.size() == 1 && net->children[0].label == "wlan0" &&
		value(&net->children[0], "Kind") == "Wi-Fi" && value(&net->children[0], "Status") == "Connected" &&
		value(&net->children[0], "Hardware address") == "aa:bb:cc:dd:ee:ff", "network interfaces, not loopback");

	const QList<ProfileItem> dev = devicesAndVolumes();
	const ProfileItem *disks = find(dev, "Disks and volumes");
	const ProfileItem *sda = disks ? find(disks->children, "sda") : nullptr;
	check(disks && disks->children.size() == 1 && sda && sda->value == "APPLE SSD SM0256F" &&
		value(sda, "Capacity") == "233 GB" && value(sda, "Kind") == "Solid-state disk",
		"disks, loop and mapper devices left out");
	const ProfileItem *p1 = sda ? find(sda->children, "sda1") : nullptr, *p2 = sda ? find(sda->children, "sda2") : nullptr;
	check(p1 && p1->value == "200 MB, at /boot/efi" && p2 && p2->value == "233 GB, at /",
		"a partition's row: its size and where it's mounted");
	check(p1 && value(p1, "Mounted at") == "/boot/efi" && value(p1, "Format") == "vfat" && value(p1, "Size") == "200 MB",
		"a partition's volume");
	check(p2 && value(p2, "Mounted at") == "/" && value(p2, "Through") == "/dev/mapper/root_crypt",
		"a partition mounted through an encrypted volume");
	const ProfileItem *usb = find(dev, "USB");
	check(usb && usb->children.size() == 1 && usb->children[0].label == "Internal Keyboard/Trackpad" &&
		usb->children[0].value == "Apple, Inc." && value(&usb->children[0], "Speed") == "12 Mbit/s",
		"USB devices named from usb.ids, root hubs and interfaces left out");
	const ProfileItem *pci = find(dev, "PCI");
	check(pci && pci->children.size() == 1 && pci->children[0].label == "Crystal Well Integrated Graphics",
		"PCI devices; class lines in pci.ids aren't vendors");
	check(find(dev, "Bluetooth") && find(dev, "Bluetooth")->value == "No adapters", "no Bluetooth: said so");
	const ProfileItem *snd = find(dev, "Sound");
	check(snd && snd->children.size() == 1 && snd->children[0].label == "HDA Intel PCH", "sound cards");

	PedigreeWindow w;
	w.show();
	QStringList rows = w.visibleRows();
	check(rows.value(0) == "System overview" && rows.value(1) == "  ZacOS version: 0.1.56" &&
		rows.contains("Hardware overview") && rows.contains("  Battery") && !rows.contains("    Charge: 87%"),
		"sections start open, the things in them closed");
	const int batteryRow = rows.indexOf("  Battery");
	w.toggle(batteryRow);
	check(w.visibleRows().value(batteryRow + 1) == "    Charge: 87%", "its triangle opens a row");
	w.toggle(batteryRow);
	check(!w.visibleRows().contains("    Charge: 87%"), "and closes it");
	w.toggle(0);
	check(w.visibleRows().value(1) == "Hardware overview", "a section closes too");
	w.setTab(1);
	check(w.visibleRows().value(0) == "Disks and volumes" && w.visibleRows().value(1) == "  sda: APPLE SSD SM0256F",
		"Devices and Volumes");
	w.setTab(0);
	check(w.visibleRows().value(1) == "Hardware overview", "each tab keeps what's open");
	const QString report = dir.path() + "/report.txt";
	QFile r(report);
	check(w.saveReport(report) && r.open(QIODevice::ReadOnly), "Save Report writes a file");
	const QString text = QString::fromUtf8(r.readAll());
	check(text.contains("System Profile\n\nSystem overview\n    ZacOS version: 0.1.56\n") &&
		text.contains("\n        Charge: 87%\n") && text.contains("Devices and Volumes\n\nDisks and volumes\n"),
		"the report has everything, closed rows too, indented");
	return fails ? 1 : 0;
}
