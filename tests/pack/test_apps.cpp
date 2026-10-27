/* The Pack's tiles: one per program, by name, the Sniffer always there and
 * in front when no window is; hidden when all its windows are. */
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>

#include "apps.h"

static int fails;

static void check(bool ok, const char *what) {
	std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

static bool lookup(const QString &appId, AppDetails *d) {
	if (appId == "org.example.zebra") {
		d->name = "Zebra";
		d->desktopFile = "/usr/share/applications/org.example.zebra.desktop";
		return true;
	}
	if (appId == "aardvark") {
		d->name = "Aardvark";
		return true;
	}
	return false;
}

static QStringList names(const std::vector<PackApp> &apps) {
	QStringList n;
	for (const PackApp &a : apps) {
		n << a.name;
	}
	return n;
}

int main() {
	std::vector<PackApp> apps = packApps({}, lookup);
	check(names(apps) == QStringList{ "Sniffer" } && apps[0].active, "nothing open: the Sniffer, in front");

	apps = packApps({ { "org.example.zebra", "Doc 1", true, false },
		{ "org.example.zebra", "Doc 2", false, false },
		{ "aardvark", "Burrow", false, true },
		{ "mystery", "Mystery Window", false, false } }, lookup);
	check(names(apps) == QStringList({ "Aardvark", "Mystery Window", "Sniffer", "Zebra" }),
		"one tile per program, alphabetical; one without a desktop file goes by its title");
	check(apps[3].active && !apps[2].active && apps[3].desktopFile.endsWith("zebra.desktop"),
		"the program with the front window is in front");
	check(apps[0].hidden && !apps[3].hidden, "a program whose windows are all hidden is hidden");

	apps = packApps({ { "zacos9-finder", "Work", true, false }, { "aardvark", "", false, false } }, lookup);
	check(names(apps) == QStringList({ "Aardvark", "Sniffer" }) && apps[1].active,
		"the Sniffer's own windows make one Sniffer tile");
	/* Documents dropped on a tile open with its program. */
	QTemporaryDir dir;
	const QString out = dir.filePath("opened");
	QFile desktop(dir.filePath("writer.desktop"));
	desktop.open(QIODevice::WriteOnly);
	desktop.write(QString("[Desktop Entry]\nType=Application\nName=Writer\n"
		"Exec=sh -c 'echo \"$@\" > %1' writer %F\n").arg(out).toUtf8());
	desktop.close();
	PackApp writer;
	writer.appId = "writer";
	writer.desktopFile = desktop.fileName();
	const QString letter = dir.filePath("Letter.txt");
	QFile(letter).open(QIODevice::WriteOnly);
	check(openWithApp(writer, { letter }), "dropped documents are handed to the program");
	QElapsedTimer t;
	t.start();
	while (!QFile::exists(out) && t.elapsed() < 5000) {
		QThread::msleep(50);
	}
	QFile opened(out);
	opened.open(QIODevice::ReadOnly);
	check(QString::fromUtf8(opened.readAll()).trimmed() == letter, "and it opens them");
	check(!openWithApp(PackApp{}, { letter }), "a program without a desktop file takes nothing");
	return fails ? 1 : 0;
}
