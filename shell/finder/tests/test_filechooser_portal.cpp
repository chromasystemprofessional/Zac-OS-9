#include "filechooserportal.h"

#include <QApplication>
#include <QDir>
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
	QApplication app(argc, argv);
	const QList<FileChooserLocation> locations = fileChooserLocations();
	check(!locations.isEmpty() && locations.front().name == "Zacintosh HD" &&
		QDir::cleanPath(locations.front().path) == QDir::cleanPath(
			QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
			"/zacos9/Zacintosh HD"),
		"Zacintosh HD opens the private Finder workspace");
	QSet<QString> paths;
	for (const FileChooserLocation &location : locations) {
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
