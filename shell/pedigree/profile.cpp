#include "profile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QNetworkInterface>
#include <QStorageInfo>
#include <QSysInfo>
#include <cctype>

namespace {

QString root() {
	return qEnvironmentVariable("ZACOS9_PEDIGREE_ROOT");
}

/* The running system, not a fixture: ask it what files can't say. */
bool live() {
	return root().isEmpty();
}

QByteArray readBytes(const QString &path, qint64 limit = 64 * 1024) {
	QFile f(root() + path);
	return f.open(QIODevice::ReadOnly) ? f.read(limit) : QByteArray();
}

QString read(const QString &path, qint64 limit = 64 * 1024) {
	return QString::fromUtf8(readBytes(path, limit)).trimmed();
}

QStringList entries(const QString &dir) {
	return QDir(root() + dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System, QDir::Name);
}

bool exists(const QString &path) {
	return QFileInfo::exists(root() + path);
}

/* "KEY=value" files such as os-release, quotes taken off. */
QString keyValue(const QString &text, const QString &key) {
	for (const QString &line : text.split('\n')) {
		if (line.startsWith(key + "=")) {
			QString v = line.mid(key.size() + 1).trimmed();
			if (v.size() >= 2 && (v.startsWith('"') || v.startsWith('\'')) && v.endsWith(v[0])) {
				v = v.mid(1, v.size() - 2);
			}
			return v;
		}
	}
	return QString();
}

/* "key : value" lines such as /proc/cpuinfo's: every value of a key. */
QStringList colonValues(const QString &text, const QString &key) {
	QStringList out;
	for (const QString &line : text.split('\n')) {
		if (line.section(':', 0, 0).trimmed() == key) {
			out << line.section(':', 1).trimmed();
		}
	}
	return out;
}

ProfileItem item(const QString &label, const QString &value = QString(), const QList<ProfileItem> &children = {}) {
	return ProfileItem{ label, value, children };
}

void add(QList<ProfileItem> &list, const QString &label, const QString &value) {
	if (!value.isEmpty()) {
		list << item(label, value);
	}
}

QString idsFile(const QString &name) {
	for (const char *dir : { "/usr/share/misc/", "/usr/share/hwdata/", "/var/lib/usbutils/" }) {
		if (exists(dir + name)) {
			return root() + dir + name;
		}
	}
	return QString();
}

/* ---- System Profile -------------------------------------------------- */

QString zacosVersion() {
	// dpkg's status file: the zacos9 package's stanza, if installed.
	const QString status = QString::fromUtf8(readBytes("/var/lib/dpkg/status", 64 * 1024 * 1024));
	for (const QString &stanza : status.split("\n\n")) {
		if (stanza.startsWith("Package: zacos9\n") && stanza.contains("\nStatus: install ok installed")) {
			return colonValues(stanza, "Version").value(0);
		}
	}
	for (const char *path : { "/etc/zacos9/version", "/usr/share/zacos9/version" }) {
		const QString v = read(path);
		if (!v.isEmpty()) {
			return v;
		}
	}
	return QString();
}

int installedPackages() {
	const QString status = QString::fromUtf8(readBytes("/var/lib/dpkg/status", 64 * 1024 * 1024));
	return status.count("\nStatus: install ok installed") + (status.startsWith("Status: install ok installed"));
}

QString uptimeText() {
	bool ok = false;
	const qint64 seconds = static_cast<qint64>(read("/proc/uptime").section(' ', 0, 0).toDouble(&ok));
	if (!ok) {
		return QString();
	}
	const qint64 days = seconds / 86400, hours = seconds / 3600 % 24, minutes = seconds / 60 % 60;
	QStringList parts;
	if (days) {
		parts << QString("%1 day%2").arg(days).arg(days == 1 ? "" : "s");
	}
	if (hours || days) {
		parts << QString("%1 hour%2").arg(hours).arg(hours == 1 ? "" : "s");
	}
	parts << QString("%1 minute%2").arg(minutes).arg(minutes == 1 ? "" : "s");
	return parts.join(", ");
}

ProfileItem systemOverview() {
	QList<ProfileItem> list;
	const QString version = zacosVersion();
	list << item("ZacOS version", version.isEmpty() ? QStringLiteral("Not installed as a package") : version);
	QString release = read("/etc/os-release");
	if (release.isEmpty()) {
		release = read("/usr/lib/os-release");
	}
	add(list, "Distribution", keyValue(release, "PRETTY_NAME"));
	const QString kernel = read("/proc/sys/kernel/osrelease");
	add(list, "Kernel", kernel.isEmpty() ? QString() : "Linux " + kernel);
	add(list, "Computer name", live() ? QSysInfo::machineHostName() : read("/etc/hostname"));
	if (live()) {
		add(list, "User", qEnvironmentVariable("USER"));
	}
	add(list, "Time since startup", uptimeText());
	const int packages = installedPackages();
	if (packages > 0) {
		list << item("Installed packages", QString::number(packages));
	}
	return item("System overview", QString(), list);
}

QString chassisKind(int type) {
	// SMBIOS chassis types.
	switch (type) {
	case 8: case 9: case 10: case 14: case 31: case 32: return "Laptop";
	case 30: return "Tablet";
	case 3: case 4: case 5: case 6: case 7: case 13: case 15: case 16: case 35: case 36: return "Desktop";
	case 17: case 23: case 28: return "Server";
	default: return QString();
	}
}

QString cpuSpeed(const QString &khz) {
	bool ok = false;
	const double mhz = khz.toDouble(&ok) / 1000;
	if (!ok || mhz <= 0) {
		return QString();
	}
	return mhz >= 1000 ? QString::number(mhz / 1000, 'f', 2) + " GHz" : QString::number(qRound(mhz)) + " MHz";
}

QList<ProfileItem> batteries() {
	QList<ProfileItem> out;
	for (const QString &name : entries("/sys/class/power_supply")) {
		const QString base = "/sys/class/power_supply/" + name;
		if (read(base + "/type") != "Battery") {
			continue;
		}
		QList<ProfileItem> details;
		const QString capacity = read(base + "/capacity");
		add(details, "Charge", capacity.isEmpty() ? QString() : capacity + "%");
		add(details, "Status", read(base + "/status"));
		add(details, "Charge cycles", read(base + "/cycle_count"));
		// What it holds now against what it held new.
		for (const char *kind : { "energy", "charge" }) {
			const double full = read(base + "/" + kind + "_full").toDouble();
			const double design = read(base + "/" + kind + "_full_design").toDouble();
			if (full > 0 && design > 0) {
				details << item("Condition", QString("%1% of its original capacity").arg(qRound(100 * full / design)));
				break;
			}
		}
		const QString model = (read(base + "/manufacturer") + " " + read(base + "/model_name")).trimmed();
		out << item("Battery", model, details);
	}
	return out;
}

ProfileItem hardwareOverview() {
	QList<ProfileItem> list;
	const QString dmi = "/sys/class/dmi/id/";
	const QString vendor = read(dmi + "sys_vendor"), product = read(dmi + "product_name");
	add(list, "Machine", product.startsWith(vendor) ? product : (vendor + " " + product).trimmed());
	add(list, "Kind", chassisKind(read(dmi + "chassis_type").toInt()));
	const QString board = (read(dmi + "board_vendor") + " " + read(dmi + "board_name")).trimmed();
	add(list, "Main board", board);
	QString firmware = (read(dmi + "bios_vendor") + " " + read(dmi + "bios_version")).trimmed();
	const QString date = read(dmi + "bios_date");
	if (!date.isEmpty()) {
		firmware += " (" + date + ")";
	}
	add(list, "Firmware", firmware);
	add(list, "Firmware type", exists("/sys/firmware/efi") ? QStringLiteral("UEFI") : QString());
	const QString serial = read(dmi + "product_serial");
	add(list, "Serial number", serial);

	const QString cpuinfo = read("/proc/cpuinfo", 4 * 1024 * 1024);
	QString cpu = colonValues(cpuinfo, "model name").value(0);
	if (cpu.isEmpty()) {
		cpu = colonValues(cpuinfo, "cpu").value(0); /* PowerPC */
	}
	add(list, "Processor", cpu.simplified());
	const int threads = colonValues(cpuinfo, "processor").size();
	const int cores = colonValues(cpuinfo, "cpu cores").value(0).toInt();
	if (threads > 0) {
		list << item("Processors", cores > 0 && cores != threads
			? QString("%1 cores, %2 threads").arg(cores).arg(threads)
			: QString("%1").arg(threads));
	}
	add(list, "Speed", cpuSpeed(read("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq")));

	const QString meminfo = read("/proc/meminfo");
	const double memory = colonValues(meminfo, "MemTotal").value(0).section(' ', 0, 0).toDouble() * 1024;
	add(list, "Memory", memory > 0 ? profileSize(memory) : QString());
	const double swap = colonValues(meminfo, "SwapTotal").value(0).section(' ', 0, 0).toDouble() * 1024;
	add(list, "Swap space", swap > 0 ? profileSize(swap) : QString());
	list << batteries();
	return item("Hardware overview", QString(), list);
}

QString pciName(const QString &device) {
	const QString base = "/sys/bus/pci/devices/" + device;
	const QString vendor = read(base + "/vendor").mid(2), id = read(base + "/device").mid(2);
	const QString ids = idsFile("pci.ids");
	const QString name = idsDevice(ids, vendor, id), maker = idsVendor(ids, vendor);
	if (!name.isEmpty()) {
		return maker.isEmpty() ? name : maker + " " + name;
	}
	return QString("PCI device %1:%2").arg(vendor, id);
}

ProfileItem displays() {
	QList<ProfileItem> list;
	for (const QString &card : entries("/sys/class/drm")) {
		if (!card.startsWith("card") || card.contains('-')) {
			continue;
		}
		// The graphics card, then the monitors plugged into it.
		const QString device = QFileInfo(root() + "/sys/class/drm/" + card + "/device").canonicalFilePath();
		const QString driver = QFileInfo(root() + "/sys/class/drm/" + card + "/device/driver").canonicalFilePath();
		QList<ProfileItem> monitors;
		if (!driver.isEmpty()) {
			monitors << item("Driver", QFileInfo(driver).fileName());
		}
		for (const QString &connector : entries("/sys/class/drm")) {
			if (!connector.startsWith(card + "-")) {
				continue;
			}
			const QString base = "/sys/class/drm/" + connector;
			if (read(base + "/status") != "connected") {
				continue;
			}
			const QString port = connector.mid(card.size() + 1);
			QString name = edidName(readBytes(base + "/edid", 4096));
			if (name.isEmpty()) {
				name = port.startsWith("eDP") || port.startsWith("LVDS") ? "Built-in display" : "Display";
			}
			QList<ProfileItem> details;
			details << item("Connected to", port);
			add(details, "Resolution", read(base + "/modes").section('\n', 0, 0));
			monitors << item(name, QString(), details);
		}
		const QString pci = QFileInfo(device).fileName();
		list << item(exists("/sys/bus/pci/devices/" + pci) ? pciName(pci) : card, QString(), monitors);
	}
	return item("Displays", list.isEmpty() ? QStringLiteral("None reported") : QString(), list);
}

ProfileItem networkOverview() {
	QList<ProfileItem> list;
	for (const QString &name : entries("/sys/class/net")) {
		if (name == "lo") {
			continue;
		}
		const QString base = "/sys/class/net/" + name;
		const bool physical = exists(base + "/device");
		const QString kind = exists(base + "/wireless") ? "Wi-Fi" : physical ? "Ethernet" : "Virtual";
		const QString state = read(base + "/operstate");
		QList<ProfileItem> details;
		details << item("Kind", kind);
		add(details, "Status", state == "up" ? QStringLiteral("Connected")
			: state == "down" || state == "dormant" ? QStringLiteral("Not connected") : QString());
		add(details, "Hardware address", read(base + "/address"));
		const int speed = read(base + "/speed").toInt();
		if (speed > 0 && kind == "Ethernet") {
			details << item("Speed", speed >= 1000 ? QString("%1 Gbit/s").arg(speed / 1000.0)
				: QString("%1 Mbit/s").arg(speed));
		}
		if (live()) {
			for (const QNetworkAddressEntry &e : QNetworkInterface::interfaceFromName(name).addressEntries()) {
				const QHostAddress a = e.ip();
				if (a.protocol() == QAbstractSocket::IPv4Protocol) {
					details << item("IPv4 address", a.toString());
				} else if (a.protocol() == QAbstractSocket::IPv6Protocol && !a.isLinkLocal()) {
					details << item("IPv6 address", a.toString());
				}
			}
		}
		list << item(name, kind, details);
	}
	return item("Network overview", list.isEmpty() ? QStringLiteral("No network interfaces") : QString(), list);
}

/* ---- Devices and Volumes --------------------------------------------- */

/* /proc/mounts: device -> (mount point, file system). */
QHash<QString, QPair<QString, QString>> mounts() {
	QHash<QString, QPair<QString, QString>> out;
	for (const QString &line : read("/proc/mounts", 1024 * 1024).split('\n')) {
		const QStringList f = line.split(' ');
		if (f.size() >= 3 && f[0].startsWith("/dev/") && !out.contains(f[0])) {
			QString point = f[1];
			point.replace("\\040", " ");
			out.insert(f[0], { point, f[2] });
		}
	}
	return out;
}

/* A partition's volume: mounted as itself, or through what holds it
 * (an encrypted or LVM volume). */
void volumeDetails(const QString &sysPath, const QString &dev, const QHash<QString, QPair<QString, QString>> &m,
		QList<ProfileItem> &details) {
	QStringList names{ "/dev/" + dev };
	for (const QString &holder : entries(sysPath + "/holders")) {
		names << "/dev/" + holder;
		const QString dm = read("/sys/block/" + holder + "/dm/name");
		if (!dm.isEmpty()) {
			names << "/dev/mapper/" + dm;
		}
	}
	for (const QString &name : names) {
		if (!m.contains(name)) {
			continue;
		}
		const auto [point, fs] = m.value(name);
		details << item("Mounted at", point) << item("Format", fs);
		if (live()) {
			const QStorageInfo info(point);
			if (info.isValid() && info.bytesTotal() > 0) {
				details << item("Available", profileSize(info.bytesAvailable()));
			}
		}
		if (name != "/dev/" + dev) {
			details << item("Through", name);
		}
		return;
	}
	details << item("Mounted", "No");
}

ProfileItem disks() {
	QList<ProfileItem> list;
	const auto m = mounts();
	for (const QString &dev : entries("/sys/block")) {
		if (dev.startsWith("loop") || dev.startsWith("ram") || dev.startsWith("zram") || dev.startsWith("dm-")) {
			continue;
		}
		const QString base = "/sys/block/" + dev;
		const double bytes = read(base + "/size").toDouble() * 512;
		const QString model = (read(base + "/device/vendor") + " " + read(base + "/device/model")).simplified();
		QList<ProfileItem> details;
		add(details, "Model", model);
		details << item("Capacity", bytes > 0 ? profileSize(bytes) : QStringLiteral("No media"));
		QString kind = dev.startsWith("sr") ? "CD or DVD drive"
			: read(base + "/queue/rotational") == "1" ? "Hard disk" : "Solid-state disk";
		if (read(base + "/removable") == "1" && !dev.startsWith("sr")) {
			kind += ", removable";
		}
		details << item("Kind", kind);
		bool partitioned = false;
		for (const QString &part : entries(base)) {
			if (!exists(base + "/" + part + "/partition")) {
				continue;
			}
			partitioned = true;
			QList<ProfileItem> volume;
			volume << item("Size", profileSize(read(base + "/" + part + "/size").toDouble() * 512));
			volumeDetails(base + "/" + part, part, m, volume);
			// Its size and where it is, on its own row too.
			QString summary = volume[0].value;
			for (const ProfileItem &v : volume) {
				if (v.label == "Mounted at") {
					summary += ", at " + v.value;
				}
			}
			details << item(part, summary, volume);
		}
		if (!partitioned && bytes > 0) {
			volumeDetails(base, dev, m, details);
		}
		list << item(dev, model.isEmpty() ? profileSize(bytes) : model, details);
	}
	return item("Disks and volumes", list.isEmpty() ? QStringLiteral("None reported") : QString(), list);
}

ProfileItem usb() {
	QList<ProfileItem> list;
	const QString ids = idsFile("usb.ids");
	for (const QString &dev : entries("/sys/bus/usb/devices")) {
		// Interfaces ("1-8:1.0") and the root hubs ("usb1") aren't devices.
		if (dev.contains(':') || dev.startsWith("usb")) {
			continue;
		}
		const QString base = "/sys/bus/usb/devices/" + dev;
		const QString vendor = read(base + "/idVendor"), product = read(base + "/idProduct");
		QString maker = read(base + "/manufacturer"), name = read(base + "/product");
		if (maker.isEmpty()) {
			maker = idsVendor(ids, vendor);
		}
		if (name.isEmpty()) {
			name = idsDevice(ids, vendor, product);
		}
		if (name.isEmpty()) {
			name = QString("USB device %1:%2").arg(vendor, product);
		}
		QList<ProfileItem> details;
		add(details, "Manufacturer", maker);
		details << item("Vendor and product ID", vendor + ":" + product);
		const QString speed = read(base + "/speed");
		add(details, "Speed", speed.isEmpty() ? QString() : speed + " Mbit/s");
		const QString power = read(base + "/bMaxPower");
		add(details, "Power", power);
		list << item(name, maker, details);
	}
	return item("USB", list.isEmpty() ? QStringLiteral("No devices") : QString(), list);
}

ProfileItem pci() {
	QList<ProfileItem> list;
	const QString ids = idsFile("pci.ids");
	for (const QString &dev : entries("/sys/bus/pci/devices")) {
		const QString base = "/sys/bus/pci/devices/" + dev;
		const QString vendor = read(base + "/vendor").mid(2), id = read(base + "/device").mid(2);
		QString name = idsDevice(ids, vendor, id);
		if (name.isEmpty()) {
			name = QString("PCI device %1:%2").arg(vendor, id);
		}
		QList<ProfileItem> details;
		add(details, "Manufacturer", idsVendor(ids, vendor));
		details << item("Slot", dev) << item("Vendor and device ID", vendor + ":" + id);
		const QString driver = QFileInfo(root() + base + "/driver").canonicalFilePath();
		add(details, "Driver", driver.isEmpty() ? QString() : QFileInfo(driver).fileName());
		list << item(name, idsVendor(ids, vendor), details);
	}
	return item("PCI", list.isEmpty() ? QStringLiteral("No devices") : QString(), list);
}

ProfileItem bluetooth() {
	QList<ProfileItem> list;
	for (const QString &hci : entries("/sys/class/bluetooth")) {
		if (hci.contains(':')) {
			continue; /* connections */
		}
		// The adapter's USB device is the interface's parent.
		const QString iface = QFileInfo(root() + "/sys/class/bluetooth/" + hci + "/device").canonicalFilePath();
		QString name;
		if (!iface.isEmpty()) {
			QFile f(QFileInfo(iface).path() + "/product");
			if (f.open(QIODevice::ReadOnly)) {
				name = QString::fromUtf8(f.readAll()).trimmed();
			}
		}
		list << item(hci, name.isEmpty() ? QStringLiteral("Bluetooth adapter") : name);
	}
	return item("Bluetooth", list.isEmpty() ? QStringLiteral("No adapters") : QString(), list);
}

ProfileItem sound() {
	QList<ProfileItem> list;
	// /proc/asound/cards: " 0 [PCH            ]: HDA-Intel - HDA Intel PCH", then a detail line.
	for (const QString &line : read("/proc/asound/cards").split('\n')) {
		const QString t = line.trimmed();
		if (t.isEmpty() || !t.at(0).isDigit() || !t.contains("]: ")) {
			continue;
		}
		const QString rest = t.section("]: ", 1);
		list << item(rest.section(" - ", 1).trimmed(), rest.section(" - ", 0, 0).trimmed());
	}
	return item("Sound", list.isEmpty() ? QStringLiteral("No sound cards") : QString(), list);
}

void reportItems(QString &out, const QList<ProfileItem> &items, int depth) {
	for (const ProfileItem &i : items) {
		out += QString(depth * 4, ' ') + i.label;
		if (!i.value.isEmpty()) {
			out += ": " + i.value;
		}
		out += '\n';
		reportItems(out, i.children, depth + 1);
	}
}

bool isHex4(const QString &s) {
	for (int i = 0; i < 4; ++i) {
		if (i >= s.size() || !isxdigit(s[i].toLatin1())) {
			return false;
		}
	}
	return true;
}

/* Vendors and devices of one ids file, read once. */
struct Ids {
	QHash<QString, QString> vendors, devices; /* "vvvv", "vvvv:dddd" */
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
				continue; /* subsystems, interfaces */
			}
			if (line.startsWith('\t')) {
				if (!vendor.isEmpty()) {
					loaded.devices.insert(vendor + ":" + line.mid(1, 4).toLower(), line.mid(7).trimmed());
				}
				continue;
			}
			// Vendors are 4 hex digits; device classes and the like follow ("C 00  …").
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

} // namespace

QString profileSize(double bytes) {
	static const char *const units[] = { "bytes", "KB", "MB", "GB", "TB", "PB" };
	int u = 0;
	while (bytes >= 1024 && u < 5) {
		bytes /= 1024;
		u++;
	}
	if (u == 0) {
		return QString("%1 bytes").arg(qRound64(bytes));
	}
	// One decimal below 10, whole numbers above, as "1.5 GB" and "251 GB".
	const QString n = bytes < 10 ? QString::number(bytes, 'f', 1) : QString::number(qRound64(bytes));
	return (n.endsWith(".0") ? n.chopped(2) : n) + " " + units[u];
}

QString edidName(const QByteArray &edid) {
	if (edid.size() < 128) {
		return QString();
	}
	for (int d = 54; d + 18 <= 126; d += 18) {
		if (edid[d] == 0 && edid[d + 1] == 0 && edid[d + 2] == 0 && static_cast<uchar>(edid[d + 3]) == 0xFC) {
			const QByteArray name = edid.mid(d + 5, 13);
			return QString::fromLatin1(name.left(name.indexOf('\n') < 0 ? 13 : name.indexOf('\n'))).trimmed();
		}
	}
	return QString();
}

QString idsVendor(const QString &idsFile, const QString &vendor) {
	return ids(idsFile).vendors.value(vendor.toLower());
}

QString idsDevice(const QString &idsFile, const QString &vendor, const QString &device) {
	return ids(idsFile).devices.value(vendor.toLower() + ":" + device.toLower());
}

QList<ProfileItem> systemProfile() {
	return { systemOverview(), hardwareOverview(), displays(), networkOverview() };
}

QList<ProfileItem> devicesAndVolumes() {
	return { disks(), usb(), pci(), bluetooth(), sound() };
}

QString profileText(const QList<ProfileItem> &items) {
	QString out;
	reportItems(out, items, 0);
	return out;
}

QString profileReport(const QList<ProfileItem> &system, const QList<ProfileItem> &devices) {
	return "Pedigree report\n\nSystem Profile\n\n" + profileText(system) + "\nDevices and Volumes\n\n" +
		profileText(devices);
}
