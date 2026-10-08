#include "filechooserportal.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QFileDialog>
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
	return failures ? 1 : 0;
}
