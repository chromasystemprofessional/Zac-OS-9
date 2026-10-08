#include "resources.h"
#include "autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSysInfo>
#include <QStandardPaths>
#include <algorithm>
#include <sys/utsname.h>
#include <unistd.h>

#include "appdb.h"
#include "extensioncatalog.h"

namespace {

const QStringList providers = {
	"sounds", "startup-items", "application-support", "device-drivers",
	"network", "system-logs", "software-components", "extensions",
};

QString fixtureRoot() {
#ifdef ZACOS9_RESOURCE_TESTING
	return qEnvironmentVariable("ZACOS9_RESOURCE_TEST_ROOT");
#else
	return {};
#endif
}

QString boundedText(const QString &path, qsizetype limit = 1024 * 1024) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return QString::fromUtf8(file.read(limit));
}

QStringList splitLines(const QString &text) {
	return text.split('\n', Qt::SkipEmptyParts);
}

QString userData(const QString &relative) {
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
		"/" + relative;
}

QStringList aliasRoots(const QString &provider) {
#ifdef ZACOS9_RESOURCE_TESTING
	const QString testRoot = fixtureRoot();
	if (!testRoot.isEmpty()) {
		if (provider == "sounds") {
			return { testRoot + "/sounds/user", testRoot + "/sounds/system" };
		}
		if (provider == "application-support") {
			return { testRoot + "/support/user", testRoot + "/support/system" };
		}
	}
#endif
	if (provider == "sounds") {
		return { userData("sounds"), "/usr/share/sounds" };
	}
	if (provider == "application-support") {
		return { userData("zacos9/appearance"), "/usr/share/zacos9" };
	}
	return {};
}

bool rootAllowed(const QString &provider, int index, const QString &root) {
#ifndef ZACOS9_RESOURCE_TESTING
	(void)provider;
#endif
	QString base;
#ifdef ZACOS9_RESOURCE_TESTING
	const QString testRoot = fixtureRoot();
	if (!testRoot.isEmpty()) {
		base = testRoot + (provider == "sounds" ? "/sounds" : "/support");
	} else
#endif
	if (index == 0) {
		base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
	} else {
		base = "/usr/share";
	}
	const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
	const QString canonicalBase = QFileInfo(base).canonicalFilePath();
	return !canonicalRoot.isEmpty() && !canonicalBase.isEmpty() &&
		canonicalRoot.startsWith(canonicalBase + '/');
}

bool inside(const QString &path, const QString &root) {
	const QString canonicalPath = QFileInfo(path).canonicalFilePath();
	const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
	return !canonicalPath.isEmpty() && !canonicalRoot.isEmpty() &&
		(canonicalPath == canonicalRoot || canonicalPath.startsWith(canonicalRoot + '/'));
}

bool hasAliasLoop(const QString &root, const QString &relative) {
	QString current = root;
	QStringList seen{ QFileInfo(root).canonicalFilePath() };
	for (const QString &part : relative.split('/', Qt::SkipEmptyParts)) {
		current += '/' + part;
		const QFileInfo info(current);
		const QString canonical = info.canonicalFilePath();
		if (canonical.isEmpty()) {
			return true;
		}
		if (info.isSymLink() && seen.contains(canonical)) {
			return true;
		}
		seen << canonical;
	}
	return false;
}

bool safeAliasPath(const QString &provider, const QString &key, QString *resolved) {
	const QStringList roots = aliasRoots(provider);
	const QString rootId = key.section(':', 0, 0);
	const QString relative = key.section(':', 1);
	int rootIndex = -1;
	if (rootId == "user") {
		rootIndex = 0;
	} else if (rootId == "system") {
		rootIndex = 1;
	}
	if (rootIndex < 0 || rootIndex >= roots.size()) {
		return false;
	}
	const QString root = roots.at(rootIndex);
	if (!rootAllowed(provider, rootIndex, root)) {
		return false;
	}
	const QString candidate = relative.isEmpty() ? root : root + '/' + relative;
	const QFileInfo info(candidate);
	if (!info.exists() || !inside(candidate, root) || hasAliasLoop(root, relative)) {
		return false;
	}
	if (resolved) {
		*resolved = info.canonicalFilePath();
	}
	return true;
}

QString safeDisplayName(const QString &name) {
	return name.isEmpty() || name == "." || name == ".." || name.contains('/') ?
		QStringLiteral("Unavailable item") : name;
}

QStringList startupEnabledNames() {
	AutostartContext context = autostartDefaultContext();
#ifdef ZACOS9_RESOURCE_TESTING
	if (!fixtureRoot().isEmpty()) {
		context.userDir = fixtureRoot() + "/startup/user";
		context.systemDirs = { fixtureRoot() + "/startup/system" };
	}
#endif
	QStringList names;
	for (const AutostartEntry &entry : autostartEntries(context)) {
		if (entry.enabled) {
			names << entry.name;
		}
	}
	names.removeDuplicates();
	return names;
}

QStringList loadedModuleLines() {
	return splitLines(boundedText("/proc/modules", 2 * 1024 * 1024));
}

QStringList loadedModuleNames() {
	QStringList names;
	for (const QString &line : loadedModuleLines()) {
		const QString name = line.section(' ', 0, 0).trimmed();
		if (!name.isEmpty()) {
			names << name;
		}
	}
	return names;
}

QStringList moduleStartupNames() {
	QStringList dirs = { "/etc/modules-load.d", "/usr/local/lib/modules-load.d",
		"/usr/lib/modules-load.d", "/lib/modules-load.d" };
	QStringList out;
	for (const QString &dir : dirs) {
		QDir d(dir);
		for (const QFileInfo &file : d.entryInfoList({ "*.conf" },
				QDir::Files | QDir::Readable, QDir::Name)) {
			for (QString line : splitLines(boundedText(file.absoluteFilePath(), 64 * 1024))) {
				line = line.section('#', 0, 0).trimmed();
				if (!line.isEmpty() && QRegularExpression("^[A-Za-z0-9_-]+$").match(line).hasMatch()) {
					out << line;
				}
			}
		}
	}
	out.removeDuplicates();
	return out;
}

QString moduleDescription(const QString &module) {
	return boundedText("/sys/module/" + module + "/description", 4096).trimmed();
}

QString distroName() {
	const QString osRelease = boundedText("/etc/os-release", 64 * 1024);
	for (const QString &line : splitLines(osRelease)) {
		if (line.startsWith("PRETTY_NAME=")) {
			QString name = line.mid(12).trimmed();
			if (name.size() >= 2 && name.startsWith('"') && name.endsWith('"')) {
				name = name.mid(1, name.size() - 2);
			}
			return name;
		}
	}
	return QSysInfo::prettyProductName();
}

QString installedZacOSVersion() {
	QString statusPath = "/var/lib/dpkg/status";
#ifdef ZACOS9_RESOURCE_TESTING
	if (!fixtureRoot().isEmpty()) {
		statusPath = fixtureRoot() + "/dpkg/status";
	}
#endif
	const QString status = boundedText(statusPath, 16 * 1024 * 1024);
	QString package;
	QString version;
	QString state;
	for (const QString &line : splitLines(status + '\n')) {
		if (line.startsWith("Package: ")) {
			if (package == "zacos9" && state == "install ok installed" && !version.isEmpty()) {
				return version;
			}
			package = line.mid(9).trimmed();
			version.clear();
			state.clear();
		} else if (package == "zacos9" && line.startsWith("Version: ")) {
			version = line.mid(9).trimmed();
		} else if (package == "zacos9" && line.startsWith("Status: ")) {
			state = line.mid(8).trimmed();
		}
	}
	if (package == "zacos9" && state == "install ok installed" && !version.isEmpty()) {
		return version;
	}
	return {};
}

QString cpuName() {
	for (const QString &line : splitLines(boundedText("/proc/cpuinfo", 1024 * 1024))) {
		if (line.startsWith("model name") || line.startsWith("Hardware")) {
			const QString model = line.section(':', 1).trimmed();
			if (!model.isEmpty()) {
				return model;
			}
		}
	}
	return QSysInfo::currentCpuArchitecture();
}

} // namespace

QString resourceTestFixtureRoot() {
	return fixtureRoot();
}

bool resourceProviderKnown(const QString &provider) {
	return providers.contains(provider);
}

std::vector<SystemResource> resourceChildren(const QString &provider, const QString &key) {
	std::vector<SystemResource> out;
	auto add = [&out](QString id, QString name, ResourceType type, QString kind,
			QString description, QStringList metadata = {}, QString source = {},
			bool directory = false) {
		out.push_back({ std::move(id), std::move(name), type, std::move(source),
			std::move(kind), std::move(description), std::move(metadata), directory });
	};

	if (!resourceProviderKnown(provider)) {
		return out;
	}
	if (provider == "sounds" || provider == "application-support") {
		if (key.isEmpty()) {
			const QStringList roots = aliasRoots(provider);
			for (int i = 0; i < roots.size(); i++) {
				const QString root = roots.at(i);
				if (!QFileInfo(root).isDir() || !rootAllowed(provider, i, root) ||
						!inside(root, root)) {
					continue;
				}
				const QString rootKey = QString(i == 0 ? "user:" : "system:");
				add(rootKey, i == 0 ? "User" : "Computer", ResourceType::Collection,
					"folder", QStringLiteral("A read-only view of allowlisted ") +
						(provider == "sounds" ? "sound resources." : "appearance support files."),
					{ "Source: " + QFileInfo(root).canonicalFilePath(),
						"Access: read-only; files are not copied." }, root, true);
			}
			if (out.empty()) {
				add("unavailable", provider == "sounds" ? "No sound folders found" :
					"No support folders found", ResourceType::Information, "information",
					"No allowlisted resource directory is currently available.");
			}
			return out;
		}

		const QString rootId = key.section(':', 0, 0);
		const QString relative = key.section(':', 1);
		QString root;
		const QStringList roots = aliasRoots(provider);
		if (rootId == "user" && !roots.isEmpty()) {
			root = roots.at(0);
		} else if (rootId == "system" && roots.size() > 1) {
			root = roots.at(1);
		}
		QString directory;
		if (root.isEmpty() || !safeAliasPath(provider, key, &directory) ||
				!QFileInfo(directory).isDir()) {
			return out;
		}
		QDir dir(directory);
		for (const QFileInfo &entry : dir.entryInfoList(QDir::AllEntries |
				QDir::NoDotAndDotDot | QDir::Readable | QDir::Hidden, QDir::Name)) {
			if (entry.fileName() == "." || entry.fileName() == "..") {
				continue;
			}
			const QString childKey = rootId + ':' +
				(relative.isEmpty() ? entry.fileName() : relative + '/' + entry.fileName());
			QString checked;
			if (!safeAliasPath(provider, childKey, &checked)) {
				continue;
			}
			const QFileInfo checkedInfo(checked);
			const bool isDir = checkedInfo.isDir();
			if (!isDir && !checkedInfo.isFile()) {
				continue;
			}
			add(childKey, safeDisplayName(entry.fileName()),
				ResourceType::FilesystemAlias, isDir ? "folder" : "file",
				"Read-only filesystem alias. The source remains in place; writes, moves, "
					"copies and aliases are disabled.",
				{ "Source: " + checkedInfo.canonicalFilePath(),
					"Access: read-only; ordinary system permissions still apply." },
				checkedInfo.canonicalFilePath(), isDir);
		}
		return out;
	}

	if (provider == "startup-items") {
		for (const QString &name : startupEnabledNames()) {
			add("startup-" + QString::number(out.size()), name, ResourceType::Information,
				"startup item", "An enabled desktop startup entry. Its command is not exposed "
					"or run by Finder.",
				{ "Status: enabled at login", "Access: informational only" });
		}
		if (out.empty()) {
			add("none", "No enabled startup items found", ResourceType::Information,
				"information", "No enabled entries were found in the supported autostart "
					"directories.");
		}
		return out;
	}

	if (provider == "extensions") {
		for (const ExtensionEntry &entry : curatedExtensions(loadedModuleNames(),
				moduleStartupNames())) {
			add(entry.key, entry.name, ResourceType::Information, "system extension",
				entry.description,
				{ "Status: on",
					"Starts: " + QString(entry.startup ? "at every startup (modules-load.d)" :
						"automatically when its hardware is found"),
					"Kernel modules: " + entry.modules.join(", ") });
			out.back().icon = entry.icon;
		}
		if (out.empty()) {
			add("none", "No extensions found", ResourceType::Information,
				"information", "The kernel did not report any familiar extensions.");
		}
		return out;
	}

	if (provider == "device-drivers") {
		const QStringList startup = moduleStartupNames();
		for (const QString &module : loadedModuleNames()) {
			QString displayName = module;
			displayName.replace('_', ' ');
			const QString description = moduleDescription(module);
			const bool configured = startup.contains(module);
			add(module, displayName, ResourceType::Information,
				"kernel module", description.isEmpty()
					? "Loaded kernel module; no public description is available." : description,
				{ "Status: loaded",
					"Startup: " + QString(configured ? "configured in modules-load.d" :
						"not configured in modules-load.d"),
					"Component: /sys/module/" + module,
					"Access: informational only; driver controls are not provided." });
			out.back().icon = extensionModuleIcon(module);
		}
		if (out.empty()) {
			add("none", "No loaded drivers found", ResourceType::Information,
				"information", "The kernel did not report any loadable modules.");
		}
		return out;
	}

	if (provider == "network") {
		for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
			if (iface.name().isEmpty()) {
				continue;
			}
			QStringList addresses;
			for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
				const QHostAddress address = entry.ip();
				if (!address.isNull() && !address.isLoopback()) {
					addresses << address.toString();
				}
			}
			addresses.removeDuplicates();
			QStringList metadata = {
				"Status: " + QString(iface.flags().testFlag(QNetworkInterface::IsUp) ?
					"up" : "down"),
				"Addresses: " + (addresses.isEmpty() ? QStringLiteral("none") :
					addresses.join(", ")),
				"Access: informational only; network settings are unchanged."
			};
			add("interface-" + iface.name(), iface.humanReadableName().isEmpty() ?
				iface.name() : iface.humanReadableName(), ResourceType::Information,
				"network interface", "Network interface discovered from Qt's interface "
					"provider.", metadata);
		}
		return out;
	}

	if (provider == "system-logs") {
		add("restricted", "Protected system logs", ResourceType::Information,
			"protected information", "Log contents are not exposed here because they can "
				"contain credentials, personal data and other sensitive information.",
			{ "Access: restricted; no log files or journal contents are opened." });
		return out;
	}

	if (provider == "software-components") {
		for (const AppEntry &app : appList()) {
			QStringList metadata = {
				"Component: " + app.id,
				"Access: informational only; no install, remove or service controls."
			};
			if (!app.origin.isEmpty()) {
				metadata << "Package ownership: " + app.origin;
			}
			if (!app.version.isEmpty()) {
				metadata << "Version: " + app.version;
			}
			add("application-" + app.id, app.folderName, ResourceType::Information,
				"installed application", app.comment.isEmpty()
					? "Application discovered from an installed desktop entry." : app.comment,
				metadata);
		}
		return out;
	}
	return out;
}

bool resourceOpenPath(const QString &provider, const QString &key, QString *path) {
	if (!path || (provider != "sounds" && provider != "application-support")) {
		return false;
	}
	QString candidate;
	if (!safeAliasPath(provider, key, &candidate)) {
		return false;
	}
	*path = candidate;
	return true;
}

ResourceDetails resourceDetails(const QString &provider, const QString &key) {
	ResourceDetails details;
	details.name = key.isEmpty() ? provider : safeDisplayName(key.section('/', -1));
	details.kind = "system resource";
	details.location = "System Folder";
	details.sources = "Provider: " + provider;
	details.description = "Provider-backed information; no file is copied.";
	details.access = "Read-only; informational resources cannot be modified.";
	if (provider == "sounds" || provider == "application-support") {
		QString source;
		if (resourceOpenPath(provider, key, &source)) {
			details.name = QFileInfo(source).fileName();
			details.kind = QFileInfo(source).isDir() ? "read-only folder alias" :
				"read-only file alias";
			details.location = provider == "sounds" ? "System Folder:Sounds" :
				"System Folder:Application Support";
			details.sources = QFileInfo(source).canonicalFilePath();
			details.description = "A real filesystem alias. The original is not copied.";
			details.access = "Read-only in Finder; source permissions remain in force.";
		} else if (key.isEmpty()) {
			details.name = provider == "sounds" ? "Sounds" : "Application Support";
			details.kind = "virtual collection";
			details.description = "A collection of allowlisted, lazily discovered sources.";
		}
		return details;
	}
	if (provider == "startup-items") {
		details.name = "Startup Items";
		details.kind = key.isEmpty() ? "virtual collection" : "informational startup item";
		details.description = "Runs enabled entries at login; command text and launch controls "
			"are not exposed here.";
		return details;
	}
	if (provider == "system-logs") {
		details.name = "System Logs";
		details.kind = "protected information";
		details.description = "Log contents are deliberately withheld to protect sensitive "
			"data.";
		details.access = "Restricted; no logs are opened or copied.";
		return details;
	}
	if (provider == "extensions") {
		const ExtensionEntry entry = extensionEntry(key);
		details.name = key.isEmpty() ? QStringLiteral("Extensions") :
			entry.key.isEmpty() ? safeDisplayName(key) : entry.name;
		details.kind = key.isEmpty() ? "virtual collection" : "system extension";
		details.description = entry.key.isEmpty() ?
			"Familiar system extensions; open Extensions Manager and choose Show All "
			"for every loaded kernel module." : entry.description;
		return details;
	}
	if (provider == "device-drivers") {
		details.name = key.isEmpty() ? QStringLiteral("Device Drivers") : safeDisplayName(key);
		details.kind = key.isEmpty() ? "virtual collection" : "loaded kernel module";
		details.description = "Current kernel module status; no load/unload controls.";
		return details;
	}
	if (provider == "network") {
		details.name = "Network";
		details.kind = key.isEmpty() ? "virtual collection" : "network interface";
		details.description = "Live interface metadata; no network settings are changed.";
		return details;
	}
	if (provider == "software-components") {
		details.name = key.isEmpty() ? "Software Components" : safeDisplayName(key);
		details.kind = key.isEmpty() ? "virtual collection" : "installed application";
		details.description = "Installed software catalog; package and service controls are "
			"not offered.";
	}
	return details;
}

QStringList systemInformation() {
	QStringList lines;
	QString version = installedZacOSVersion();
	if (version.isEmpty()) {
		version = boundedText("/etc/zacos9/version", 128).trimmed();
	}
	if (version.isEmpty()) {
		version = boundedText("/usr/share/zacos9/version", 128).trimmed();
	}
	lines << "ZacOS version: " + (version.isEmpty() ?
		QStringLiteral("not reported by the installed system") : version)
		<< "Distribution: " + distroName()
		<< "Kernel: " + QSysInfo::kernelType() + " " + QSysInfo::kernelVersion()
		<< "Processor: " + cpuName();
	for (const QString &line : splitLines(boundedText("/proc/meminfo", 1024 * 1024))) {
		if (line.startsWith("MemTotal:")) {
			lines << "Memory: " + line.mid(9).trimmed();
			break;
		}
	}
	for (const QString card : { "card0", "card1", "card2", "card3" }) {
		const QString base = "/sys/class/drm/" + card + "/device";
		const QString vendor = boundedText(base + "/vendor", 128).trimmed();
		const QString device = boundedText(base + "/device", 128).trimmed();
		const QString driver = QFileInfo(base + "/driver").symLinkTarget();
		if (!vendor.isEmpty() || !device.isEmpty()) {
			lines << "Graphics: " + (vendor + " " + device).trimmed() +
				(driver.isEmpty() ? QString() : " (" + QFileInfo(driver).fileName() + ")");
			break;
		}
	}
	QStringList disks = QDir("/sys/block").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
	disks.removeIf([](const QString &name) {
		return name.startsWith("loop") || name.startsWith("ram") || name.startsWith("zram");
	});
	lines << "Storage devices: " + (disks.isEmpty() ? QStringLiteral("not reported") :
		disks.join(", "));
	QStringList interfaces;
	for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
		if (iface.flags().testFlag(QNetworkInterface::IsUp) &&
				!iface.flags().testFlag(QNetworkInterface::IsLoopBack)) {
			interfaces << iface.humanReadableName();
		}
	}
	lines << "Network: " + (interfaces.isEmpty() ? QStringLiteral("no active interfaces") :
		interfaces.join(", "));
	const QStringList audio = splitLines(boundedText("/proc/asound/cards", 64 * 1024));
	QStringList audioNames;
	for (const QString &line : audio) {
		const QString trimmed = line.trimmed();
		if (!trimmed.isEmpty() && !trimmed.startsWith("0 [") &&
				!trimmed.startsWith("1 [") && !trimmed.startsWith("2 [")) {
			audioNames << trimmed;
		}
	}
	lines << "Audio: " + (audioNames.isEmpty() ? QStringLiteral("not reported") :
		audioNames.join(", "));
	QStringList drivers;
	for (const QString &line : loadedModuleLines()) {
		const QString name = line.section(' ', 0, 0);
		if (name.startsWith("snd") || name.startsWith("i915") ||
				name.startsWith("amdgpu") || name.startsWith("nouveau") ||
				name.startsWith("nvidia") || name.startsWith("iwlwifi") ||
				name.startsWith("ath")) {
			drivers << name;
		}
	}
	drivers.removeDuplicates();
	lines << "Loaded hardware drivers: " + (drivers.isEmpty() ?
		QStringLiteral("not reported") : drivers.join(", "));
	const QString uptime = splitLines(boundedText("/proc/uptime", 128)).value(0).section(' ', 0, 0);
	bool ok = false;
	const double seconds = uptime.toDouble(&ok);
	if (ok) {
		const qint64 totalMinutes = static_cast<qint64>(seconds) / 60;
		lines << QString("Uptime: %1 days, %2 hours, %3 minutes")
			.arg(totalMinutes / 1440).arg((totalMinutes / 60) % 24).arg(totalMinutes % 60);
	}
	return lines;
}

QStringList extensionInformation() {
	QStringList lines;
	for (const SystemResource &resource : resourceChildren("extensions")) {
		lines << resource.name + '\t' + resource.description + '\t' +
			resource.metadata.join("; ");
	}
	return lines;
}
