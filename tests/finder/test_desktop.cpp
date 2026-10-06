#include <QApplication>
#include <QDir>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileSystemWatcher>
#include <QLineEdit>
#include <QMimeData>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include "alert.h"
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
	QFile launcher(home.path() + "/data/applications/desktop-test.desktop");
	check(launcher.open(QIODevice::WriteOnly), "create isolated application launcher");
	launcher.write("[Desktop Entry]\nType=Application\nName=Desktop Test\nExec=true\n");
	launcher.close();
	QApplication app(argc, argv);
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
	Finder::instance().openItem(alias.get());
	check(FolderWindow::isOpen(alias->path), "opening folder alias opens its real contents");
	const auto copied = transferItems({alias->path}, path, true);
	check(!copied.isEmpty() && QFileInfo(alias->path + " copy").isSymLink(),
		"Option-copy preserves a folder alias rather than copying its target");
	check(QFileInfo(alias->path + " copy").symLinkTarget() == path + "/External folder",
		"copied alias still points to original folder");

	QFileDialog dialog(nullptr, "Save");
	dialog.setOption(QFileDialog::DontUseNativeDialog);
	dialog.setAcceptMode(QFileDialog::AcceptSave);
	dialog.setSidebarUrls({QUrl::fromLocalFile(home.path())});
	dialog.show();
	settle();
	check(dialog.sidebarUrls().contains(QUrl::fromLocalFile(path)) &&
		dialog.sidebarUrls().contains(QUrl::fromLocalFile(home.path())),
		"Qt save dialog adds Desktop without replacing existing places");
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
			appAlias = appDrop.isAccepted() && QFileInfo(shortcut).symLinkTarget() == launcher.fileName()
				&& makeItem(QFileInfo(shortcut))->kind == PL_ICON_APPLICATION &&
				QFileInfo::exists(launcher.fileName());
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
	closeAlert();
	Finder::instance().openItem(unavailable.get());
	check(!QFileInfo::exists(path + "/missing") && !FolderWindow::isOpen(unavailable->path),
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
	for (QWidget *widget : QApplication::topLevelWidgets()) {
		if (dynamic_cast<FolderWindow *>(widget)) {
			widget->close();
		}
	}
	settle();
	return failures ? 1 : 0;
}
