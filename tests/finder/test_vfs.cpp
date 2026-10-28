/*
 * The Macintosh view of this computer: the startup disk's curated
 * hierarchy, the Applications folder built from XDG desktop entries, and
 * what the Finder will and won't let you do to either.
 *
 * The test builds a private XDG environment in a temporary directory, so
 * it sees only the desktop entries it writes itself and touches nothing
 * the real desktop owns.
 */
#include <cstdlib>
#include <unistd.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QSaveFile>
#include <functional>

#include <glib.h>

#include "appdb.h"
#include "apptrash.h"
#include "controlpanels.h"
#include "items.h"
#include "resources.h"
#include "folderpath.h"
#include "vfs.h"

static int fails;

static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

static QString appsDir, homeAppsDir, root;

static void writeEntry(const QString &dir, const QString &file, const QString &body) {
	QDir().mkpath(dir);
	QFile f(dir + "/" + file);
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		f.write(body.toUtf8());
	}
}

/* An ordinary application entry, as most packages write one. */
static QString entry(const QString &name, const QString &exec = "/bin/true",
		const QString &extra = QString()) {
	return "[Desktop Entry]\nType=Application\nName=" + name +
		"\nExec=" + exec + "\n" + extra;
}

/* Give GIO's file monitors time to be served: it drops its cached list
 * of desktop entries only once they have been, and inotify takes a
 * moment to deliver. appRefresh() serves whatever is waiting. */
static void settle() {
	QElapsedTimer timer;
	timer.start();
	while (timer.elapsed() < 600) {
		g_main_context_iteration(nullptr, FALSE);
		QCoreApplication::processEvents();
		usleep(2000);
	}
	appRefresh();
	vfsRefresh();
}

/* The names in a virtual folder. */
static QStringList namesIn(const QString &path) {
	QStringList out;
	for (const auto &item : vfsList(path)) {
		out << item->name;
	}
	return out;
}

static QString appFolder(const QString &desktopId) {
	return vfsPathFor("applications/" + desktopId);
}

static QString pathNamed(const QString &path, const QString &name) {
	for (const auto &item : vfsList(path)) {
		if (item->name == name) {
			return item->path;
		}
	}
	return {};
}

static bool waitFor(const std::function<bool()> &condition) {
	QElapsedTimer timer;
	timer.start();
	while (!condition() && timer.elapsed() < 3000) {
		g_main_context_iteration(nullptr, FALSE);
		QCoreApplication::processEvents();
		usleep(2000);
	}
	return condition();
}

int main(int argc, char **argv) {
	QTemporaryDir tmp;
	if (!tmp.isValid()) {
		return 1;
	}
	root = tmp.path();
	/* A private XDG environment: our entries only, and nothing of the
	 * real desktop's is read or written. Set before Qt or GIO look. */
	const QString dataHome = root + "/data";
	const QString dataDirs = root + "/system";
	const QString flatpakData = dataHome + "/flatpak/exports/share";
	appsDir = dataDirs + "/applications";
	homeAppsDir = dataHome + "/applications";
	setenv("XDG_DATA_HOME", dataHome.toUtf8().constData(), 1);
	setenv("XDG_DATA_DIRS", (flatpakData + ":" + dataDirs).toUtf8().constData(), 1);
	setenv("XDG_CONFIG_HOME", (root + "/config").toUtf8().constData(), 1);
	setenv("HOME", root.toUtf8().constData(), 1);
	setenv("XDG_CURRENT_DESKTOP", "ZacOS9", 1);
	const QString resourceRoot = root + "/resource-fixtures";
	setenv("ZACOS9_RESOURCE_TEST_ROOT", resourceRoot.toUtf8().constData(), 1);
	QDir().mkpath(appsDir);
	/* The control panels, as installed: Control Panels lists them. */
	for (const QFileInfo &entry : QDir(ZACOS9_DESKTOP_ENTRIES).entryInfoList({ "*.desktop" })) {
		QFile file(entry.filePath());
		if (file.open(QIODevice::ReadOnly) && file.readAll().contains("X-ZacOS9-ControlPanel")) {
			QFile::copy(entry.filePath(), appsDir + "/" + entry.fileName());
		}
	}
	QDir().mkpath(homeAppsDir);
	QDir().mkpath(resourceRoot + "/sounds/user/pack");
	QDir().mkpath(resourceRoot + "/sounds/system");
	QDir().mkpath(resourceRoot + "/support/user/appearance");
	QDir().mkpath(resourceRoot + "/support/system");
	QDir().mkpath(resourceRoot + "/startup/user");
	QDir().mkpath(resourceRoot + "/startup/system");
	QDir().mkpath(resourceRoot + "/dpkg");
	QDir().rmdir(resourceRoot + "/support/system");
	QFile::link("/etc", resourceRoot + "/support/system");
	writeEntry(resourceRoot + "/sounds/user/pack", "chime.wav", "sound data");
	writeEntry(resourceRoot + "/support/user/appearance", "theme.txt", "theme data");
	writeEntry(resourceRoot + "/startup/user", "enabled.desktop",
		"[Desktop Entry]\nType=Application\nName=Fixture Startup\nExec=sh -c secret-command\n");
	writeEntry(resourceRoot + "/startup/user", "disabled.desktop",
		"[Desktop Entry]\nType=Application\nName=Disabled Fixture\nExec=sh -c secret-command\nHidden=true\n");
	writeEntry(resourceRoot + "/dpkg", "status",
		"Package: other\nVersion: 1.0\n\nPackage: zacos9\nVersion: 0.1.27-fixture\n"
		"Status: install ok installed\n\n");
	QFile::link(resourceRoot + "/sounds/user/pack/chime.wav",
		resourceRoot + "/sounds/user/alias.wav");
	QFile::link("/etc/passwd", resourceRoot + "/sounds/user/escape");
	QFile::link(resourceRoot + "/sounds/user/missing",
		resourceRoot + "/sounds/user/broken");
	QFile::link(resourceRoot + "/sounds/user", resourceRoot + "/sounds/user/loop");
	QFile::link("/etc", resourceRoot + "/support/user/appearance/escape");
	const QString aptState = root + "/apt/extended_states";
	setenv("ZACOS9_APT_STATE", aptState.toUtf8().constData(), 1);
	writeEntry(root + "/apt", "extended_states",
		"Package: finder-auto-demo\nArchitecture: amd64\nAuto-Installed: 1\n\n");
	writeEntry(root + "/bin", "dpkg",
		"#!/bin/sh\n"
		"case \"$2\" in\n"
		"*/automatic.desktop) printf 'finder-auto-demo:amd64: %s\\n' \"$2\" ;;\n"
		"*/manual.desktop|*/qps.desktop) printf 'finder-manual-demo: %s\\n' \"$2\" ;;\n"
		"*) exec /usr/bin/dpkg \"$@\" ;;\n"
		"esac\n");
	writeEntry(root + "/bin", "dpkg-query",
		"#!/bin/sh\n"
		"case \"$3\" in\n"
		"finder-auto-demo:amd64|finder-manual-demo) printf '1.0\\n' ;;\n"
		"*) exec /usr/bin/dpkg-query \"$@\" ;;\n"
		"esac\n");
	for (const QString name : { "dpkg", "dpkg-query" }) {
		QFile::setPermissions(root + "/bin/" + name,
			QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
	}
	setenv("PATH", (root + "/bin:" + QString::fromLocal8Bit(qgetenv("PATH"))).toUtf8().constData(), 1);

	/* A QGuiApplication (not just QCoreApplication), offscreen: so icon
	 * resolution (QIcon::fromTheme, which needs a QPA platform to
	 * rasterize into) runs for real instead of skipping itself the way
	 * it does for a tool with no GUI platform at all. */
	QGuiApplication app(argc, argv);
	QStandardPaths::setTestModeEnabled(false);

	/* ---- pasted custom icons ------------------------------------------ */
	const QString iconFixture = root + "/custom-icon-fixture.dat";
	writeEntry(root, "custom-icon-fixture.dat", "fixture");
	QImage pasted(48, 24, QImage::Format_ARGB32);
	pasted.fill(0xffff0000);
	check(writeCustomIconImage(iconFixture, pasted),
		"a pasted picture is stored as a bounded custom icon");
	const QImage storedIcon = customIconImage(iconFixture);
	check(storedIcon.size() == QSize(32, 32) &&
		storedIcon.pixel(16, 16) == 0xffff0000 && qAlpha(storedIcon.pixel(0, 0)) == 0,
		"custom icons persist as centered 32x32 transparent PNGs");
	const auto iconItem = makeItem(QFileInfo(iconFixture));
	check(iconItem && iconItem->customIcon32.size() == 32 * 32 &&
		iconItem->customIcon32[16 * 32 + 16] == 0xffff0000,
		"Finder items use the persisted custom icon");
	check(!writeCustomIconImage(iconFixture, QImage()),
		"an empty Clipboard image is rejected without replacing the icon");
	check(!writeCustomIconImage(iconFixture, QImage(4097, 1, QImage::Format_ARGB32)),
		"oversized pictures are rejected before encoding");
	check(clearCustomIconImage(iconFixture) && customIconImage(iconFixture).isNull(),
		"clearing a custom icon removes its persisted metadata");

	/* ---- discovery ---------------------------------------------------- */
	writeEntry(appsDir, "simpletext.desktop", entry("SimpleText"));
	writeEntry(appsDir, "calculator.desktop",
		entry("Calculator", "/bin/true", "Categories=Utility;\n"));
	/* An entry written for this desktop only shows too. */
	writeEntry(appsDir, "ours-only.desktop", entry("Only Ours", "/bin/true", "OnlyShowIn=ZacOS9;\n"));
	/* What Applications leaves out: the system's own applications (listed
	 * when the ISO is built), ZacOS 9's own pieces, Wine's uninstallers. */
	writeEntry(dataDirs + "/zacos9", "base-applications",
		"builtin.desktop\nfoot.desktop\nflasher.desktop\nxscreensaver-settings.desktop\n");
	/* A utility ZacOS 9 ships (System Folder > Utilities), though the
	 * system came with it. */
	writeEntry(appsDir, "flasher.desktop",
		entry("Disk Flasher", "/bin/true", "Categories=Utility;X-ZacOS9-Utility;\n"));
	writeEntry(appsDir, "builtin.desktop", entry("Came With The System"));
	writeEntry(appsDir, "automatic.desktop", entry("Automatic Dependency"));
	writeEntry(appsDir, "manual.desktop", entry("Explicitly Installed"));
	writeEntry(appsDir, "system-twin.desktop",
		entry("SimpleText", "/bin/true", "Categories=System;\n"));
	for (const auto &utility : std::vector<std::pair<QString, QString>>{
			{ "qps", "System" }, { "pcmanfm-qt", "FileManager;Utility;Core;Qt" },
			{ "pcmanfm-qt-desktop-pref", "Settings;Qt;DesktopSettings" },
			{ "xscreensaver-settings", "Settings;DesktopSettings;Security" },
			{ "system-config-printer", "Settings;System;Printing" } }) {
		writeEntry(appsDir, utility.first + ".desktop",
			entry(utility.first, "/bin/true", "Categories=" + utility.second + ";\n"));
	}
	writeEntry(appsDir, "zacos9-somepanel.desktop", entry("A ZacOS Panel"));
	/* One of ZacOS 9's own that it files in Utilities (Disk Setup). */
	writeEntry(appsDir, "zacos9-diskthing.desktop", entry("A ZacOS Disk Tool", "/bin/true",
		"Categories=System;Utility;X-ZacOS9-Utility;\nOnlyShowIn=ZacOS9;\n"));
	writeEntry(homeAppsDir + "/wine/Programs/Notepad Plus", "Notepad Plus.desktop",
		entry("Notepad Plus", "env WINEPREFIX=/nonexistent wine C:\\\\np.exe"));
	writeEntry(homeAppsDir + "/wine/Programs/Notepad Plus", "Uninstall Notepad Plus.desktop",
		entry("Uninstall Notepad Plus", "env WINEPREFIX=/nonexistent wine C:\\\\unins.exe"));
	/* Every way an entry asks not to be shown. */
	writeEntry(appsDir, "hidden.desktop",
		"[Desktop Entry]\nType=Application\nName=Hidden One\n"
		"Exec=/bin/true\nHidden=true\n");
	writeEntry(appsDir, "nodisplay.desktop",
		"[Desktop Entry]\nType=Application\nName=NoDisplay One\n"
		"Exec=/bin/true\nNoDisplay=true\n");
	writeEntry(appsDir, "othershell.desktop",
		"[Desktop Entry]\nType=Application\nName=Only Elsewhere\n"
		"Exec=/bin/true\nOnlyShowIn=GNOME;\n");
	writeEntry(appsDir, "notus.desktop",
		"[Desktop Entry]\nType=Application\nName=Not Here\n"
		"Exec=/bin/true\nNotShowIn=ZacOS9;\n");
	writeEntry(appsDir, "missingbin.desktop",
		entry("Missing Binary", "/bin/true", "TryExec=/nonexistent/program\n"));
	settle();

	QStringList apps = namesIn(vfsPathFor("applications"));
	const QString systemFolder = vfsPathFor("system-folder");
	const QStringList systemNames = namesIn(systemFolder);
	for (const QString name : { "Sounds", "Startup Items", "Shutdown Items",
			"Application Support", "Device Drivers", "Network", "System Logs",
			"Software Components", "Extensions", "Extensions (Disabled)",
			"Zac Menu Items", "Zac Menu Items (Disabled)", "System Information", "Extensions Manager", "Favorites" }) {
		check(systemNames.contains(name), "System Folder contains " + name);
	}
	check(vfsOpensAs(vfsPathFor("system-folder/shutdown-items")) ==
			dataHome + "/zacos9/Shutdown Items",
		"Shutdown Items stands for the folder zacos9-shutdown-items runs");
	check(vfsOpensAs(vfsPathFor("system-folder/zac-menu-items")) ==
			root + "/config/zacos9/ZacOS 9 Menu Items",
		"Zac Menu Items stands for the folder the Zac menu reads");
	check(vfsOpensAs(vfsPathFor("system-folder/zac-menu-items-disabled")) ==
			root + "/config/zacos9/ZacOS 9 Menu Items (Disabled)",
		"Zac Menu Items (Disabled) is beside it, where the Zac menu doesn't look");
	check(pathNamed(vfsPathFor("system-folder/utilities"), "System Information") != QString() &&
		pathNamed(vfsPathFor("system-folder/utilities"), "Extensions Manager") != QString(),
		"the trusted System Information and Extensions Manager launchers are also in Utilities");
	const QString sounds = vfsPathFor("system-folder/sounds");
	const QString userSounds = pathNamed(sounds, "User");
	check(!userSounds.isEmpty() && !pathNamed(sounds, "Computer").isEmpty(),
		"sound roots are discovered at runtime as virtual collections");
	const QString pack = pathNamed(userSounds, "pack");
	const QString sound = pathNamed(pack, "chime.wav");
	check(!pack.isEmpty() && !sound.isEmpty(),
		"allowlisted sound descendants retain virtual identities");
	check(vfsIsSounds(sounds) && vfsIsSounds(userSounds) &&
		!vfsIsSounds(pathNamed(sounds, "Computer")),
		"Sounds and its User collection are where a dragged alert sound goes");
	check(vfsAcceptsDrops(sounds) && vfsAcceptsDrops(userSounds) &&
		!vfsAcceptsDrops(pathNamed(sounds, "Computer")) && !vfsAcceptsDrops(pack),
		"they take a dragged sound; the installed set and a subfolder do not");
	check(pathNamed(userSounds, "alias.wav") != QString(),
		"a symlink resolving inside an allowlisted source remains browsable");
	check(pathNamed(userSounds, "escape").isEmpty() &&
		pathNamed(userSounds, "broken").isEmpty() &&
		pathNamed(userSounds, "loop").isEmpty(),
		"broken, looping and out-of-root symlinks are not exposed");
	const QString support = vfsPathFor("system-folder/application-support");
	const QString userSupport = pathNamed(support, "User");
	check(!userSupport.isEmpty() && pathNamed(support, "Computer").isEmpty() &&
		pathNamed(pathNamed(userSupport, "appearance"), "escape").isEmpty(),
		"support aliases reject symlinks that escape the allowlisted root");
	QString soundSource;
	QString directResourceSource;
	const bool directResourceOpen = resourceOpenPath("sounds", "user:pack/chime.wav",
		&directResourceSource);
	check(vfsResourceOpenPath(sound, &soundSource) &&
		soundSource == resourceRoot + "/sounds/user/pack/chime.wav",
		"an alias opens only after canonical-root validation (" + soundSource + ", " +
			(vfsNode(sound) ? vfsNode(sound)->resourceProvider + ":" +
				vfsNode(sound)->resourceKey + ":" +
				QString::number(vfsNode(sound)->resourceDirectory) : QStringLiteral("no node")) +
			", direct=" + QString::number(directResourceOpen) + ':' + directResourceSource +
			", root=" + resourceTestFixtureRoot() + ")");
	check(vfsRealCounterpart(sound).isEmpty() && vfsOpensAs(pack).isEmpty(),
		"resource aliases cannot escape to unrestricted filesystem navigation");
	check(!vfsCanRename(pack) && !vfsCanRename(sound) && !vfsCanDelete(pack) &&
		!vfsAcceptsDrops(pack) && !vfsSetLabel(sound, 2) &&
		!vfsRename(sound, "renamed.wav") && !vfsIsAppFolder(sound),
		"read-only resource capabilities reject rename, delete, labels, drops and app-trash handling");
	QStringList resourceInfo;
	QString resourceKind, resourceLocation, resourceSource, resourceAccess;
	const bool soundDetails = vfsResourceDetails(sound, &resourceInfo, &resourceKind,
		&resourceLocation, &resourceSource, &resourceAccess);
	check(soundDetails && resourceKind.contains("file", Qt::CaseInsensitive) &&
		resourceSource == soundSource &&
		resourceAccess.contains("read-only", Qt::CaseInsensitive) &&
		resourceLocation.contains("Sounds"),
		"resource Get Info reports its kind, location, source and effective access (" +
			resourceKind + ", " + resourceLocation + ", " + resourceSource + ", " +
			resourceAccess + ")");
	const QString startupItems = vfsPathFor("system-folder/startup-items");
	const QString startupEntry = pathNamed(startupItems, "Fixture Startup");
	check(!startupEntry.isEmpty() &&
		pathNamed(startupItems, "Disabled Fixture").isEmpty(),
		"startup discovery includes enabled entries and excludes disabled entries");
	vfsResourceDetails(startupEntry, &resourceInfo, &resourceKind, &resourceLocation,
		&resourceSource, &resourceAccess);
	check(resourceInfo.join('\n').contains("opens at login") &&
		!resourceInfo.join('\n').contains("secret-command"),
		"startup metadata never reveals or executes desktop-entry command text");
	check(vfsIsStartupItems(startupItems) && vfsAcceptsDrops(startupItems) &&
		vfsStartupItemsPath() == startupItems && !vfsIsStartupItems(startupEntry),
		"Startup Items is a drop target for applications");
	check(vfsStartupItemId(startupEntry) == "enabled.desktop" &&
		vfsStartupItemId(startupItems).isEmpty() &&
		vfsStartupItemId(vfsPathFor("system-folder/sounds")).isEmpty(),
		"each startup item names its desktop-file ID, so the Trash removes exactly that entry");
	const QString logs = vfsPathFor("system-folder/system-logs");
	check(pathNamed(logs, "Protected system logs") != QString(),
		"System Logs presents a protected-information explanation, not log contents");
	check(vfsNode(vfsPathFor("system-folder/sounds/../../etc")) == nullptr &&
		!vfsResourceOpenPath(vfsPathFor("system-folder/sounds/../../etc"), &soundSource) &&
		resourceChildren("untrusted-provider").empty(),
		"provider/path traversal attempts fail closed");
	const QStringList systemInfo = systemInformation();
	check(systemInfo.join('\n').contains("0.1.27-fixture") &&
		systemInfo.join('\n').contains("Distribution:") &&
		systemInfo.join('\n').contains("Kernel:") &&
		systemInfo.join('\n').contains("Processor:") &&
		systemInfo.join('\n').contains("Memory:") &&
		systemInfo.join('\n').contains("Storage devices:") &&
		systemInfo.join('\n').contains("Network:") &&
		systemInfo.join('\n').contains("Audio:") &&
		systemInfo.join('\n').contains("Loaded hardware drivers:") &&
		systemInfo.join('\n').contains("Uptime:"),
		"System Information reports installed version and the required hardware/system fields");
	check(!extensionInformation().isEmpty(),
		"Extensions discovery reports loaded drivers or an explicit unavailable state");
	const QString disabledExtensions = vfsPathFor("system-folder/extensions-disabled");
	check(!namesIn(disabledExtensions).isEmpty(),
		"Extensions (Disabled) always lists extensions or an explicit empty state");
	check(vfsLaunch(vfsPathFor("system-folder/system-information")) &&
		vfsLaunch(vfsPathFor("system-folder/extensions-manager")),
		"the only built-in launch actions are the two trusted provider windows");
	check(!vfsLaunch(vfsPathFor("system-folder/fonts")),
		"resource folders do not become launchable commands");
	QDir(resourceRoot + "/sounds/system").removeRecursively();
	check(pathNamed(sounds, "Computer").isEmpty(),
		"a source removed after startup disappears during lazy rediscovery");
	QDir(resourceRoot + "/sounds/user").removeRecursively();
	check(pathNamed(sounds, "No sound folders found") != QString(),
		"missing sound directories yield a safe informational entry");
	QDir().mkpath(resourceRoot + "/sounds/user/pack");
	writeEntry(resourceRoot + "/sounds/user/pack", "chime.wav", "sound data");
	QFile::link(resourceRoot + "/sounds/user/pack/chime.wav",
		resourceRoot + "/sounds/user/alias.wav");
	QFile::link("/etc/passwd", resourceRoot + "/sounds/user/escape");
	QFile::link(resourceRoot + "/sounds/user/missing",
		resourceRoot + "/sounds/user/broken");
	QFile::link(resourceRoot + "/sounds/user", resourceRoot + "/sounds/user/loop");

	check(apps.contains("SimpleText"), "an installed application appears in Applications");
	check(namesIn(vfsPathFor("system-folder/utilities")).contains("SimpleText"),
		"the same name in a different destination does not rename either app");
	check(apps.contains("Calculator"), "a second application appears too");
	check(!apps.contains("Hidden One"), "Hidden=true is not shown");
	check(!apps.contains("NoDisplay One"), "NoDisplay=true is not shown");
	check(!apps.contains("Only Elsewhere"), "OnlyShowIn for another desktop is not shown");
	check(!apps.contains("Not Here"), "NotShowIn for this desktop is not shown");
	check(!apps.contains("Missing Binary"), "a failing TryExec is not shown");
	check(!apps.contains("Utilities"), "shipped Utilities is no longer inside Applications");
	check(!apps.contains("Disk Flasher"), "a shipped utility is not loose in Applications");
	check(namesIn(vfsPathFor("system-folder/utilities")).contains("Disk Flasher"),
		"shipped tools are accessible in System Folder > Utilities");
	check(!namesIn(vfsPathFor("system-folder/utilities")).contains("Calculator"),
		"an ordinary Utility-category application stays in Applications");
	check(!apps.contains("Automatic Dependency") && apps.contains("Explicitly Installed"),
		"automatic Debian dependencies stay out, explicit Debian installs stay in Applications");
	check(appById("automatic.desktop") &&
		appById("automatic.desktop")->origin == "dpkg:finder-auto-demo",
		"architecture-qualified desktop ownership keeps a supported package uninstall identity");
	for (const QString name : { "qps", "pcmanfm-qt", "pcmanfm-qt-desktop-pref",
			"xscreensaver-settings", "system-config-printer" }) {
		check(!apps.contains(name) && namesIn(vfsPathFor("system-folder/utilities")).contains(name),
			"system utility is routed away from Applications: " + name);
	}
	QString trashError;
	check(!appTrashQueue("qps.desktop", &trashError) && !trashError.isEmpty(),
		"system tools cannot be queued for uninstall");
	check(!appTrashQueue("automatic.desktop", &trashError) && !trashError.isEmpty(),
		"automatic dependencies cannot be queued for uninstall");
	check(!vfsAcceptsDrops(vfsPathFor("system-folder/utilities")) &&
		!vfsIsApplications(vfsPathFor("system-folder/utilities")),
		"system Utilities is not an app installation drop target");
	check(apps.contains("Only Ours"), "an entry for this desktop only is shown");
	check(!apps.contains("Came With The System"), "an application the system came with is not shown");
	check(!apps.contains("A ZacOS Panel"), "ZacOS 9's own entries are not shown");
	check(!apps.contains("A ZacOS Disk Tool") &&
			namesIn(vfsPathFor("system-folder/utilities")).contains("A ZacOS Disk Tool") &&
			!namesIn(vfsPathFor("system-folder/utilities")).contains("A ZacOS Panel"),
		"ZacOS 9's own utilities (Disk Setup) are in System Folder > Utilities");
	check(apps.contains("Notepad Plus"), "a Windows program installed through Wine is shown");
	check(!apps.contains("Uninstall Notepad Plus"), "its Wine uninstaller is not shown");
	const int markWatch = vfsOnChange([] {});
	writeEntry(root + "/apt", "extended_states",
		"Package: finder-auto-demo\nArchitecture: amd64\nAuto-Installed: 0\n");
	check(waitFor([&] { return namesIn(vfsPathFor("applications")).contains("Automatic Dependency"); }),
		"marking a dependency manual updates Applications without rewriting its launcher");
	writeEntry(root + "/apt", "extended_states",
		"Package: finder-auto-demo\nArchitecture: amd64\nAuto-Installed: 1\n\n");
	check(waitFor([&] { return !namesIn(vfsPathFor("applications")).contains("Automatic Dependency"); }),
		"marking an app automatic removes it from Applications");
	vfsOffChange(markWatch);
	writeEntry(homeAppsDir, "automatic.desktop", entry("User Dependency Override"));
	settle();
	check(namesIn(vfsPathFor("applications")).contains("User Dependency Override"),
		"user-local launchers are kept even when overriding an automatic package");

	/* First Flatpak install creates the export directories after startup. */
	int appChanges = 0;
	appOnChange([&] { ++appChanges; });
	int folderChanges = 0;
	const int flatpakWatch = vfsOnChange([&] { ++folderChanges; });
	const QString flatpakApps = flatpakData + "/applications";
	const QString chaskiId = "com.a_chacon.chaski.desktop";
	const QString exported = dataHome + "/flatpak/app/chaski/export/share/applications";
	QDir().mkpath(flatpakData + "/icons/hicolor/64x64/apps");
	writeEntry(flatpakData + "/icons/hicolor", "index.theme",
		"[Icon Theme]\nName=Hicolor\nDirectories=64x64/apps\n"
		"[64x64/apps]\nSize=64\nType=Fixed\nContext=Applications\n");
	QImage flatpakIcon(64, 64, QImage::Format_ARGB32);
	flatpakIcon.fill(0xff123456);
	check(flatpakIcon.save(flatpakData + "/icons/hicolor/64x64/apps/com.a_chacon.chaski.png"),
		"the first Flatpak export supplies an application icon");
	writeEntry(exported, chaskiId, entry("Chaski", "/bin/true",
		"Categories=Network;News;\nIcon=com.a_chacon.chaski\nX-Flatpak=com.a_chacon.chaski\n"));
	QDir().mkpath(flatpakApps);
	check(QFile::link(exported + "/" + chaskiId, flatpakApps + "/" + chaskiId),
		"the Flatpak fixture exports a symlinked launcher");
	check(waitFor([&] { return appChanges > 0 && folderChanges > 0 && appById(chaskiId); }),
		"first Flatpak install is discovered automatically without manual refresh");
	if (const AppEntry *chaski = appById(chaskiId)) {
		check(chaski->name == "Chaski" && chaski->origin == "flatpak:com.a_chacon.chaski",
			"the newly exported app keeps its name and Flatpak identity");
		check(!chaski->icon32.empty(), "icons from a newly created Flatpak export tree resolve");
	}
	check(namesIn(vfsPathFor("applications")).contains("Chaski"),
		"the newly exported app appears in Applications");
	const int changesBeforeRemoval = appChanges;
	check(QFile::remove(flatpakApps + "/" + chaskiId), "the fixture removes the exported launcher");
	check(waitFor([&] { return appChanges > changesBeforeRemoval && !appById(chaskiId); }),
		"removing a Flatpak launcher refreshes Applications automatically");
	check(QDir().rmdir(flatpakApps), "the fixture removes its empty export applications directory");
	QDir().mkpath(flatpakApps);
	check(QFile::link(exported + "/" + chaskiId, flatpakApps + "/" + chaskiId),
		"the fixture recreates the export applications directory and launcher");
	check(waitFor([&] { return appById(chaskiId) &&
		namesIn(vfsPathFor("applications")).contains("Chaski"); }),
		"a replaced Flatpak export directory is watched again");
	QFile::remove(flatpakApps + "/" + chaskiId);
	check(waitFor([&] { return !appById(chaskiId); }),
		"removal after directory replacement is noticed");
	vfsOffChange(flatpakWatch);

	/* ---- the startup disk --------------------------------------------- */
	const QString sharedDocuments = root + "/Shared";
	QFile initialRegistry(vfsRegistryPath());
	check(initialRegistry.open(QIODevice::ReadOnly), "read the startup disk fixture registry");
	QJsonObject initialMapping = QJsonDocument::fromJson(initialRegistry.readAll()).object();
	initialRegistry.close();
	QJsonArray initialNodes = initialMapping.value("nodes").toArray();
	for (int i = 0; i < initialNodes.size(); ++i) {
		QJsonObject node = initialNodes[i].toObject();
		if (node.value("id").toString() == "shared-documents") {
			node.insert("backing", sharedDocuments);
			initialNodes[i] = node;
		}
	}
	initialMapping.insert("nodes", initialNodes);
	QSaveFile fixtureRegistry(vfsRegistryPath());
	const QByteArray mappingData = QJsonDocument(initialMapping).toJson();
	check(fixtureRegistry.open(QIODevice::WriteOnly) &&
		fixtureRegistry.write(mappingData) == mappingData.size() && fixtureRegistry.commit(),
		"Shared Documents uses a private fixture, not the host's /home/Shared");
	vfsRefresh();
	const QStringList expectedStartupItems{ "System Folder", "Applications", "Home" };
	QStringList volume = namesIn(vfsRoot());
	check(volume.contains("System Folder"), "the startup disk holds a System Folder");
	check(volume.contains("Applications"), "the startup disk holds Applications");
	check(!volume.contains("Documents"), "Documents is gone: Home holds it");
	check(volume.contains("Home"), "the startup disk holds the user's Home folder");
	check(!volume.contains("Utilities"), "Utilities is off until the registry asks for it");
	check(volume == expectedStartupItems,
		"the startup disk lists exactly its curated items when Shared Documents is absent");
	check(QDir().mkdir(sharedDocuments), "create the private Shared Documents folder");
	vfsRefresh();
	check(namesIn(vfsRoot()) == (expectedStartupItems + QStringList{ "Shared Documents" }),
		"an existing Shared Documents folder appears exactly once on the startup disk");
	check(vfsRealCounterpart(vfsPathFor("shared-documents")) == sharedDocuments,
		"Shared Documents opens its configured real folder");
	check(QDir().rmdir(sharedDocuments), "remove the private Shared Documents folder");
	vfsRefresh();
	check(namesIn(vfsRoot()) == expectedStartupItems,
		"Shared Documents disappears when its backing folder is removed");
	for (const QString unix_ : { "usr", "etc", "bin", "var", "lib", "proc", "sys", "home" }) {
		check(!volume.contains(unix_), "the startup disk does not show /" + unix_);
	}

	QStringList system = namesIn(vfsPathFor("system-folder"));
	check(system.contains("Control Panels"), "the System Folder holds Control Panels");
	check(system.contains("Extensions"), "the System Folder holds Extensions");
	check(system.contains("Fonts"), "the System Folder holds Fonts");
	check(system.contains("Preferences"), "the System Folder holds Preferences");

	const VNode *sysNode = vfsNode(vfsPathFor("system-folder"));
	check(sysNode && sysNode->icon == PL_ICON_SYSTEM_FOLDER,
		"the System Folder has its own icon");
	const VNode *appsNode = vfsNode(vfsPathFor("applications"));
	const VNode *homeNode = vfsNode(vfsPathFor("home"));
	check(appsNode && appsNode->icon == PL_ICON_APPLICATIONS_FOLDER && homeNode &&
		homeNode->icon == PL_ICON_HOME_FOLDER, "Applications and Home have their own icons");
	const VNode *fontsNode = vfsNode(vfsPathFor("system-folder/fonts"));
	const VNode *favNode = vfsNode(vfsPathFor("system-folder/favorites"));
	const VNode *extNode = vfsNode(vfsPathFor("system-folder/extensions"));
	check(fontsNode && fontsNode->icon == PL_ICON_SF_FONTS && favNode && favNode->icon == PL_ICON_SF_FAVORITES &&
		extNode && extNode->icon == PL_ICON_SF_EXTENSIONS, "the System Folder's folders have their emblems");

	QStringList panels = namesIn(vfsPathFor("system-folder/control-panels"));
	check(panels.contains("Appearance") && panels.contains("File Sharing") &&
		panels.contains("TCP/IP"), "Control Panels lists the control panels");
	check(panels.contains("Bluetooth") && panels.contains("Users") && panels.contains("Walkies") &&
		panels.contains("Keychain Access"), "Control Panels has every panel the Zac menu has");
	const VNode *mouse = vfsNode(vfsPathFor("system-folder/control-panels/zacos9-controlpanel-mouse"));
	const VNode *napTime =
		vfsNode(vfsPathFor("system-folder/control-panels/zacos9-controlpanel-energysaver"));
	check(mouse && mouse->icon == PL_ICON_CP_MOUSE && napTime && napTime->icon == PL_ICON_EXT_POWER,
		"each control panel has its own icon, as in the Zac menu");

	/* Control Panels (Disabled): the panels the user turned off, still
	 * their own entries, for dragging back as Mac OS 9's Extensions
	 * Manager did. */
	const QString controlPanels = vfsPathFor("system-folder/control-panels");
	const QString disabledPanels = vfsPathFor("system-folder/control-panels-disabled");
	check(namesIn(vfsPathFor("system-folder")).contains("Control Panels (Disabled)"),
		"the System Folder holds Control Panels (Disabled)");
	check(vfsIsControlPanels(controlPanels) && vfsIsControlPanelsDisabled(disabledPanels),
		"the two control-panel folders know themselves");
	const VNode *disabledNode = vfsNode(disabledPanels);
	check(disabledNode && disabledNode->icon == PL_ICON_CONTROL_PANELS &&
		disabledNode->kind == VKind::PanelsDisabled,
		"the (Disabled) folder has Control Panels' icon");
	check(namesIn(disabledPanels).isEmpty(), "and is empty while every panel is on");

	/* One turned off moves there, keeping its icon, its place and its
	 * entry's id; the panel itself is still there to open. */
	check(pl_control_panel_set_enabled("zacos9-mouse.desktop", false), "a panel can be turned off");
	vfsRefresh();
	check(namesIn(disabledPanels) == QStringList{ "Mouse" },
		"which moves it to Control Panels (Disabled)");
	check(!namesIn(controlPanels).contains("Mouse"), "and out of Control Panels");
	const VNode *offMouse = vfsNode(disabledPanels + "/zacos9-controlpanel-mouse");
	check(offMouse && offMouse->kind == VKind::Launcher && offMouse->icon == PL_ICON_CP_MOUSE &&
		offMouse->panelId == "zacos9-mouse.desktop",
		"keeping its icon, its place and its entry's id");
	check(offMouse && offMouse->program == "zacos9-controlpanel" && offMouse->args == QStringList{ "mouse" },
		"and still a control panel, to open from there too");
	check(pl_control_panel_set_enabled("zacos9-mouse.desktop", true), "turned back on");
	vfsRefresh();
	check(namesIn(disabledPanels).isEmpty() && namesIn(controlPanels).contains("Mouse"),
		"which puts it back in Control Panels, and leaves (Disabled) empty");

	/* A registry written before the folder existed gains it, keeping what
	 * the user set. */
	QFile olderRegistry(vfsRegistryPath());
	QJsonObject older = olderRegistry.open(QIODevice::ReadOnly) ?
		QJsonDocument::fromJson(olderRegistry.readAll()).object() : QJsonObject();
	olderRegistry.close();
	QJsonArray olderNodes;
	for (const QJsonValue &value : older.value("nodes").toArray()) {
		if (value.toObject().value("id").toString() != "system-folder/control-panels-disabled") {
			olderNodes.append(value);
		}
	}
	older.insert("nodes", olderNodes);
	older.insert("version", 19);
	if (olderRegistry.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		olderRegistry.write(QJsonDocument(older).toJson());
		olderRegistry.close();
	}
	vfsRefresh();
	check(namesIn(vfsPathFor("system-folder")).contains("Control Panels (Disabled)"),
		"a registry written before the folder existed gains it");

	/* ---- an application appears as a direct launcher in Applications --- */
	const VNode *launcher = vfsNode(vfsPathFor("applications/simpletext.desktop"));
	check(launcher && launcher->kind == VKind::Launcher &&
		launcher->icon == PL_ICON_APPLICATION, "an application is a direct launcher");
	check(launcher && launcher->appId == "simpletext.desktop",
		"identity is the desktop file id, not the display name");
	check(namesIn(appFolder("simpletext.desktop")).empty(),
		"a launcher has no children of its own");

	writeEntry(appsDir, "browser.desktop", entry("Browser", "/bin/true"));

	/* ---- where an application came from --------------------------------- */
	/* /bin/true is owned by coreutils on Debian; a real package lookup,
	 * not a guess, so this is the one check tied to the host system. */
	if (const AppEntry *coreutil = appById("simpletext.desktop")) {
		check(coreutil->origin == "dpkg:coreutils",
			"an application's package is found from its executable (dpkg:coreutils)");
		check(!coreutil->version.isEmpty(), "and its installed version");
	} else {
		check(false, "simpletext.desktop was not found to check its origin");
	}

	/* A Flatpak export names its own application id; it is never
	 * attributed to the "flatpak" package that merely runs it. Real
	 * Exec on a Flatpak export runs the flatpak binary itself (e.g.
	 * "/usr/bin/flatpak run org.example.Flatpak"); GIO requires Exec's
	 * program to actually resolve or it drops the entry outright (true
	 * even with no TryExec key), and this sandbox has no flatpak
	 * installed, so /bin/true stands in for it here. */
	writeEntry(appsDir, "org.example.Flatpak.desktop",
		entry("Flatpak App", "/bin/true", "X-Flatpak=org.example.Flatpak\n"));
	settle();
	if (const AppEntry *flatpak = appById("org.example.Flatpak.desktop")) {
		check(flatpak->origin == "flatpak:org.example.Flatpak",
			"a Flatpak export is identified by its own application id");
	} else {
		check(false, "the Flatpak entry was not discovered");
	}

	/* An entry with no resolvable executable and no X-Flatpak key (a
	 * hand-written one, say) has no origin at all: never guessed. */
	writeEntry(appsDir, "handwritten.desktop", entry("Handwritten", "/bin/true"));
	settle();
	/* (still dpkg:coreutils, since /bin/true resolves the same way) */
	if (const AppEntry *handwritten = appById("handwritten.desktop")) {
		check(handwritten->origin == "dpkg:coreutils",
			"two entries exec'ing the same program share its package, not a guess each");
	}

	/* ---- icons -------------------------------------------------------- */
	/* mousepad.desktop ships a real icon in the hicolor theme on a
	 * standard Debian desktop; a made-up name resolves to nothing. */
	writeEntry(appsDir, "org.xfce.mousepad.desktop",
		entry("Mousepad", "/usr/bin/true", "Icon=org.xfce.mousepad\n"));
	writeEntry(appsDir, "noicon.desktop",
		entry("No Icon", "/bin/true", "Icon=this-icon-does-not-exist-anywhere\n"));
	settle();
	if (const AppEntry *mousepad = appById("org.xfce.mousepad.desktop")) {
		if (!mousepad->icon32.empty()) {
			check(mousepad->icon32.size() == 32 * 32, "a resolved icon is 32x32 ARGB");
			check(mousepad->icon16.size() == 16 * 16, "and 16x16 for the small views");
			bool anyOpaque = false;
			for (uint32_t px : mousepad->icon32) {
				anyOpaque = anyOpaque || (px >> 24) != 0;
			}
			check(anyOpaque, "the resolved icon has some non-transparent pixels");
		} else {
			/* Our own sandbox deliberately keeps XDG_DATA_DIRS away
			 * from the real /usr/share/icons, so this is expected here
			 * even when the real desktop would resolve it; that is not
			 * a failure of the resolver. */
			check(true, "mousepad's icon isn't reachable from this test's "
				"sandboxed XDG_DATA_DIRS; skipping its shape check");
		}
	}
	if (const AppEntry *noIcon = appById("noicon.desktop")) {
		check(noIcon->icon32.empty(),
			"an icon name that resolves to nothing falls back cleanly, not to garbage");
	}

	/* ---- duplicate names ---------------------------------------------- */
	writeEntry(appsDir, "files-one.desktop", entry("Files"));
	writeEntry(appsDir, "files-two.desktop", entry("Files"));
	settle();
	apps = namesIn(vfsPathFor("applications"));
	check(!apps.contains("Files"), "two applications called Files aren't both just Files");
	check(apps.contains("Files (files-one)") && apps.contains("Files (files-two)"),
		"each is told apart by its desktop file id");
	check(apps.count("Files (files-one)") == 1, "and each appears once");

	/* ---- XDG precedence ----------------------------------------------- */
	/* The same id in XDG_DATA_HOME wins over the system one. */
	writeEntry(homeAppsDir, "simpletext.desktop", entry("SimpleText Override"));
	settle();
	apps = namesIn(vfsPathFor("applications"));
	check(apps.contains("SimpleText Override"),
		"an entry in XDG_DATA_HOME overrides the system one of that id");
	check(!apps.contains("SimpleText"), "and the overridden one does not also appear");

	/* ---- launching ---------------------------------------------------- */
	const QString stamp = root + "/launched";
	writeEntry(appsDir, "toucher.desktop",
		entry("Toucher", "/usr/bin/touch " + stamp + " %f"));
	settle();
	check(appLaunch("toucher.desktop"), "an application launches by its desktop file id");
	QElapsedTimer waited;
	waited.start();
	while (!QFile::exists(stamp) && waited.elapsed() < 5000) {
		usleep(20000);
	}
	check(QFile::exists(stamp), "launching ran the program named by Exec");
	check(!appLaunch("not-installed.desktop"), "launching an entry that isn't there fails");
	writeEntry(appsDir, "broken-launch.desktop",
		entry("Broken Launch", "/nonexistent/zacos9-launch-test"));
	check(!appLaunch("broken-launch.desktop"), "a failed process launch is reported");
	const QString document = root + "/document.txt";
	const QString documentStamp = root + "/document-launched";
	writeEntry(root, "document.txt", "launch feedback test\n");
	writeEntry(appsDir, "document-handler.desktop",
		entry("Document Handler", "/usr/bin/touch " + documentStamp + " %f",
			"MimeType=text/plain;\n"));
	writeEntry(root + "/config", "mimeapps.list",
		"[Default Applications]\ntext/plain=document-handler.desktop;\n");
	settle();
	check(appOpenFile(document), "a document opens with its registered application");
	waited.restart();
	while (!QFile::exists(documentStamp) && waited.elapsed() < 5000) {
		usleep(20000);
	}
	check(QFile::exists(documentStamp), "document opening launches the default handler");
	check(!appOpenFile(root + "/missing-document"), "a missing document reports an error");

	/* ---- what the Finder allows --------------------------------------- */
	const QString appsPath = vfsPathFor("applications");
	const QString appPath = appFolder("browser.desktop"); /* the launcher itself */
	check(!vfsCanDelete(appPath), "an application can't be thrown away");
	check(!vfsCanDelete(appsPath), "the Applications folder can't be thrown away");
	check(!vfsCanDelete(vfsPathFor("system-folder")),
		"the System Folder can't be thrown away");
	check(vfsAcceptsDrops(appsPath), "Applications takes drops (it installs install files)");
	check(vfsIsApplications(appsPath), "and is known as Applications");
	check(!vfsIsApplications(vfsPathFor("home")), "Home isn't Applications");
	check(!vfsAcceptsDrops(vfsPathFor("system-folder")), "the System Folder takes no drops");
	check(vfsAcceptsDrops(vfsPathFor("home")), "Home takes drops");
	check(vfsCanRename(appPath), "an application can be renamed");

	/* ---- hiding an application (never uninstalling it) ----------------- */
	check(vfsIsAppFolder(appPath), "an application launcher is recognized as one");
	check(!vfsIsAppFolder(appsPath), "the Applications folder itself is not");
	check(vfsSetLabel(appPath, 3), "give it a label, to check it isn't lost");
	check(vfsHideApplication(appPath), "an application can be hidden");
	apps = namesIn(vfsPathFor("applications"));
	check(!apps.contains("Browser"), "a hidden application no longer appears");
	check(vfsNode(appPath) != nullptr,
		"but its node, and so its remembered label and position, still exist");
	vfsRefresh();
	apps = namesIn(vfsPathFor("applications"));
	check(!apps.contains("Browser"), "hiding an application outlasts a reload, same as a rename");
	vfsShowAllHidden();
	apps = namesIn(vfsPathFor("applications"));
	check(apps.contains("Browser"), "Show All Applications brings it back");
	check(vfsLabel(appPath) == 3, "bringing it back did not also clear its label");
	check(!vfsHideApplication(appsPath), "the Applications folder itself can't be hidden");

	/* ---- Get Info reads the real thing an item stands for -------------- */
	check(vfsRealCounterpart(appPath).endsWith("browser.desktop"),
		"Get Info on an application reads its desktop entry");
	check(vfsRealCounterpart(vfsPathFor("home")).startsWith(root),
		"Get Info on Home reads its real directory");
	check(vfsRealCounterpart(vfsPathFor("system-folder")).isEmpty(),
		"a curated folder stands for nothing on disk");
	QStringList folderInfo;
	QString folderKind, folderLocation, folderSource, folderAccess;
	check(vfsResourceDetails(vfsPathFor("system-folder"), &folderInfo, &folderKind,
			&folderLocation, &folderSource, &folderAccess) && folderKind == "system folder" &&
			folderInfo.join('\n').contains("Version: 0.1.27-fixture") &&
			folderInfo.join('\n').contains("Package ownership: ZacOS 9"),
		"Get Info on the System Folder shows its version and origin");

	/* ---- folders standing for real directories ------------------------ */
	const QString documents = vfsOpensAs(vfsPathFor("home"));
	check(!documents.isEmpty() && QDir(documents).exists(),
		"Home opens a real directory, made if it was missing");
	check(documents.startsWith(root), "and it is inside the home folder, not a system one");
	const QString prefs = vfsOpensAs(vfsPathFor("system-folder/preferences"));
	const QString desktopPatterns = vfsOpensAs(vfsPathFor("system-folder/appearance/desktop-patterns"));
	check(desktopPatterns == dataHome + "/zacos9/appearance/Desktop Patterns" &&
		QDir(desktopPatterns).exists(), "System Folder exposes a writable custom desktop patterns folder");
	const QString wallpaper = vfsOpensAs(vfsPathFor("system-folder/appearance/wallpaper"));
	check(wallpaper == dataHome + "/zacos9/appearance/Wallpaper" && QDir(wallpaper).exists(),
		"Appearance contains a writable Wallpaper folder");
	check(!namesIn(vfsPathFor("system-folder")).contains("Wallpaper"),
		"Wallpaper is not directly inside System Folder");
	check(vfsOpensAs(vfsPathFor("system-folder/appearance/themes")) ==
		dataHome + "/zacos9/appearance/Themes",
		"Appearance contains a custom preset Themes folder");
	check(vfsOpensAs(vfsPathFor("system-folder/appearance/sound-themes")) ==
		dataHome + "/zacos9/appearance/Sound Themes",
		"Appearance contains a custom Sound Themes folder");
	check(prefs == root + "/config", "Preferences stands for the XDG config directory");
	check(vfsOpensAs(vfsPathFor("system-folder")).isEmpty(),
		"a curated folder opens as a virtual window, not a directory");
	check(vfsList(vfsPathFor("home")).empty(),
		"a folder standing for a directory has no virtual children");

	/* ---- renames and labels persist ----------------------------------- */
	check(vfsRename(appPath, "Web Browser"), "an application can be renamed");
	check(vfsName(appPath) == "Web Browser", "the new name is used");
	check(vfsSetLabel(appPath, 2), "a label can be set on it");
	check(vfsLabel(appPath) == 2, "and read back");
	check(!vfsRename(appPath, "   "), "an empty name is refused");

	/* A rename doesn't move the item: its id, and so its icon's place
	 * and its window, stay as they were. */
	check(vfsNode(appPath) != nullptr, "the renamed item keeps its id");

	/* Reload from the registry file, as a fresh Finder would. */
	vfsRefresh();
	check(vfsName(appPath) == "Web Browser", "the rename outlasts a reload");
	check(vfsLabel(appPath) == 2, "the label outlasts a reload");
	check(QFile::exists(vfsRegistryPath()), "the registry was written");
	check(vfsRegistryPath().startsWith(root),
		"the registry is in the user's own data directory");

	/* Renaming to a name a sibling already has is refused by the Finder. */
	writeEntry(appsDir, "twin.desktop", entry("Twin"));
	settle();
	check(vfsNameTaken(appFolder("browser.desktop"), "Twin"),
		"a sibling's name is reported as taken");
	check(!vfsNameTaken(appFolder("browser.desktop"), "Nothing Called This"),
		"an unused name is free");

	/* ---- uninstalling -------------------------------------------------- */
	QFile::remove(appsDir + "/calculator.desktop");
	settle();
	apps = namesIn(vfsPathFor("applications"));
	check(!apps.contains("Calculator"), "an uninstalled application goes away");
	check(apps.contains("Web Browser") && apps.contains("SimpleText Override"),
		"and the others stay");
	check(vfsNode(appFolder("calculator.desktop")) == nullptr,
		"its node goes with it");
	check(vfsName(appPath) == "Web Browser" && apps.contains("Web Browser"),
		"a rename persists across the uninstall of another application");

	/* A rename kept for an application that has gone is harmless. */
	QFile::remove(appsDir + "/browser.desktop");
	settle();
	check(vfsNode(appPath) == nullptr, "a removed application's node goes too");
	vfsRefresh();
	check(true, "a rename left over for a removed application does no harm");

	/* ---- the power-user view of Debian --------------------------------- */
	check(!vfsUnixVolumeShown(), "the real filesystem is not shown by default");
	vfsSetUnixVolumeShown(true);
	check(vfsUnixVolumeShown(), "it can be turned on");
	vfsRefresh();
	check(vfsUnixVolumeShown(), "and that outlasts a reload");
	check(namesIn(vfsRoot()) == expectedStartupItems,
		"turning it on does not put Unix directories on the startup disk");
	vfsSetUnixVolumeShown(false);
	check(!vfsUnixVolumeShown(), "and off again");

	/* ---- the registry is configurable ---------------------------------- */
	QFile reg(vfsRegistryPath());
	check(reg.open(QIODevice::ReadOnly), "the registry can be read");
	QJsonObject registry = QJsonDocument::fromJson(reg.readAll()).object();
	reg.close();
	check(registry.value("version").toInt() >= 1, "it records its version");
	check(registry.value("nodes").toArray().size() >= 8, "it lists the mapping");
	/* Show Utilities and rename the disk, as a user editing it would. */
	QJsonObject volumeObj;
	volumeObj.insert("name", "Work HD");
	registry.insert("volume", volumeObj);
	QJsonArray nodes = registry.value("nodes").toArray();
	for (int i = 0; i < nodes.size(); i++) {
		QJsonObject node = nodes[i].toObject();
		if (node.value("id").toString() == "utilities") {
			node.insert("visible", true);
			nodes[i] = node;
		}
	}
	registry.insert("nodes", nodes);
	if (reg.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		reg.write(QJsonDocument(registry).toJson());
		reg.close();
	}
	vfsRefresh();
	check(vfsVolumeName() == "Work HD", "the disk can be renamed in the registry");
	volume = namesIn(vfsRoot());
	check(volume.contains("Utilities"), "Utilities can be switched on in the registry");

	/* Utilities keeps only the categories it names. */
	writeEntry(appsDir, "wrench.desktop",
		entry("Wrench", "/bin/true", "Categories=Utility;\n"));
	writeEntry(appsDir, "game.desktop",
		entry("Game", "/bin/true", "Categories=Game;\n"));
	settle();
	QStringList utilities = namesIn(vfsPathFor("utilities"));
	check(utilities.contains("Wrench"), "a Utility-category application is in Utilities");
	check(!utilities.contains("Game"), "one in another category is not");

	/* Version 8 moves shipped utilities and their overrides into System Folder. */
	registry.insert("version", 8);
	nodes = registry.value("nodes").toArray();
	for (int i = 0; i < nodes.size(); ++i) {
		QJsonObject node = nodes[i].toObject();
		if (node.value("id").toString() == "system-folder/utilities") {
			node.insert("id", "applications/utilities");
			node.insert("name", "My Tools");
			node.insert("categories", QJsonArray{ "X-ZacOS9-Utility" });
			nodes[i] = node;
		}
	}
	registry.insert("nodes", nodes);
	QJsonObject oldOverrides = registry.value("overrides").toObject();
	oldOverrides.insert("applications/qps.desktop",
		QJsonObject{ { "name", "Process Viewer" }, { "label", 4 } });
	registry.insert("overrides", oldOverrides);
	if (reg.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		reg.write(QJsonDocument(registry).toJson());
		reg.close();
	}
	vfsRefresh();
	check(vfsName(vfsPathFor("system-folder/utilities")) == "My Tools" &&
		!namesIn(vfsPathFor("applications")).contains("My Tools"),
		"version 9 moves the Utilities node while preserving a custom folder name");
	check(vfsName(vfsPathFor("system-folder/utilities/qps.desktop")) == "Process Viewer" &&
		vfsLabel(vfsPathFor("system-folder/utilities/qps.desktop")) == 4,
		"migrated system launchers retain their renamed title and label");

	/* ---- migration ----------------------------------------------------- */
	/* An older registry keeps the user's own nodes and gains ours. */
	QJsonObject old;
	old.insert("version", 0);
	QJsonArray oldNodes;
	QJsonObject mine;
	mine.insert("id", "my-folder");
	mine.insert("name", "My Folder");
	mine.insert("kind", "folder");
	oldNodes.append(mine);
	old.insert("nodes", oldNodes);
	if (reg.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		reg.write(QJsonDocument(old).toJson());
		reg.close();
	}
	vfsRefresh();
	volume = namesIn(vfsRoot());
	check(volume.contains("My Folder"), "migration keeps a node the user added");
	check(volume.contains("System Folder") && volume.contains("Applications") &&
		volume.contains("Home"), "and adds the ones this version expects");

	/* Version 16: an older Applications and Home get their icons; one the
	 * user chose stays. */
	{
		QJsonObject v15;
		v15.insert("version", 15);
		QJsonObject apps{ { "id", "applications" }, { "name", "Applications" }, { "kind", "apps" }, { "order", 1 } };
		QJsonObject home{ { "id", "home" }, { "name", "Home" }, { "kind", "backed" }, { "backing", "$HOME" },
			{ "order", 2 }, { "icon", "document" } };
		v15.insert("nodes", QJsonArray{ apps, home });
		if (reg.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			reg.write(QJsonDocument(v15).toJson());
			reg.close();
		}
		vfsRefresh();
		const VNode *a = vfsNode(vfsPathFor("applications"));
		const VNode *h = vfsNode(vfsPathFor("home"));
		check(a && a->icon == PL_ICON_APPLICATIONS_FOLDER, "migration gives Applications its bone");
		check(h && h->icon == PL_ICON_DOCUMENT, "and keeps an icon the user gave Home");
	}
	if (reg.open(QIODevice::ReadOnly)) {
		check(QJsonDocument::fromJson(reg.readAll()).object().value("version").toInt() >= 1,
			"and writes the new version back");
		reg.close();
	}

	/* Version 18 restores the Extensions Manager's name unless the user
	 * chose one of their own. */
	{
		QJsonObject v17;
		v17.insert("version", 17);
		QJsonObject manager{ { "id", "system-folder/extensions-manager" },
			{ "name", "Tricks Manager" }, { "kind", "launcher" }, { "action", "extensions-manager" } };
		v17.insert("nodes", QJsonArray{ manager });
		if (reg.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			reg.write(QJsonDocument(v17).toJson());
			reg.close();
		}
		vfsRefresh();
		check(vfsName(vfsPathFor("system-folder/extensions-manager")) == "Extensions Manager",
			"migration restores the Extensions Manager's Mac OS 9 name");
		QJsonObject custom = manager;
		custom.insert("name", "My Extension Tool");
		v17.insert("nodes", QJsonArray{ custom });
		if (reg.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			reg.write(QJsonDocument(v17).toJson());
			reg.close();
		}
		vfsRefresh();
		check(vfsName(vfsPathFor("system-folder/extensions-manager")) == "My Extension Tool",
			"and keeps a name the user chose");
	}

	/* Version 6's Wallpaper node moves without losing user overrides. */
	old.insert("version", 6);
	QJsonObject oldWallpaper;
	oldWallpaper.insert("id", "system-folder/wallpaper");
	oldWallpaper.insert("name", "My Wallpapers");
	oldWallpaper.insert("kind", "backed");
	oldWallpaper.insert("backing", root + "/custom-wallpapers");
	oldNodes.append(oldWallpaper);
	old.insert("nodes", oldNodes);
	if (reg.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		reg.write(QJsonDocument(old).toJson());
		reg.close();
	}
	vfsRefresh();
	check(vfsName(vfsPathFor("system-folder/appearance/wallpaper")) == "My Wallpapers" &&
		vfsOpensAs(vfsPathFor("system-folder/appearance/wallpaper")) == root + "/custom-wallpapers",
		"Wallpaper migration preserves the user's name and backing override");
	check(!namesIn(vfsPathFor("system-folder")).contains("My Wallpapers"),
		"the migrated Wallpaper node no longer appears directly under System Folder");

	/* ---- folders of the user's own ------------------------------------- */
	const QString userFolder = vfsNewFolder();
	check(!userFolder.isEmpty() && vfsIsUserFolder(userFolder) && namesIn(vfsRoot()).contains("untitled folder"),
		"New Folder on the startup disk makes a folder of the user's own");
	const QString userFolder2 = vfsNewFolder();
	check(namesIn(vfsRoot()).contains("untitled folder 2") && userFolder2 != userFolder, "the next one is told apart");
	check(vfsAcceptsDrops(userFolder) && vfsOpensAs(userFolder).startsWith(root) && QDir(vfsOpensAs(userFolder)).exists(),
		"it takes files, in a real directory inside the user's own");
	check(vfsRename(userFolder, "Projects") && namesIn(vfsRoot()).contains("Projects"), "and can be renamed");
	vfsRefresh();
	check(namesIn(vfsRoot()).contains("Projects"), "and it outlasts a reload");
	QFile keep(vfsOpensAs(userFolder) + "/a.txt");
	keep.open(QIODevice::WriteOnly);
	keep.close();
	check(!vfsDeleteUserFolder(userFolder) && namesIn(vfsRoot()).contains("Projects"),
		"a folder holding files isn't thrown away");
	keep.remove();
	check(vfsDeleteUserFolder(userFolder) && !namesIn(vfsRoot()).contains("Projects"), "an empty one is");
	check(!vfsDeleteUserFolder(vfsPathFor("home")), "Home is not one of them");
	vfsDeleteUserFolder(userFolder2);

	/* ---- Mac disk image recognition ----------------------------------- */
	for (const QString &suffix : QStringList{ "dmg", "sparseimage" }) {
		QFile image(root + "/disk." + suffix);
		image.open(QIODevice::WriteOnly);
		image.write("image");
		image.close();
		check(isMacDiskImage(image.fileName()) && iconKindFor(image.fileName()) == PL_ICON_DISK_IMAGE,
			"Mac container images have a mountable disk-image icon");
	}
	const QString bundle = root + "/disk.sparsebundle";
	QDir().mkpath(bundle);
	QFile bundleInfo(bundle + "/Info.plist");
	bundleInfo.open(QIODevice::WriteOnly);
	bundleInfo.write("plist");
	bundleInfo.close();
	check(isMacDiskImage(bundle) && iconKindFor(bundle) == PL_ICON_DISK_IMAGE,
		"a sparse bundle is a disk image, not an ordinary folder");
	for (const QByteArray &signature : { QByteArray("NXSB"), QByteArray("EFI PART") }) {
		QByteArray data(400 * 1024, '\0');
		data.replace(signature == "NXSB" ? 32 : 512, signature.size(), signature);
		QFile image(root + "/disk.img");
		image.open(QIODevice::WriteOnly);
		image.write(data);
		image.close();
		check(isMacDiskImage(image.fileName()), "APFS and GPT raw images are recognized");
	}

	/* ---- console ROM recognition --------------------------------------- */
	{
		const auto make = [&](const QString &name, const QByteArray &bytes = QByteArray()) {
			QFile f(root + "/" + name);
			f.open(QIODevice::WriteOnly);
			f.write(bytes);
			f.close();
			return f.fileName();
		};
		check(romIconKind(make("game.nes", QByteArray("NES\x1A", 4))) == PL_ICON_ROM_NES &&
			iconKindFor(root + "/game.nes") == PL_ICON_ROM_NES,
			"a NES ROM gets its own icon");
		check(romIconKind(make("not.nes", "x")) == PL_ICON_DOCUMENT,
			"a .nes without the header is an ordinary document");
		check(romIconKind(make("game.sfc")) == PL_ICON_ROM_SNES &&
			romIconKind(make("game.smc")) == PL_ICON_ROM_SNES, "Super NES ROMs are recognized");
		check(romIconKind(make("game.n64")) == PL_ICON_ROM_N64 &&
			romIconKind(make("game.z64")) == PL_ICON_ROM_N64 &&
			romIconKind(make("game.v64")) == PL_ICON_ROM_N64, "Nintendo 64 ROMs are recognized");
		check(romIconKind(make("game.gen")) == PL_ICON_ROM_SEGA &&
			romIconKind(make("game.smd")) == PL_ICON_ROM_SEGA &&
			romIconKind(make("game.sms")) == PL_ICON_ROM_SEGA, "Sega ROMs are recognized");
		check(romIconKind(make("readme.md", "a small markdown file")) == PL_ICON_DOCUMENT,
			"a .md document is not a Sega cartridge");
		{
			QByteArray rom(128 * 1024, '\0');
			rom.replace(0x100, 4, "SEGA");
			check(romIconKind(make("sonic.md", rom)) == PL_ICON_ROM_SEGA,
				"a .md with the Genesis header is a Sega cartridge");
		}
		check(romIconKind(make("game.gb")) == PL_ICON_ROM_GAMEBOY &&
			romIconKind(make("game.gbc")) == PL_ICON_ROM_GAMEBOY &&
			romIconKind(make("game.gba")) == PL_ICON_ROM_GAMEBOY, "Game Boy games are recognized");
		check(romIconKind(make("game.cue")) == PL_ICON_ROM_PS1 &&
			iconKindFor(root + "/game.cue") == PL_ICON_ROM_PS1, "a PlayStation cue sheet is a disc");
		check(romIconKind(make("game.bin")) == PL_ICON_ROM_PS1,
			"a disc track beside its cue sheet is a PlayStation disc");
		check(romIconKind(make("alone.bin")) == PL_ICON_DOCUMENT,
			"a bare .bin with no cue sheet is an ordinary document");
	}

	/* ---- unprivileged -------------------------------------------------- */
	check(!namesIn(vfsRoot()).isEmpty(), "browsing needs no privileges");
	/* Every directory the Finder may write into through this hierarchy
	 * belongs to the user, so no ordinary action needs root and none can
	 * reach into a system location. */
	bool allInHome = true;
	for (const QString id : { "home", "system-folder/preferences", "system-folder/appearance" }) {
		const QString real = vfsOpensAs(vfsPathFor(id));
		allInHome = allInHome && !real.isEmpty() && real.startsWith(root);
	}
	check(allInHome, "every writable folder is inside the user's own directories");
	check(vfsIsFonts(vfsPathFor("system-folder/fonts")) &&
		vfsAcceptsDrops(vfsPathFor("system-folder/fonts")) &&
		vfsOpensAs(vfsPathFor("system-folder/fonts")).isEmpty() &&
		vfsRealCounterpart(vfsPathFor("system-folder/fonts")).startsWith(root),
		"Fonts is a writable catalog with a user-owned file-chooser counterpart");
	check(vfsOpensAs(vfsPathFor("applications")).isEmpty() &&
		vfsOpensAs(vfsPathFor("system-folder/control-panels")).isEmpty(),
		"the generated folders are not backed by any directory to write to");

	/* Source monitoring starts at existing ancestors, then re-arms both
	 * directory and file watches after creation, edits, replacement, and
	 * directory recreation. */
	int changes = 0;
	const int watchToken = vfsOnChange([&] { ++changes; });
	const QString autostart = root + "/config/autostart";
	const QString desktopFile = autostart + "/watch.desktop";
	int previous = changes;
	QDir().mkpath(autostart);
	writeEntry(autostart, "watch.desktop", entry("Watch Created"));
	check(waitFor([&] { return changes > previous; }),
		"an initially absent autostart directory is detected when created");
	previous = changes;
	writeEntry(autostart, "watch.desktop", entry("Watch Edited"));
	check(waitFor([&] { return changes > previous; }),
		"in-place desktop-file edits notify resource watchers");
	previous = changes;
	QSaveFile replacement(desktopFile);
	check(replacement.open(QIODevice::WriteOnly) &&
		replacement.write(entry("Watch Replaced").toUtf8()) > 0 && replacement.commit(),
		"desktop-file atomic replacement succeeds");
	check(waitFor([&] { return changes > previous; }),
		"atomic desktop-file replacement notifies and rearms the watcher");
	previous = changes;
	check(QDir(autostart).removeRecursively(), "watched source directory is removed");
	check(waitFor([&] { return changes > previous; }),
		"directory removal notifies the watcher");
	previous = changes;
	QDir().mkpath(autostart);
	writeEntry(autostart, "watch.desktop", entry("Watch Recreated"));
	check(waitFor([&] { return changes > previous; }),
		"recreated source directories are detected after watcher rearming");
	vfsOffChange(watchToken);

	bool nestedRefresh = false;
	const int refreshToken = vfsOnChange([&] {
		if (!nestedRefresh) {
			nestedRefresh = true;
			vfsRefresh();
		}
	});
	vfsRefresh();
	check(nestedRefresh, "reentrant refresh requests are suppressed safely");
	vfsOffChange(refreshToken);

	/* ---- folder paths (⌘-click on a title, ⌘-↑) -------------------------- */
	{
		const QString ext = vfsPathFor("system-folder/extensions");
		check(folderChain(ext) == QStringList({ ext, vfsPathFor("system-folder"), vfsRoot() }),
			"a folder in the Macintosh view goes up through it to the startup disk");
		check(folderTitle(vfsRoot()) == vfsVolumeName() && enclosingFolder(vfsRoot()).isEmpty(),
			"the startup disk is the top, by its own name");
		const QString home = QDir::homePath();
		QDir().mkpath(home + "/Desktop/Stuff");
		check(vfsPathBacking(home) == vfsPathFor("home"), "Home stands for the home folder");
		check(folderChain(home + "/Desktop/Stuff") == QStringList({ home + "/Desktop/Stuff",
				home + "/Desktop", vfsPathFor("home"), vfsRoot() }),
			"a real folder goes up through real folders, then Home, then the startup disk");
		check(enclosingFolder(home) == vfsRoot(), "the home folder's enclosing folder is the startup disk");
		check(folderTitle(home + "/Desktop/Stuff") == "Stuff", "a real folder shows its name");
		check(enclosingFolder("/").isEmpty(), "and Unix (/) is a disk's top");
	}

	/* ---- mounted ZacOS 9 disks ------------------------------------------ */
	{
		const QString external = root + "/external-hd";
		QDir().mkpath(external + "/usr/share/applications");
		QDir().mkpath(external + "/usr/lib");
		QDir().mkpath(external + "/usr/bin");
		QDir().mkpath(external + "/usr/share/fonts");
		QDir().mkpath(external + "/etc/xdg/autostart");
		QDir().mkpath(external + "/home/alice/Documents");
		vfsSetZacosVolumes({ { external, "External HD", "0.1.70" } });
		const QString disk = vfsZacosVolumeRoot(external);
		check(!disk.isEmpty() && vfsIsZacosVolumeRoot(disk),
			"a mounted ZacOS 9 disk has its own Macintosh root");
		check(displayName(disk) == "External HD" && folderTitle(disk) == "External HD",
			"the disk is named from its own disk-name");
		check(namesIn(disk) == QStringList({ "System Folder", "Applications", "Home" }),
			"the disk root shows Applications, System Folder and Home, not Unix");
		check(!vfsCanRename(disk) && !vfsAcceptsDrops(disk) && !vfsSetLabel(disk, 2),
			"the mounted disk's view is read-only");
		const QString system = pathNamed(disk, "System Folder");
		const QString extensions = pathNamed(system, "Extensions");
		check(!system.isEmpty() && !extensions.isEmpty() &&
			vfsOpensAs(extensions) == external + "/usr/lib",
			"its System Folder maps Extensions onto that disk");
		check(vfsOpensAs(pathNamed(disk, "Applications")) == external + "/usr/share/applications" &&
			vfsOpensAs(pathNamed(disk, "Home")) == external + "/home/alice",
			"Applications and Home open real folders on that disk");
		check(enclosingFolder(disk).isEmpty() &&
			folderChain(extensions) == QStringList({ extensions, system, disk }),
			"the mounted disk is the top of its own folder chain");
		check(vfsPathBacking(external + "/usr/lib") == extensions,
			"a real folder on the disk goes back up through its Macintosh view");
		QStringList info;
		QString kind, location, source, access;
		check(vfsResourceDetails(disk, &info, &kind, &location, &source, &access) &&
			kind.contains("disk") && location == "Desktop" &&
			info.join('\n').contains("Version: 0.1.70"),
			"Get Info presents the mounted ZacOS disk and its version");
		vfsSetZacosVolumes({});
		check(vfsZacosVolumeRoot(external).isEmpty() && !vfsNode(disk),
			"unmounting removes the disk's Macintosh view");
	}

	QTextStream(stdout) << (fails ? QString("%1 failed\n").arg(fails)
		: QStringLiteral("all passed\n"));
	return fails ? 1 : 0;
}
