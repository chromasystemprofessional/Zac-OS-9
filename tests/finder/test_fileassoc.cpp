/* File associations: hidden extensions, renames that keep them, the default
 * application and its document icon, setting a default, and noticing
 * associations changed by another program. All in a private XDG tree. */

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QWidget>
#include <cstdio>
#include <functional>

#include "fileassoc.h"
#include "items.h"
#include "labeleditor.h"

static int failures = 0;

static void check(bool ok, const char *message) {
	std::printf("%s: %s\n", ok ? "ok" : "FAIL", message);
	failures += ok ? 0 : 1;
}

class PaintCounter : public QWidget {
public:
	int paints = 0;

protected:
	void paintEvent(QPaintEvent *) override { paints++; }
};

static bool writeAll(const QString &path, const QByteArray &data) {
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

static QByteArray readAll(const QString &path) {
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

static void desktopEntry(const QString &dir, const QString &id, const QString &name,
		const QString &mimeTypes, const QString &icon) {
	writeAll(dir + "/" + id, QStringLiteral("[Desktop Entry]\nType=Application\nName=%1\n"
		"Exec=/bin/true %f\nIcon=%2\nMimeType=%3\n").arg(name, icon, mimeTypes).toUtf8());
}

static bool waitUntil(const std::function<bool()> &done, int ms = 5000) {
	QElapsedTimer timer;
	timer.start();
	while (!done() && timer.elapsed() < ms) {
		QApplication::processEvents(QEventLoop::AllEvents, 20);
	}
	return done();
}

static bool isRed(uint32_t p) {
	return ((p >> 16) & 0xff) > 200 && ((p >> 8) & 0xff) < 60 && (p & 0xff) < 60;
}

static bool isBlue(uint32_t p) {
	return ((p >> 16) & 0xff) < 60 && ((p >> 8) & 0xff) < 60 && (p & 0xff) > 200;
}

int main(int argc, char **argv) {
	QTemporaryDir home;
	const QString base = QFileInfo(home.path()).canonicalFilePath();
	qputenv("HOME", QFile::encodeName(base));
	qputenv("XDG_CONFIG_HOME", QFile::encodeName(base + "/config"));
	qputenv("XDG_DATA_HOME", QFile::encodeName(base + "/data"));
	qputenv("XDG_CACHE_HOME", QFile::encodeName(base + "/cache"));
	qputenv("XDG_CONFIG_DIRS", QFile::encodeName(base + "/etc"));
	qputenv("XDG_DATA_DIRS", QFile::encodeName(base + "/sys"));
	qputenv("XDG_CURRENT_DESKTOP", "ZacOS9");
	QDir().mkpath(base + "/config");
	QDir().mkpath(base + "/etc");
	/* The system's MIME database, without its applications. */
	QDir().mkpath(base + "/sys");
	QFile::link("/usr/share/mime", base + "/sys/mime");

	const QString apps = base + "/data/applications";
	QDir().mkpath(apps);
	QImage red(32, 32, QImage::Format_ARGB32);
	red.fill(qRgb(255, 0, 0));
	QImage blue(32, 32, QImage::Format_ARGB32);
	blue.fill(qRgb(0, 0, 255));
	red.save(base + "/red.png");
	blue.save(base + "/blue.png");
	desktopEntry(apps, "zz-viewer.desktop", "Text Viewer", "text/plain;", base + "/red.png");
	desktopEntry(apps, "zz-editor.desktop", "Text Editor", "text/plain;image/x-xcf;",
		base + "/blue.png");
	const QString userList = base + "/config/mimeapps.list";
	writeAll(userList, "[Default Applications]\ntext/plain=zz-viewer.desktop;\n");

	QApplication app(argc, argv);

	/* ---- which extensions are hidden ---- */
	check(hiddenExtension("Report.txt") == ".txt", "a known extension is hidden");
	check(hiddenExtension("archive.tar.gz") == ".tar.gz", "a compound extension is hidden whole");
	check(hiddenExtension("Photo.JPG") == ".JPG", "the extension's own case is kept");
	check(hiddenExtension("Makefile").isEmpty(), "a name with no extension is shown whole");
	check(hiddenExtension(".bashrc").isEmpty() && hiddenExtension(".txt").isEmpty(),
		"a dot-file keeps its whole name");
	check(hiddenExtension("Notes.v2").isEmpty(), "an unknown suffix is part of the name");
	check(nameWithoutExtension("My Letter.odt") == "My Letter", "the shown name drops it");

	/* ---- renames keep the hidden extension ---- */
	check(renamedFileName("Report.txt", "Report", "Summary") == "Summary.txt",
		"renaming a file keeps its hidden extension");
	check(renamedFileName("Report.txt", "Report", "Summary.md") == "Summary.md",
		"typing an extension of its own replaces it");
	check(renamedFileName("Report.txt", "Report", "Report") == "Report.txt",
		"the unchanged name is the same file name");
	check(renamedFileName("Report.v2", QString(), "Final.v2") == "Final.v2",
		"a whole-name file is renamed as typed");
	check(renamedFileName("Report.txt", "Report", "Notes.v2") == "Notes.v2.txt",
		"an unknown suffix typed is part of the name");

	/* ---- items ---- */
	const QString notes = base + "/Notes.txt";
	writeAll(notes, "some notes\n");
	QDir().mkpath(base + "/Project.d");
	QFile::link(notes, base + "/Notes alias");
	QFile::link(base + "/gone.txt", base + "/Broken alias.txt");
	auto file = makeItem(QFileInfo(notes));
	auto folder = makeItem(QFileInfo(base + "/Project.d"));
	check(file->visibleName() == "Notes" && file->name == "Notes.txt" && file->key() == "Notes.txt",
		"a file shows its name without the extension, keyed by its real name");
	check(folder->visibleName() == "Project.d" && folder->shownName.isEmpty(),
		"a folder's name is shown whole");

	/* ---- types ---- */
	check(fileMimeType(notes) == "text/plain", "a text file's type");
	check(fileMimeType(base + "/Notes alias") == "text/plain", "an alias reports its original's type");
	check(fileMimeType(base + "/Project.d").isEmpty(), "a folder has no document type");
	check(fileMimeType(base + "/Broken alias.txt").isEmpty(),
		"an alias to a missing original has no type");

	/* ---- the default application ---- */
	check(defaultAppFor("text/plain") == "zz-viewer.desktop", "the user's mimeapps.list default");
	const QStringList candidates = appsFor("text/plain");
	check(candidates.value(0) == "zz-viewer.desktop" && candidates.contains("zz-editor.desktop"),
		"every application for the type, default first");
	check(appDisplayName("zz-editor.desktop") == "Text Editor", "an application's name by id");
	check(defaultAppFor("application/x-zacos9-nothing").isEmpty(), "no application for a type");

	bool listed = false;
	for (const FileType &t : openableFileTypes()) {
		if (t.mimeType == "text/plain") {
			listed = t.extensions.contains("txt") && !t.description.isEmpty() &&
				t.description[0].isUpper();
		}
	}
	check(listed, "the File Exchange list has the types applications open, with extensions");

	/* ---- the document icon ---- */
	const std::vector<uint32_t> *large = documentIcon("text/plain", 32);
	const std::vector<uint32_t> *small = documentIcon("text/plain", 16);
	check(large && large->size() == 32 * 32 && small && small->size() == 16 * 16,
		"a document icon in both sizes");
	if (large && small) {
		check(isRed((*large)[19 * 32 + 15]) && isRed((*small)[8 * 16 + 7]),
			"it bears the default application's icon");
		check(((*large)[20 * 32 + 6] & 0xffffff) == 0 && ((*large)[20 * 32 + 6] >> 24) == 0xff,
			"on the Platinum document page");
		check(((*large)[20 * 32 + 0] >> 24) == 0, "outside the page stays transparent");
	}
	check(documentIcon("application/x-zacos9-nothing", 32) == nullptr,
		"no application: the plain document icon");

	Pixels canvas(32, 32);
	paintIcon(&canvas.c, *file, 0, 0, 32, false);
	check(file->mimeResolved && file->mimeType == "text/plain" &&
		isRed(canvas.img.pixel(15, 19)), "the Finder draws a document with its application's icon");

	/* ---- setting the default ---- */
	PaintCounter watched;
	watched.resize(10, 10);
	watched.show();
	watchFileAssociations(&watched);
	waitUntil([&] { return watched.paints > 0; }, 1000);
	int paints = watched.paints;
	const unsigned generation = fileAssocGeneration();
	QString error;
	check(setDefaultApp("text/plain", "zz-editor.desktop", &error), "set a new default");
	check(readAll(userList).contains("text/plain=zz-editor.desktop"),
		"written to the user's mimeapps.list");
	check(defaultAppFor("text/plain") == "zz-editor.desktop" &&
		fileAssocGeneration() != generation, "the cached default follows");
	check(waitUntil([&] { return watched.paints > paints; }), "watched windows repaint");
	large = documentIcon("text/plain", 32);
	check(large && isBlue((*large)[19 * 32 + 15]), "the document icon follows the new default");
	check(!setDefaultApp("text/plain", "zz-missing.desktop", &error) && !error.isEmpty(),
		"an application that isn't installed is refused, with a reason");

	/* ---- another program changes it (atomic replace, as GLib writes) ---- */
	paints = watched.paints;
	const unsigned before = fileAssocGeneration();
	writeAll(userList + ".tmp", "[Default Applications]\ntext/plain=zz-viewer.desktop;\n");
	QFile::remove(userList);
	QFile::rename(userList + ".tmp", userList);
	check(waitUntil([&] { return fileAssocGeneration() != before; }),
		"a replaced mimeapps.list is noticed");
	check(waitUntil([&] { return defaultAppFor("text/plain") == "zz-viewer.desktop"; }),
		"and its default is used");
	check(watched.paints > paints || waitUntil([&] { return watched.paints > paints; }),
		"and watched windows repaint");

	/* Edited in place, after the replacement: the watcher was re-armed. */
	const unsigned again = fileAssocGeneration();
	{
		QFile f(userList);
		f.open(QIODevice::Append);
		f.write("image/x-xcf=zz-editor.desktop;\n");
	}
	check(waitUntil([&] { return fileAssocGeneration() != again; }),
		"an edit in place after a replacement is noticed");

	/* A desktop-specific list, created where there was none. */
	const unsigned created = fileAssocGeneration();
	writeAll(base + "/config/zacos9-mimeapps.list",
		"[Default Applications]\ntext/plain=zz-editor.desktop;\n");
	check(waitUntil([&] { return fileAssocGeneration() != created; }),
		"a newly created desktop-specific list is noticed");
	check(waitUntil([&] { return defaultAppFor("text/plain") == "zz-editor.desktop"; }),
		"and it takes precedence over mimeapps.list");

	std::printf("%d failure(s)\n", failures);
	return failures ? 1 : 0;
}
