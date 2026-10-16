#include "filechooserportal.h"
#include "items.h"
#include "alias.h"
#include "settings.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QFileDialog>
#include <QKeyEvent>
#include <QStandardPaths>
#include <QSet>
#include <QUrl>
#include <QWidget>
#include <cstdio>

static int failures = 0;

static void check(bool condition, const char *message) {
	if (!condition) {
		std::fprintf(stderr, "FAIL: %s\n", message);
		++failures;
	}
}

int main(int argc, char **argv) {
	QTemporaryDir home;
	const QString base = QFileInfo(home.path()).canonicalFilePath();
	qputenv("HOME", QFile::encodeName(base));
	qputenv("XDG_CONFIG_HOME", QFile::encodeName(base + "/config"));
	qputenv("XDG_DATA_HOME", QFile::encodeName(base + "/data"));
	qputenv("XDG_RUNTIME_DIR", QFile::encodeName(base + "/runtime"));
	QDir().mkpath(base + "/runtime");
	QFile::setPermissions(base + "/runtime", QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
	QApplication app(argc, argv);
	QFile note(base + "/note.txt");
	check(note.open(QIODevice::WriteOnly), "create a Home file");
	note.close();
	QTemporaryDir server;
	const QString serverRoot = QFileInfo(server.path()).canonicalFilePath();
	const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
	check(QDir().mkpath(desktop) && QDir().mkpath(serverRoot + "/AFP/Projects") &&
		QDir().mkpath(serverRoot + "/SMB/Shared"), "create desktop and server-folder fixtures");
	check(QFile::link(serverRoot + "/AFP/Projects", desktop + "/AFP Projects") &&
		QFile::link(serverRoot + "/SMB/Shared", desktop + "/Windows Shared") &&
		QFile::link(desktop + "/AFP Projects", desktop + "/Second Projects") &&
		QFile::link(base + "/note.txt", desktop + "/Document alias") &&
		QFile::link(serverRoot + "/Offline", desktop + "/Offline Server"),
		"create folder, chained, document and unavailable server aliases");
	check(QFile::link(serverRoot + "/SMB/Shared", base + "/Not on Desktop"),
		"create a non-desktop alias");
	check(QDir().mkpath(serverRoot + "/Before") &&
		QFile::link(serverRoot + "/Before", desktop + "/Moved Folder") &&
		aliasRecord(desktop + "/Moved Folder", serverRoot + "/Before") &&
		QDir().rename(serverRoot + "/Before", serverRoot + "/After"),
		"create a recorded alias whose original was renamed");
	check(QFile::link(desktop + "/Loop alias", desktop + "/Loop alias"),
		"create a looping alias");
	/* Favorites: a folder of its own, and one the desktop already has. */
	const QString favorites = favoritesPath();
	check(QDir().mkpath(favorites) && QDir().mkpath(serverRoot + "/Fav") &&
		QFile::link(serverRoot + "/Fav", favorites + "/Fav Folder") &&
		QFile::link(serverRoot + "/SMB/Shared", favorites + "/Shared Again"),
		"create Favorites folder aliases");
	QElapsedTimer timer;
	timer.start();
	const QList<FileChooserLocation> locations = fileChooserLocations();
	std::fprintf(stdout, "Drive list built in %lld ms\n", static_cast<long long>(timer.elapsed()));
	check(!locations.isEmpty() && locations.front().name == "Zacintosh HD",
		"the startup disk comes first under its Finder name");
	const FileChooserLocation disk = locations.value(0);
	const QDir root(disk.path);
	for (const char *name : { "System Folder", "Applications", "Home" }) {
		check(root.exists(name), "the startup disk shows the Finder's System Folder, Applications and Home");
	}
	check(QFileInfo(root.filePath("Home")).isSymLink() &&
		QFileInfo(root.filePath("Home")).canonicalFilePath() == base,
		"Home stands for the real Home folder");
	check(QFileInfo(root.filePath("System Folder/Preferences")).canonicalFilePath() ==
		base + "/config", "System Folder's real folders stand for their backing folders");
	check(!root.exists("System Folder/Control Panels") && !root.exists("System Folder/Extensions"),
		"generated System Folder listings that are not files are left out");
	check(disk.realRoots.contains(base), "Home's real folder can be chosen");
	check(!fileChooserPathWithinDisk(disk.path, disk.path, disk.realRoots, false) &&
		!fileChooserPathWithinDisk(root.filePath("System Folder"), disk.path, disk.realRoots, false) &&
		fileChooserPathWithinDisk(root.filePath("System Folder"), disk.path, disk.realRoots, true),
		"the startup disk's own folders can be browsed but not chosen");
	check(fileChooserPathWithinDisk(root.filePath("Home/note.txt"), disk.path, disk.realRoots, false) &&
		fileChooserPathWithinDisk(root.filePath("Home/new.txt"), disk.path, disk.realRoots, false),
		"files inside Home can be opened and saved");
	check(!fileChooserPathWithinDisk(base + "/note.txt", disk.path, disk.realRoots, false) &&
		!fileChooserPathWithinDisk(root.filePath("Home/.."), disk.path, disk.realRoots, true) &&
		!fileChooserPathWithinDisk("/", disk.path, disk.realRoots, true),
		"paths outside the startup disk are refused");
	check(QFile::link("/", base + "/escape") &&
		!fileChooserPathWithinDisk(root.filePath("Home/escape/etc"), disk.path, disk.realRoots, true),
		"a link inside Home cannot escape to the Unix root");
	check(fileChooserRealPath(root.filePath("Home/note.txt")) == base + "/note.txt" &&
		fileChooserRealPath(root.filePath("Home/new.txt")) == base + "/new.txt",
		"applications receive real paths, not the chooser's links");
	QSet<QString> aliases;
	for (const FileChooserLocation &location : locations) {
		if (location.folderAlias) {
			aliases.insert(location.name);
			check(location.path == desktop + "/" + location.name ||
					location.path == favorites + "/" + location.name,
				"folder locations retain their alias's name and path");
			check(location.error.isEmpty() ==
				(location.name != "Offline Server" && location.name != "Loop alias"),
				"unavailable and looping aliases have an explicit error");
		}
	}
	check(aliases == QSet<QString>{ "AFP Projects", "Windows Shared", "Second Projects",
			"Offline Server", "Moved Folder", "Loop alias", "Fav Folder" },
		"all desktop folder aliases appear, including duplicate targets and offline servers, but not documents");
	check(QFileInfo(desktop + "/Moved Folder").canonicalFilePath() == serverRoot + "/After",
		"the chooser reuses Finder's identity-based reconnection for renamed originals");
	check(fileChooserPathWithinDisk(root.filePath("Home/Desktop/AFP Projects/new.txt"),
		disk.path, disk.realRoots, false) &&
		fileChooserPathWithinDisk(root.filePath("Home/Desktop/Windows Shared"),
			disk.path, disk.realRoots, false),
		"desktop aliases can also be followed inside the Macintosh view to external folders");
	check(fileChooserRealPath(desktop + "/AFP Projects/new.txt") == serverRoot + "/AFP/Projects/new.txt",
		"saving through a desktop server alias returns its real path");
	QFile blocked(root.filePath("stray.txt"));
	check(!blocked.open(QIODevice::WriteOnly), "the startup disk's own folders are read-only");
	const FileChooserLocation again = fileChooserStartupDisk(
		base + "/runtime/zacos9-file-chooser");
	check(again.path == disk.path && QFile::exists(base + "/note.txt") &&
		QFile::exists(base + "/config"),
		"rebuilding the startup disk removes only its links, never real files");
	QSet<QString> paths;
	for (const FileChooserLocation &location : locations) {
		std::fprintf(stdout, "Chooser drive: %s (%s)\n",
			qPrintable(location.name), qPrintable(location.path));
		check(!location.name.isEmpty() && !location.path.isEmpty(),
			"every disk location has a readable label and path");
		check(!paths.contains(location.path), "disk locations are unique");
		paths.insert(location.path);
	}
	for (const QString hidden : { "/proc", "/sys", "/dev", "/run" }) {
		check(!paths.contains(hidden), "system pseudo-filesystems are not shown as disks");
	}
	QFileDialog dialog;
	dialog.setSidebarUrls({ QUrl::fromLocalFile(QDir::homePath()) });
	prepareFileChooserDialog(dialog);
	check(dialog.property("zacos9-file-portal-dialog").toBool(),
		"portal-owned dialogs are marked for the ZacOS Qt style");
	check(dialog.sidebarUrls().isEmpty(), "standard file-dialog places are removed");
	if (QWidget *sidebar = dialog.findChild<QWidget *>("sidebar")) {
		check(!sidebar->isVisible(), "the built-in places sidebar is hidden");
	}
	const QString keyboardFolder = base + "/Keyboard";
	QDir().mkpath(keyboardFolder);
	QFile keyboardFirst(keyboardFolder + "/alpha.txt");
	check(keyboardFirst.open(QIODevice::WriteOnly), "create the first chooser keyboard fixture");
	keyboardFirst.close();
	QFile keyboardFixture(keyboardFolder + "/note.txt");
	check(keyboardFixture.open(QIODevice::WriteOnly), "create a chooser keyboard fixture");
	keyboardFixture.close();
	QFileDialog keyboardDialog;
	keyboardDialog.setOption(QFileDialog::DontUseNativeDialog, true);
	keyboardDialog.setDirectory(keyboardFolder);
	keyboardDialog.setFileMode(QFileDialog::ExistingFile);
	keyboardDialog.setNameFilter("Text files (*.txt)");
	prepareFileChooserDialog(keyboardDialog);
	keyboardDialog.show();
	QApplication::processEvents();
	if (QAbstractItemView *list = keyboardDialog.findChild<QAbstractItemView *>("listView")) {
		list->setFocus();
		QKeyEvent typeName(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a");
		QApplication::sendEvent(list, &typeName);
		QApplication::processEvents();
		check(list->currentIndex().isValid() &&
			list->currentIndex().data().toString() == "alpha.txt",
			"Open dialog supports type-to-select in the file list");
		QKeyEvent moveDown(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
		QApplication::sendEvent(list, &moveDown);
		QApplication::processEvents();
		check(list->currentIndex().isValid() &&
			list->currentIndex().data().toString() == "note.txt",
			"Open dialog arrow navigation moves to the next listed file");
	} else {
		check(false, "Open dialog exposes its keyboard-navigable file list");
	}
	keyboardDialog.close();
	check(QFile::link("/", desktop + "/Filesystem alias"),
		"create an explicit alias to the filesystem root");
	const auto rootLocations = fileChooserLocations();
	bool foundRoot = false;
	for (const auto &location : rootLocations) {
		foundRoot |= location.name == "Filesystem alias" && location.folderAlias && location.error.isEmpty();
	}
	check(foundRoot && fileChooserPathWithinDrive("/etc", "/"),
		"an explicit desktop folder alias may target any directory, including the filesystem root");
	const QString gvfsShare = base + "/runtime/gvfs/smb-share:server=windows,share=Team Files,user=adam";
	check(QDir().mkpath(gvfsShare), "create a GVFS SMB share fixture");
	const QByteArray mounts =
		"316 31 0:53 / /missing/adam's\\040home rw shared:281 - fuse.afp afp://host/home rw\n"
		"317 31 0:54 / /missing/Windows\\040Disk rw - cifs //host/share rw\n"
		"318 31 0:55 / /missing/SMB3 rw - smb3 //host/share rw\n"
		"319 31 0:56 / /missing/adam's\\040home rw - fuse.afp afp://host/home rw\n"
		"320 31 0:57 / /run/user/1001/gvfs rw - fuse.gvfsd-fuse gvfsd-fuse rw\n"
		"321 31 0:58 / /missing/Local rw - ext4 /dev/sda rw\n"
		"invalid mount record\n";
	const auto networkLocations = fileChooserNetworkLocations(mounts, base + "/runtime");
	check(networkLocations.size() == 4,
		"network discovery covers AFP, kernel SMB and GVFS without duplicates or unrelated disks");
	check(networkLocations.value(0).name == "adam's home" &&
		networkLocations.value(0).path == "/missing/adam's home",
		"AFP names and paths preserve spaces and apostrophes even when the mount is inaccessible");
	check(networkLocations.value(1).name == "Windows Disk" &&
		networkLocations.value(2).name == "SMB3" &&
		networkLocations.value(3).name == "Team Files" &&
		networkLocations.value(3).path == gvfsShare,
		"kernel and GVFS Windows shares use readable names and local paths");
	const auto refreshed = fileChooserLocations();
	bool foundSmb = false;
	for (const auto &location : refreshed) {
		foundSmb |= location.path == gvfsShare && location.name == "Team Files";
	}
	check(foundSmb, "the actual chooser includes GVFS shares omitted by GIO local-path enumeration");

	/* General Controls' Documents. */
	check(fileChooserStartFolder("org.example.Writer").isEmpty(),
		"by default (folder set by the application) dialogs start from the list of disks");
	pl_setting_set("documents-folder", "documents");
	const QString documents = fileChooserStartFolder("org.example.Writer");
	check(documents == base + "/Documents" && QFileInfo(documents).isDir(),
		"Documents folder: dialogs start there, and it is made if missing");
	QString browse;
	const FileChooserLocation startup = fileChooserLocationFor(refreshed, documents, &browse);
	check(!startup.realRoots.isEmpty() && browse.startsWith(startup.path + "/") &&
		QFileInfo(browse).canonicalFilePath() == documents,
		"a folder in Home opens on the startup disk, through its links");
	check(fileChooserLocationFor(refreshed, "/proc", &browse).path.isEmpty(),
		"a folder no location reaches has none");
	check(fileChooserDroppedFolder({ QUrl::fromLocalFile(serverRoot + "/AFP/Projects") },
		serverRoot + "/AFP", {}) == serverRoot + "/AFP/Projects" &&
		fileChooserDroppedFolder({ QUrl::fromLocalFile(base) }, serverRoot + "/AFP", {}).isEmpty() &&
		fileChooserDroppedFolder({ QUrl::fromLocalFile(base + "/note.txt") },
			serverRoot + "/AFP", {}).isEmpty() &&
		fileChooserDroppedFolder({ QUrl("https://example.invalid/folder") },
			serverRoot + "/AFP", {}).isEmpty(),
		"dropping a local folder navigates only inside the selected disk");
	pl_setting_set("documents-folder", "last");
	check(fileChooserStartFolder("org.example.Writer").isEmpty(), "nothing used yet: the list of disks");
	fileChooserRememberFolder("org.example.Writer", serverRoot + "/AFP/Projects");
	fileChooserRememberFolder("org.example.Paint", base);
	fileChooserRememberFolder("org.example.Paint", base + "/gone");
	check(fileChooserStartFolder("org.example.Writer") == serverRoot + "/AFP/Projects" &&
		fileChooserStartFolder("org.example.Paint") == base &&
		fileChooserStartFolder("org.example.Other").isEmpty(),
		"Last folder used: each program starts where it last was");
	fileChooserRememberRecentFolder(serverRoot + "/AFP/Projects");
	fileChooserRememberRecentFolder(base);
	fileChooserRememberRecentFolder(serverRoot + "/AFP/Projects");
	check(fileChooserRecentFolders() == QStringList({ serverRoot + "/AFP/Projects", base }),
		"recent folders are ordered newest-first and repeated locations move to the front");
	const FileChooserLocation afpLocation{ "AFP", serverRoot + "/AFP", false, {} };
	check(fileChooserRecentFolders(afpLocation) ==
		QStringList({ serverRoot + "/AFP/Projects" }),
		"Recent Folders only offers destinations inside the selected disk");
	fileChooserRememberRecentFolder(base + "/missing");
	check(fileChooserRecentFolders() == QStringList({ serverRoot + "/AFP/Projects", base }),
		"unavailable folders are not added to Recent Folders");
	QStringList expectedRecent;
	for (int i = 0; i < 12; i++) {
		const QString recent = base + QStringLiteral("/recent-%1").arg(i);
		QDir().mkpath(recent);
		fileChooserRememberRecentFolder(recent);
		expectedRecent.prepend(recent);
	}
	expectedRecent = expectedRecent.mid(0, 10);
	check(fileChooserRecentFolders() == expectedRecent,
		"Recent Folders keeps only the ten newest locations");
	const QString newlineFolder = base + "/recent\nfolder";
	QDir().mkpath(newlineFolder);
	fileChooserRememberRecentFolder(newlineFolder);
	check(fileChooserRecentFolders().first() == newlineFolder,
		"recent-folder persistence safely round-trips unusual legal path characters");
	pl_setting_set("documents-folder", nullptr);
	return failures ? 1 : 0;
}
