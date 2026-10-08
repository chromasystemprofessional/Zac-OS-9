#include <QApplication>
#include <QDir>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemWatcher>
#include <QImage>
#include <QLineEdit>
#include <QMimeData>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include "alert.h"
#include "appdb.h"
#include "apptrash.h"
#include "fileops.h"
#include "finder.h"
#include "folderstate.h"
#include "items.h"
#include "labeleditor.h"
#include "localvolumes.h"
#include "zacos9style.h"
#include "vfs.h"

#define private public
#define protected public
#include "desktop.h"
#include "folderwindow.h"
#undef protected
#undef private

static int failures;

std::vector<LocalVolume> localVolumes() { return {}; }
QString localVolumeUsbDevice(const QString &) { return {}; }
void localVolumesOnChange(std::function<void()>) {}
void localVolumesOnError(std::function<void(const QString &)>) {}
void localVolumeMountMacImage(const QString &,
		std::function<void(const QString &, const QString &)> completed) {
	completed({}, "Disk image mounting is unavailable in the desktop fixture.");
}
void localVolumesMountAll() {}
void localVolumesOnMountFailed(std::function<void(const QString &)>) {}
void localVolumeMountDevice(const QString &) {}
void localVolumeInhibitMount(const QString &, bool) {}
bool localVolumeEject(const LocalVolume &) { return false; }
bool localVolumePathShown(const QString &) { return false; }

static void check(bool ok, const char *message) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << message << "\n";
	failures += !ok;
}

static bool write(const QString &path) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write("saved data") == 10;
}

static void settle() {
	QElapsedTimer timer;
	timer.start();
	while (timer.elapsed() < 100) {
		QApplication::processEvents(QEventLoop::AllEvents, 10);
	}
}

static bool waitFor(const std::function<bool()> &condition, int timeoutMs = 3000) {
	QElapsedTimer timer;
	timer.start();
	while (!condition() && timer.elapsed() < timeoutMs) {
		QApplication::processEvents(QEventLoop::AllEvents, 10);
	}
	return condition();
}

static bool hasFile(Desktop &desktop, const QString &name) {
	for (const auto &item : desktop.m_files) {
		if (item->name == name) {
			return true;
		}
	}
	return false;
}

static void closeAlert() {
	QTimer::singleShot(0, [] {
		for (QWidget *widget : QApplication::topLevelWidgets()) {
			if (auto *alert = dynamic_cast<Alert *>(widget)) {
				alert->accept();
			}
		}
	});
}

int main(int argc, char **argv) {
	if (argc == 2 && QByteArray(argv[1]) == "--check-app-trash") {
		QCoreApplication app(argc, argv);
		QString error;
		const auto entries = appTrashEntries(&error);
		return error.isEmpty() && entries.size() == 1 &&
			entries.front().id == "com.example.TrashTest.desktop" ? 0 : 1;
	}
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
	qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
	qputenv("XDG_DATA_DIRS", (home.path() + "/data").toUtf8());
	qputenv("XDG_RUNTIME_DIR", home.path().toUtf8());
	QDir().mkpath(home.path() + "/config");
	QFile userDirs(home.path() + "/config/user-dirs.dirs");
	check(userDirs.open(QIODevice::WriteOnly), "create test XDG desktop mapping");
	userDirs.write("XDG_DESKTOP_DIR=\"$HOME/Work Desktop\"\n");
	userDirs.close();
	QDir().mkpath(home.path() + "/data/applications");
	QImage appIcon(32, 32, QImage::Format_ARGB32);
	appIcon.fill(0xff336699);
	const QString appIconPath = home.path() + "/desktop-test.png";
	check(appIcon.save(appIconPath), "create isolated application icon");
	QFile launcher(home.path() + "/data/applications/desktop-test.desktop");
	check(launcher.open(QIODevice::WriteOnly), "create isolated application launcher");
	launcher.write(("[Desktop Entry]\nType=Application\nName=Desktop Test\nExec=true\nIcon=" +
		appIconPath + "\n").toUtf8());
	launcher.close();
	QApplication app(argc, argv);
	Item regularDisk;
	regularDisk.kind = PL_ICON_DISK;
	Item usbDisk;
	usbDisk.kind = PL_ICON_DISK;
	usbDisk.isUsbVolume = true;
	Pixels regularDiskPixels(PL_ICON_LARGE, PL_ICON_LARGE);
	Pixels usbDiskPixels(PL_ICON_LARGE, PL_ICON_LARGE);
	paintIcon(&regularDiskPixels.c, regularDisk, 0, 0, PL_ICON_LARGE, false);
	paintIcon(&usbDiskPixels.c, usbDisk, 0, 0, PL_ICON_LARGE, false);
	bool usbBadgeDiffers = false;
	bool diskRemainsVisible = false;
	for (int y = 0; y < PL_ICON_LARGE; y++) {
		for (int x = 0; x < PL_ICON_LARGE; x++) {
			const bool same = regularDiskPixels.img.pixel(x, y) == usbDiskPixels.img.pixel(x, y);
			if (x >= PL_ICON_LARGE / 2 && y >= PL_ICON_LARGE / 2) {
				usbBadgeDiffers |= !same;
			} else {
				diskRemainsVisible |= same;
			}
		}
	}
	check(usbBadgeDiffers && diskRemainsVisible,
		"USB volume icon overlays a USB badge on the drive without replacing it");
	Pixels smallUsb(PL_ICON_SMALL, PL_ICON_SMALL);
	Pixels expectedSmallUsb(PL_ICON_SMALL, PL_ICON_SMALL);
	Pixels usbBadge(PL_ICON_SMALL, PL_ICON_SMALL);
	paintIcon(&smallUsb.c, usbDisk, 0, 0, PL_ICON_SMALL, false);
	pl_icon_paint(&expectedSmallUsb.c, 0, 0, PL_ICON_DISK, PL_ICON_SMALL, false);
	pl_icon_paint(&usbBadge.c, 0, 0, PL_ICON_EXT_USB, PL_ICON_SMALL, false);
	const QImage smallBadge = usbBadge.img.scaled(8, 8,
		Qt::IgnoreAspectRatio, Qt::FastTransformation);
	pl_image(&expectedSmallUsb.c, 8, 8,
		reinterpret_cast<const uint32_t *>(smallBadge.constBits()), 8, 8);
	check(smallUsb.img == expectedSmallUsb.img,
		"small USB drive icon uses the complete scaled USB badge");
	app.setStyle(new Zacos9Style);

	Desktop desktop;
	desktop.resize(1024, 768);
	desktop.show();
	Finder::instance().start(&desktop);
	settle();
	const QString path = desktop.folderPath();
	check(path == home.path() + "/Work Desktop", "Finder uses the configured real Desktop");
	check(write(path + "/External file"), "application saves a file directly to Desktop");
	check(QDir().mkdir(path + "/External folder"), "application creates a Desktop folder");
	settle();
	check(hasFile(desktop, "External file") && hasFile(desktop, "External folder"),
		"filesystem watcher displays external Desktop files and folders");
	check(desktop.m_disk && desktop.m_trash &&
		desktop.m_disk->isVirtual && !desktop.m_trash->path.isEmpty(),
		"startup disk and Trash remain separate from real Desktop files");
	Finder::instance().command("new-folder");
	check(hasFile(desktop, "untitled folder"), "New Folder works on Desktop");
	desktop.selectByName("External folder");
	Finder::instance().command("make-alias");
	check(QFileInfo(path + "/External folder alias").isSymLink(),
		"Make Alias creates a navigable filesystem folder link on Desktop");
	auto alias = makeItem(QFileInfo(path + "/External folder alias"));
	check(alias->isAlias && alias->isDir && alias->kind == PL_ICON_FOLDER,
		"folder aliases keep folder navigation and alias appearance");
	for (const QString &name : { QStringLiteral("Windows.exe"),
			QStringLiteral("Installer.msi"), QStringLiteral("Disk.dmg"),
			QStringLiteral("Document.txt"), QStringLiteral("Classic app") }) {
		const QString original = path + "/" + name;
		check(write(original), "create alias icon target");
		if (name == "Classic app") {
			QDir().mkpath(path + "/.finf");
			QFile finderInfo(path + "/.finf/" + name);
			check(finderInfo.open(QIODevice::WriteOnly) && finderInfo.write("APPL") == 4,
				"create classic application Finder metadata");
		}
		const QString aliasName = createAlias(original, path, name);
		auto originalItem = makeItem(QFileInfo(original));
		auto linkedItem = makeItem(QFileInfo(path + "/" + aliasName));
		check(!aliasName.isEmpty() && linkedItem->isAlias &&
			linkedItem->name == aliasName && linkedItem->path != original &&
			linkedItem->nameFont() == PL_FONT_VIEWS_ITALIC &&
			linkedItem->iconKind() == originalItem->iconKind() &&
			iconKindFor(linkedItem->path) == iconKindFor(original),
			"alias inherits target icon without losing alias identity or italic label");
		const QString chainedName = createAlias(linkedItem->path, path, aliasName);
		check(!chainedName.isEmpty() &&
			makeItem(QFileInfo(path + "/" + chainedName))->iconKind() == originalItem->iconKind(),
			"alias chains inherit the final target icon");
	}
	qputenv("ZACOS9_SHARING_CONF", (home.path() + "/config").toUtf8());
	QFile sharedFolders(home.path() + "/config/shared-folders");
	check(sharedFolders.open(QIODevice::WriteOnly) &&
		sharedFolders.write(("External folder\t" + path + "/External folder\n").toUtf8()) > 0,
		"create isolated shared-folder configuration");
	sharedFolders.close();
	check(makeItem(QFileInfo(alias->path))->iconKind() == PL_ICON_SHARED_FOLDER &&
		makeItem(QFileInfo(alias->path))->iconKind() ==
			makeItem(QFileInfo(path + "/External folder"))->iconKind(),
		"shared-folder alias inherits the original shared-folder icon");
	Finder::instance().openItem(alias.get());
	check(waitFor([&] { return FolderWindow::isOpen(path + "/External folder"); }),
		"opening folder alias opens its real contents");
	const auto copied = transferItems({alias->path}, path, true);
	check(!copied.isEmpty() && QFileInfo(alias->path + " copy").isSymLink(),
		"Option-copy preserves a folder alias rather than copying its target");
	check(QFileInfo(alias->path + " copy").symLinkTarget() == path + "/External folder",
		"copied alias still points to original folder");

	QFileDialog dialog(nullptr, "Save");
	dialog.setOption(QFileDialog::DontUseNativeDialog);
	dialog.setAcceptMode(QFileDialog::AcceptSave);
	const QString privateDisk = home.path() + "/data/zacos9/Zacintosh HD";
	QDir().mkpath(privateDisk + "/System Folder");
	dialog.setSidebarUrls({QUrl::fromLocalFile(home.path())});
	dialog.show();
	settle();
	check(dialog.sidebarUrls().contains(QUrl::fromLocalFile(path)) &&
		dialog.sidebarUrls().contains(QUrl::fromLocalFile(privateDisk)) &&
		dialog.sidebarUrls().contains(QUrl::fromLocalFile(home.path())) &&
		dialog.sidebarUrls().contains(QUrl::fromLocalFile("/")),
		"Qt save dialog adds Desktop, Zacintosh HD and mounted disks without replacing places");
	check(QDir::cleanPath(dialog.directory().absolutePath()) == QDir::cleanPath(path),
		"Qt save dialog defaults to the XDG Desktop");
	dialog.setDirectory(path);
	check(dialog.directory().absolutePath() == path, "save chooser navigates to Desktop");
	dialog.setDirectory(alias->path);
	settle();
	auto *filename = dialog.findChild<QLineEdit *>("fileNameEdit");
	check(filename != nullptr, "save dialog offers a filename field");
	if (filename) {
		filename->setText("From application");
	}
	check(dialog.selectedFiles().size() == 1 && write(dialog.selectedFiles().front()) &&
		QFile::exists(path + "/External folder/From application"),
		"application save chooser follows Desktop folder alias to its target");
	dialog.hide();
	QFileDialog explicitDialog(nullptr, "Explicit save location");
	explicitDialog.setOption(QFileDialog::DontUseNativeDialog);
	explicitDialog.setAcceptMode(QFileDialog::AcceptSave);
	const QString explicitFolder = path + "/External folder";
	explicitDialog.setDirectory(explicitFolder);
	explicitDialog.show();
	settle();
	check(QDir::cleanPath(explicitDialog.directory().absolutePath()) ==
		QDir::cleanPath(explicitFolder),
		"Qt save dialog preserves an application-selected non-default directory");
	explicitDialog.hide();

	const QString backed = vfsNewFolder();
	auto virtualItem = makeItem(QFileInfo(path + "/External folder"));
	virtualItem->name = "Folder shortcut";
	virtualItem->path = backed;
	virtualItem->isVirtual = true;
	std::unique_ptr<QMimeData> virtualMime(itemDragMime({virtualItem.get()}));
	check(virtualMime->hasFormat(ALIAS_ITEMS_MIME) && !virtualMime->hasUrls(),
		"virtual drag exposes alias payload, never movable package paths");
	QDropEvent virtualDrop(QPointF(10, 400), Qt::MoveAction | Qt::LinkAction,
		virtualMime.get(), Qt::LeftButton, Qt::NoModifier);
	desktop.dropEvent(&virtualDrop);
	check(virtualDrop.isAccepted() && virtualDrop.dropAction() == Qt::LinkAction &&
		QFileInfo(path + "/Folder shortcut alias").isSymLink(),
		"virtual backed/app drag onto Desktop creates alias, never moves original");
	bool appAlias = false;
	for (auto &item : vfsList(vfsRoot() + "applications")) {
		if (vfsRealCounterpart(item->path) == launcher.fileName()) {
			std::unique_ptr<QMimeData> appMime(itemDragMime({item.get()}));
			QDropEvent appDrop(QPointF(10, 400), Qt::MoveAction | Qt::LinkAction,
				appMime.get(), Qt::LeftButton, Qt::NoModifier);
			desktop.dropEvent(&appDrop);
			const QString shortcut = path + "/" + item->name + " alias";
			auto shortcutItem = makeItem(QFileInfo(shortcut));
			appAlias = appDrop.isAccepted() && QFileInfo(shortcut).symLinkTarget() == launcher.fileName()
				&& shortcutItem->kind == PL_ICON_APPLICATION &&
				QFileInfo::exists(launcher.fileName());
			check(!item->customIcon32.empty() && !item->customIcon16.empty() &&
				shortcutItem->customIcon32 == item->customIcon32 &&
				shortcutItem->customIcon16 == item->customIcon16,
				"application alias inherits original large and small custom icons");
			const QString chainedName = createAlias(shortcut, path, "Application chain");
			auto chained = makeItem(QFileInfo(path + "/" + chainedName));
			check(!chainedName.isEmpty() && chained->isAlias &&
				chained->kind == PL_ICON_APPLICATION &&
				chained->customIcon32 == item->customIcon32 &&
				chained->customIcon16 == item->customIcon16,
				"application alias chain inherits final target custom icons");
		}
	}
	check(appAlias, "dragging actual Applications entry creates a launchable alias without moving launcher");
	std::unique_ptr<QMimeData> fixedMime(itemDragMime({desktop.m_disk.get()}));
	check(fixedMime->hasFormat(ICON_MOVE_MIME) && !fixedMime->hasFormat(ALIAS_ITEMS_MIME),
		"startup disk stays fixed; synthetic system nodes never become filesystem links");

	QMimeData fileMime;
	fileMime.setUrls({QUrl::fromLocalFile(path + "/External file")});
	QDropEvent linkDrop(QPointF(10, 400), Qt::MoveAction | Qt::LinkAction, &fileMime,
		Qt::LeftButton, Qt::ControlModifier | Qt::AltModifier);
	dropItems(&linkDrop, nullptr, path);
	check(linkDrop.isAccepted() && QFileInfo(path + "/External file alias").isSymLink() &&
		QFile::exists(path + "/External file"), "Command-Option drag makes alias without moving file");
	check(!transferItems({path + "/External file"}, alias->path, false).isEmpty() &&
		QFile::exists(path + "/External folder/External file"), "drop into folder alias writes to target");
	closeAlert();
	check(transferItems({path + "/External folder"}, alias->path, true).isEmpty(),
		"copy into one's own folder alias is rejected before recursion");
	check(QFile::link(path + "/missing", path + "/External file alias 2"),
		"create dangling alias collision");
	check(createAlias(path + "/External folder", path, "External file") == "External file alias 3",
		"alias naming also avoids dangling links");
	auto unavailable = makeItem(QFileInfo(path + "/External file alias 2"));
	check(unavailable->isAlias && unavailable->kind == PL_ICON_DOCUMENT,
		"unavailable alias retains alias identity with a generic icon");
	check(QFile::link(path + "/Loop alias", path + "/Loop alias"),
		"create looping alias fixture");
	auto loop = makeItem(QFileInfo(path + "/Loop alias"));
	check(loop->isAlias && loop->kind == PL_ICON_DOCUMENT,
		"looping alias resolves safely to a generic icon");
	bool missingAliasPrompt = false;
	QTimer dismissMissingAlias;
	dismissMissingAlias.setInterval(10);
	QObject::connect(&dismissMissingAlias, &QTimer::timeout, [&] {
		if (auto *alert = dynamic_cast<Alert *>(QApplication::activeModalWidget())) {
			missingAliasPrompt = true;
			alert->reject();
		}
	});
	dismissMissingAlias.start();
	Finder::instance().openItem(unavailable.get());
	const bool dismissed = waitFor([&] {
		return missingAliasPrompt && !QApplication::activeModalWidget();
	});
	dismissMissingAlias.stop();
	check(dismissed && !QFileInfo::exists(path + "/missing") &&
		!FolderWindow::isOpen(unavailable->path),
		"opening missing alias reports failure without creating a replacement target");
	closeAlert();
	QDropEvent unsupportedLink(QPointF(10, 400), Qt::MoveAction, &fileMime, Qt::LeftButton,
		Qt::ControlModifier | Qt::AltModifier);
	dropItems(&unsupportedLink, nullptr, path);
	check(!unsupportedLink.isAccepted(),
		"source without link support is never told to delete its original after alias creation");

	const QString toTrash = path + "/External folder alias copy";
	QMimeData trashMime;
	trashMime.setUrls({QUrl::fromLocalFile(toTrash)});
	QDropEvent trashDrop(QPointF(0, 0), Qt::MoveAction, &trashMime, Qt::LeftButton, Qt::NoModifier);
	dropItems(&trashDrop, desktop.m_trash.get(), path);
	check(trashDrop.isAccepted() && !QFileInfo(toTrash).isSymLink() &&
		QFile::exists(path + "/External folder/From application"),
		"trashing folder alias removes link without deleting target or saved contents");

	const QString apps = home.path() + "/data/applications";
	auto fixture = [&](const QString &name, const QByteArray &data, bool executable = false) {
		QFile file(name);
		check(file.open(QIODevice::WriteOnly), "open uninstall fixture");
		check(file.write(data) == data.size(), "write uninstall fixture");
		file.close();
		if (executable) {
			check(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner),
				"make mock uninstall command executable");
		}
	};
	fixture(apps + "/com.example.TrashTest.desktop",
		"[Desktop Entry]\nType=Application\nName=Trash Test\nExec=/bin/true\n"
		"X-Flatpak=com.example.TrashTest\n");
	fixture(apps + "/unowned.desktop",
		"[Desktop Entry]\nType=Application\nName=Unowned\nExec=/bin/sh\nX-Flatpak=invalid\n");
	QDir().mkpath(home.path() + "/commands");
	fixture(home.path() + "/commands/flatpak", R"SH(#!/bin/sh
echo "$*" >> "$TEST_TRASH_ROOT/calls"
case "$2" in
list) [ -f "$TEST_TRASH_ROOT/installed" ] && echo com.example.TrashTest; exit 0 ;;
uninstall)
 if [ -f "$TEST_TRASH_ROOT/fail" ]; then echo "Mock uninstall failed" >&2; exit 1; fi
 if [ -f "$TEST_TRASH_ROOT/leave-installed" ]; then exit 0; fi
 rm "$TEST_TRASH_ROOT/installed"
 rm "$TEST_TRASH_ROOT/data/applications/com.example.TrashTest.desktop"
 ;;
*) exit 2 ;;
esac
)SH", true);
	fixture(home.path() + "/commands/apt-helper", R"SH(#!/bin/sh
echo "$*" >> "$TEST_TRASH_ROOT/calls"
case "$1" in
installed)
 if [ -f "$TEST_TRASH_ROOT/data/applications/desktop-test.desktop" ]; then
  printf '%s\tyes\n' "$2"
 else
  printf '%s\tno\n' "$2"
 fi ;;
remove) rm "$TEST_TRASH_ROOT/data/applications/desktop-test.desktop" ;;
*) exit 2 ;;
esac
)SH", true);
	qputenv("PATH", (home.path() + "/commands:/usr/bin:/bin").toUtf8());
	fixture(home.path() + "/commands/pkexec",
		"#!/bin/sh\n[ \"$1\" = \"$ZACOS9_APPSTORE_HELPER\" ] || exit 2\nexec \"$@\"\n", true);
	qputenv("TEST_TRASH_ROOT", home.path().toUtf8());
	qputenv("ZACOS9_APPSTORE_HELPER", (home.path() + "/commands/apt-helper").toUtf8());
	fixture(home.path() + "/installed", "installed");
	fixture(home.path() + "/personal-data", "keep");
	// Let GIO expire its fixture catalog, then resolve all origins again.
	QElapsedTimer appTimer;
	appTimer.start();
	while (appTimer.elapsed() < 600) {
		settle();
	}
	vfsRefresh();
	QString error;
	check(!appTrashQueue("unowned.desktop", &error) && !error.isEmpty(),
		"unsupported uninstall sources are reported without queueing");
	const QString appPath = vfsPathFor("applications/com.example.TrashTest.desktop");
	auto *applicationsWindow = FolderWindow::open(vfsPathFor("applications"));
	Finder::instance().setFront(applicationsWindow);
	applicationsWindow->selectByName("Trash Test");
	Finder::instance().command("move-to-trash");
	auto queued = appTrashEntries(&error);
	check(error.isEmpty() && queued.size() == 1 && QFile::exists(home.path() + "/installed"),
		"Move To Trash queues the application without uninstalling it");
	check(!vfsNode(appPath), "queued application is hidden from Applications");
	vfsRefresh();
	check(appTrashEntries(&error).size() == 1 && !vfsNode(appPath),
		"queue survives catalog reload using its persisted Trash record");
	QProcess restarted;
	bool restartDone = false;
	QObject::connect(&restarted, &QProcess::finished, [&] { restartDone = true; });
	restarted.start(QCoreApplication::applicationFilePath(), { "--check-app-trash" });
	QElapsedTimer restartTimer;
	restartTimer.start();
	while (!restartDone && restartTimer.elapsed() < 5000) {
		QApplication::processEvents();
	}
	if (!restartDone) {
		restarted.kill();
	}
	check(restartDone && restarted.exitCode() == 0,
		"a separate process reads the queued app after restart");
	if (!queued.empty()) {
		auto recordItem = makeItem(QFileInfo(queued.front().path));
		check(recordItem->name == "Trash Test" && recordItem->kind == PL_ICON_APPLICATION,
			"Trash displays the application name and icon rather than the record filename");
		auto *trashWindow = FolderWindow::open(trashFilesPath());
		Finder::instance().setFront(trashWindow);
		trashWindow->selectByName("Trash Test");
		Finder::instance().command("put-away");
		check(appTrashEntries(&error).empty() && vfsNode(appPath),
			"Put Away cancels uninstall and restores Applications");
	}
	auto appItems = vfsList(vfsPathFor("applications"));
	for (auto &item : appItems) {
		if (item->path == appPath) {
			std::unique_ptr<QMimeData> mime(itemDragMime({item.get()}));
			check(mime->hasFormat(APPLICATION_ITEMS_MIME), "application drag carries its uninstall identity");
			QDropEvent drop(QPointF(), Qt::MoveAction | Qt::LinkAction, mime.get(),
				Qt::LeftButton, Qt::NoModifier);
			dropItems(&drop, desktop.m_trash.get(), path);
			check(drop.isAccepted() && drop.dropAction() == Qt::MoveAction,
				"drag-to-Trash queues rather than aliases the application");
			settle();
		}
	}
	check(appTrashEntries(&error).size() == 1 && !vfsNode(appPath),
		"application drag populates the persistent uninstall queue");
	const auto currentQueue = appTrashEntries(&error);
	if (!currentQueue.empty()) {
		TrashedApplication decoded;
		check(!appTrashRead(home.path() + "/personal-data", &decoded, &error),
			"ordinary files are never interpreted as uninstall records");
		QFile record(currentQueue.front().path);
		check(record.open(QIODevice::ReadOnly), "read valid queue fixture");
		const QByteArray validRecord = record.readAll();
		record.close();
		fixture(currentQueue.front().path, "{}");
		check(!appTrashRead(currentQueue.front().path, &decoded, &error) && !error.isEmpty(),
			"malformed queue metadata is rejected explicitly");
		fixture(currentQueue.front().path, validRecord);
		check(appTrashQueue("com.example.TrashTest.desktop", &error) &&
			appTrashEntries(&error).size() == 1, "repeated queueing does not duplicate removal");
	}
	QTimer::singleShot(0, [] {
		if (auto *alert = dynamic_cast<Alert *>(QApplication::activeModalWidget())) {
			alert->reject();
		}
	});
	Finder::instance().emptyTrash();
	check(appTrashEntries(&error).size() == 1 && QFile::exists(home.path() + "/installed"),
		"canceling Empty Trash leaves the app installed and queued");
	QTimer confirmations;
	confirmations.setInterval(10);
	QObject::connect(&confirmations, &QTimer::timeout, [] {
		if (auto *alert = dynamic_cast<Alert *>(QApplication::activeModalWidget())) {
			alert->accept();
		}
	});
	fixture(apps + "/com.example.TrashTest.desktop",
		"[Desktop Entry]\nType=Application\nName=Trash Test\nExec=/bin/true\n"
		"X-Flatpak=com.example.TrashTest\nCategories=Settings;\n");
	vfsRefresh();
	confirmations.start();
	Finder::instance().emptyTrash();
	confirmations.stop();
	check(appTrashEntries(&error).size() == 1 && QFile::exists(home.path() + "/installed"),
		"an already queued app that becomes a system tool is not uninstalled");
	fixture(apps + "/com.example.TrashTest.desktop",
		"[Desktop Entry]\nType=Application\nName=Trash Test\nExec=/bin/true\n"
		"X-Flatpak=com.example.TrashTest\n");
	vfsRefresh();
	fixture(home.path() + "/fail", "fail");
	confirmations.start();
	Finder::instance().emptyTrash();
	confirmations.stop();
	check(appTrashEntries(&error).size() == 1 && QFile::exists(home.path() + "/installed"),
		"failed uninstall retains its record and application for retry or Put Away");
	check(!QDir(trashFilesPath()).entryList({ "External folder alias copy*" }).isEmpty(),
		"failed uninstall does not delete unrelated ordinary Trash contents");
	QFile::remove(home.path() + "/fail");
	fixture(home.path() + "/leave-installed", "keep");
	confirmations.start();
	Finder::instance().emptyTrash();
	confirmations.stop();
	check(appTrashEntries(&error).size() == 1 && QFile::exists(home.path() + "/installed"),
		"a successful command that leaves the app installed does not clear its queue record");
	QFile::remove(home.path() + "/leave-installed");
	check(appTrashQueue("desktop-test.desktop", &error), "Debian application can also be queued");
	vfsRefresh();
	confirmations.start();
	Finder::instance().emptyTrash();
	confirmations.stop();
	check(appTrashEntries(&error).empty() && QDir(trashFilesPath()).isEmpty() &&
		!QFile::exists(home.path() + "/installed") &&
		!QFile::exists(apps + "/desktop-test.desktop"),
		"Empty Trash uninstalls both source types and clears only completed records");
	check(QFile::exists(home.path() + "/personal-data"), "uninstall keeps personal application data");
	QFile calls(home.path() + "/calls");
	check(calls.open(QIODevice::ReadOnly), "open mock uninstall command log");
	const QByteArray commands = calls.readAll();
	check(commands.contains("--user uninstall --noninteractive -- com.example.TrashTest"),
		"Flatpak uninstall is user-scoped and does not delete data");
	check(commands.contains("remove coreutils") && !commands.contains("purge"),
		"Debian uninstall uses the existing helper without purging configuration");
	for (QWidget *widget : QApplication::topLevelWidgets()) {
		if (dynamic_cast<FolderWindow *>(widget)) {
			widget->close();
		}
	}
	settle();
	return failures ? 1 : 0;
}
