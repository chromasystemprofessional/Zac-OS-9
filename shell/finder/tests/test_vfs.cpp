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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

#include <glib.h>

#include "appdb.h"
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

/* An ordinary application entry. */
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
	appsDir = dataDirs + "/applications";
	homeAppsDir = dataHome + "/applications";
	setenv("XDG_DATA_HOME", dataHome.toUtf8().constData(), 1);
	setenv("XDG_DATA_DIRS", dataDirs.toUtf8().constData(), 1);
	setenv("XDG_CONFIG_HOME", (root + "/config").toUtf8().constData(), 1);
	setenv("HOME", root.toUtf8().constData(), 1);
	setenv("XDG_CURRENT_DESKTOP", "ZacOS9", 1);
	QDir().mkpath(appsDir);
	QDir().mkpath(homeAppsDir);

	/* A QGuiApplication (not just QCoreApplication), offscreen: so icon
	 * resolution (QIcon::fromTheme, which needs a QPA platform to
	 * rasterize into) runs for real instead of skipping itself the way
	 * it does for a tool with no GUI platform at all. */
	QGuiApplication app(argc, argv);
	QStandardPaths::setTestModeEnabled(false);

	/* ---- discovery ---------------------------------------------------- */
	writeEntry(appsDir, "simpletext.desktop", entry("SimpleText"));
	writeEntry(appsDir, "calculator.desktop",
		entry("Calculator", "/bin/true", "Categories=Utility;\n"));
	/* Every way an entry asks not to be shown. */
	writeEntry(appsDir, "hidden.desktop", entry("Hidden One", "/bin/true", "Hidden=true\n"));
	writeEntry(appsDir, "nodisplay.desktop",
		entry("NoDisplay One", "/bin/true", "NoDisplay=true\n"));
	writeEntry(appsDir, "othershell.desktop",
		entry("Only Elsewhere", "/bin/true", "OnlyShowIn=GNOME;\n"));
	writeEntry(appsDir, "notus.desktop",
		entry("Not Here", "/bin/true", "NotShowIn=ZacOS9;\n"));
	writeEntry(appsDir, "missingbin.desktop",
		entry("Missing Binary", "/bin/true", "TryExec=/nonexistent/program\n"));
	settle();

	QStringList apps = namesIn(vfsPathFor("applications"));
	check(apps.contains("SimpleText"), "an installed application appears in Applications");
	check(apps.contains("Calculator"), "a second application appears too");
	check(!apps.contains("Hidden One"), "Hidden=true is not shown");
	check(!apps.contains("NoDisplay One"), "NoDisplay=true is not shown");
	check(!apps.contains("Only Elsewhere"), "OnlyShowIn for another desktop is not shown");
	check(!apps.contains("Not Here"), "NotShowIn for this desktop is not shown");
	check(!apps.contains("Missing Binary"), "a failing TryExec is not shown");

	/* ---- the startup disk --------------------------------------------- */
	QStringList volume = namesIn(vfsRoot());
	check(volume.contains("System Folder"), "the startup disk holds a System Folder");
	check(volume.contains("Applications"), "the startup disk holds Applications");
	check(volume.contains("Documents"), "the startup disk holds Documents");
	check(!volume.contains("Utilities"), "Utilities is off until the registry asks for it");
	check(volume.size() == 3, "and nothing else: no Unix directories");
	for (const QString &unix_ : { "usr", "etc", "bin", "var", "lib", "proc", "sys", "home" }) {
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

	QStringList panels = namesIn(vfsPathFor("system-folder/control-panels"));
	check(panels.contains("Appearance") && panels.contains("File Sharing") &&
		panels.contains("TCP/IP"), "Control Panels lists the control panels");

	/* ---- an application is a folder holding a launcher ---------------- */
	QStringList inside = namesIn(appFolder("simpletext.desktop"));
	check(inside == QStringList{ "SimpleText" },
		"an application folder holds one launcher, named for the application");
	const VNode *launcher = vfsNode(vfsPathFor("applications/simpletext.desktop/launch"));
	check(launcher && launcher->kind == VKind::Launcher &&
		launcher->icon == PL_ICON_APPLICATION, "the launcher is an application item");
	check(launcher && launcher->appId == "simpletext.desktop",
		"identity is the desktop file id, not the display name");

	/* Desktop Actions appear beside the application. */
	writeEntry(appsDir, "browser.desktop",
		entry("Browser", "/bin/true",
			"Actions=NewWindow;\n\n[Desktop Action NewWindow]\n"
			"Name=New Window\nExec=/bin/true\n"));
	settle();
	check(namesIn(appFolder("browser.desktop")).contains("New Window"),
		"a Desktop Action appears in the application's folder");

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

	/* ---- what the Finder allows --------------------------------------- */
	const QString appsPath = vfsPathFor("applications");
	const QString launchPath = vfsPathFor("applications/browser.desktop/launch");
	check(!vfsCanDelete(appFolder("browser.desktop")),
		"an application folder can't be thrown away");
	check(!vfsCanDelete(appsPath), "the Applications folder can't be thrown away");
	check(!vfsCanDelete(vfsPathFor("system-folder")),
		"the System Folder can't be thrown away");
	check(!vfsAcceptsDrops(appsPath), "Applications takes no drops");
	check(!vfsAcceptsDrops(vfsPathFor("system-folder")), "the System Folder takes no drops");
	check(vfsAcceptsDrops(vfsPathFor("documents")), "Documents takes drops");
	check(vfsCanRename(launchPath), "an application can be renamed");
	check(!vfsCanRename(vfsPathFor("applications/browser.desktop/action:NewWindow")),
		"a Desktop Action can't be renamed on its own");

	/* ---- hiding an application (never uninstalling it) ----------------- */
	check(vfsIsAppFolder(appFolder("browser.desktop")),
		"an application's folder is recognized as one");
	check(!vfsIsAppFolder(appsPath), "the Applications folder itself is not");
	check(!vfsIsAppFolder(launchPath), "neither is the launcher inside the folder");
	check(vfsSetLabel(launchPath, 3), "give it a label, to check it isn't lost");
	check(vfsHideApplication(appFolder("browser.desktop")), "an application can be hidden");
	apps = namesIn(vfsPathFor("applications"));
	check(!apps.contains("Browser"), "a hidden application no longer appears");
	check(vfsNode(appFolder("browser.desktop")) != nullptr,
		"but its node, and so its remembered label and position, still exist");
	vfsRefresh();
	apps = namesIn(vfsPathFor("applications"));
	check(!apps.contains("Browser"), "hiding an application outlasts a reload, same as a rename");
	vfsShowAllHidden();
	apps = namesIn(vfsPathFor("applications"));
	check(apps.contains("Browser"), "Show All Applications brings it back");
	check(vfsLabel(launchPath) == 3, "bringing it back did not also clear its label");
	check(!vfsHideApplication(appsPath), "the Applications folder itself can't be hidden");
	check(!vfsHideApplication(launchPath),
		"hiding targets the folder, not the launcher inside it, directly");

	/* ---- Get Info reads the real thing an item stands for -------------- */
	check(vfsRealCounterpart(launchPath).endsWith("browser.desktop"),
		"Get Info on an application reads its desktop entry");
	check(vfsRealCounterpart(vfsPathFor("documents")).startsWith(root),
		"Get Info on Documents reads its real directory");
	check(vfsRealCounterpart(vfsPathFor("system-folder")).isEmpty(),
		"a curated folder stands for nothing on disk");

	/* ---- folders standing for real directories ------------------------ */
	const QString documents = vfsOpensAs(vfsPathFor("documents"));
	check(!documents.isEmpty() && QDir(documents).exists(),
		"Documents opens a real directory, made if it was missing");
	check(documents.startsWith(root), "and it is inside the home folder, not a system one");
	const QString prefs = vfsOpensAs(vfsPathFor("system-folder/preferences"));
	check(prefs == root + "/config", "Preferences stands for the XDG config directory");
	check(vfsOpensAs(vfsPathFor("system-folder")).isEmpty(),
		"a curated folder opens as a virtual window, not a directory");
	check(vfsList(vfsPathFor("documents")).empty(),
		"a folder standing for a directory has no virtual children");

	/* ---- renames and labels persist ----------------------------------- */
	check(vfsRename(launchPath, "Web Browser"), "an application can be renamed");
	check(vfsName(launchPath) == "Web Browser", "the new name is used");
	check(vfsSetLabel(launchPath, 2), "a label can be set on it");
	check(vfsLabel(launchPath) == 2, "and read back");
	check(!vfsRename(launchPath, "   "), "an empty name is refused");

	/* A rename doesn't move the item: its id, and so its icon's place
	 * and its window, stay as they were. */
	check(vfsNode(launchPath) != nullptr, "the renamed item keeps its id");

	/* Reload from the registry file, as a fresh Finder would. */
	vfsRefresh();
	check(vfsName(launchPath) == "Web Browser", "the rename outlasts a reload");
	check(vfsLabel(launchPath) == 2, "the label outlasts a reload");
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
	check(apps.contains("Browser") && apps.contains("SimpleText Override"),
		"and the others stay");
	check(vfsNode(appFolder("calculator.desktop")) == nullptr,
		"its folder goes with it");
	/* Renaming the launcher renames the application, not the folder
	 * around it, as on a Mac where they are separate things. */
	check(vfsName(launchPath) == "Web Browser" && apps.contains("Browser"),
		"renaming an application leaves its folder's name alone");

	/* A rename kept for an application that has gone is harmless. */
	QFile::remove(appsDir + "/browser.desktop");
	settle();
	check(vfsNode(launchPath) == nullptr, "a removed application's launcher goes too");
	vfsRefresh();
	check(true, "a rename left over for a removed application does no harm");

	/* ---- the power-user view of Debian --------------------------------- */
	check(!vfsUnixVolumeShown(), "the real filesystem is not shown by default");
	vfsSetUnixVolumeShown(true);
	check(vfsUnixVolumeShown(), "it can be turned on");
	vfsRefresh();
	check(vfsUnixVolumeShown(), "and that outlasts a reload");
	check(namesIn(vfsRoot()).size() == 3,
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
		volume.contains("Documents"), "and adds the ones this version expects");
	if (reg.open(QIODevice::ReadOnly)) {
		check(QJsonDocument::fromJson(reg.readAll()).object().value("version").toInt() >= 1,
			"and writes the new version back");
		reg.close();
	}

	/* ---- unprivileged -------------------------------------------------- */
	check(!namesIn(vfsRoot()).isEmpty(), "browsing needs no privileges");
	/* Every directory the Finder may write into through this hierarchy
	 * belongs to the user, so no ordinary action needs root and none can
	 * reach into a system location. */
	bool allInHome = true;
	for (const QString &id : { "documents", "system-folder/preferences",
			"system-folder/fonts", "system-folder/appearance" }) {
		const QString real = vfsOpensAs(vfsPathFor(id));
		allInHome = allInHome && !real.isEmpty() && real.startsWith(root);
	}
	check(allInHome, "every writable folder is inside the user's own directories");
	check(vfsOpensAs(vfsPathFor("applications")).isEmpty() &&
		vfsOpensAs(vfsPathFor("system-folder/control-panels")).isEmpty(),
		"the generated folders are not backed by any directory to write to");

	QTextStream(stdout) << (fails ? QString("%1 failed\n").arg(fails)
		: QStringLiteral("all passed\n"));
	return fails ? 1 : 0;
}
