/*
 * Folder Tricks (attached programs run when items arrive in a folder) and
 * Contextual Menu Items (desktop entries offered by the files' types).
 */
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <functional>

#include "contextitems.h"
#include "foldertricks.h"

static int failures;
static void check(bool ok, const char *what) {
	std::printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
	failures += !ok;
}

static bool waitFor(const std::function<bool()> &condition, int ms = 5000) {
	QElapsedTimer timer;
	timer.start();
	while (!condition() && timer.elapsed() < ms) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		QThread::msleep(10);
	}
	return condition();
}

static bool write(const QString &path, const QByteArray &data, bool executable = false) {
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
		return false;
	}
	f.close();
	return !executable || f.setPermissions(f.permissions() | QFile::ExeOwner);
}

static QByteArray read(const QString &path) {
	QFile f(path);
	return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	QTemporaryDir tmp;
	check(tmp.isValid(), "temporary folder");
	const QString root = tmp.path();
	const QString tricksDir = root + "/Folder Tricks", config = root + "/folder-tricks.json";
	const QString inbox = root + "/Inbox", log = root + "/trick.log";
	check(QDir().mkpath(tricksDir) && QDir().mkpath(inbox), "make the folders");
	check(write(tricksDir + "/Log Arrivals",
		"#!/bin/sh\necho \"$PWD|$*\" >> '" + log.toUtf8() + "'\n", true) &&
		write(tricksDir + "/Another", "#!/bin/sh\n", true) &&
		write(tricksDir + "/notes.txt", "not a program"), "write the tricks");

	{
		FolderTricks tricks(config, tricksDir);
		check(tricks.available() == QStringList({ "Another", "Log Arrivals" }),
			"the Folder Tricks folder's programs, by name; other files left out");
		check(!tricks.attach(inbox, "notes.txt") && !tricks.attach(root + "/none", "Another"),
			"only programs, and only to folders");
		check(tricks.attach(inbox, "Log Arrivals") && tricks.attach(inbox, "Log Arrivals"),
			"attach a trick (twice is once)");
		check(tricks.attached(inbox + "/") == QStringList({ "Log Arrivals" }), "it's attached");
	}

	FolderTricks tricks(config, tricksDir);
	check(tricks.attached(inbox) == QStringList({ "Log Arrivals" }), "attachments are remembered");
	tricks.settleMs = 150;
	QStringList ranWith;
	tricks.ran = [&](const QString &trick, const QString &, const QStringList &items) {
		ranWith << trick + ":" + QStringList(items).replaceInStrings(inbox + "/", "").join(",");
	};

	check(write(inbox + "/a.txt", "a") && write(inbox + "/b.txt", "b"), "two items arrive");
	check(waitFor([&] { return !ranWith.isEmpty(); }), "the trick runs");
	check(ranWith == QStringList({ "Log Arrivals:a.txt,b.txt" }), "once, for both, in order");
	check(waitFor([&] { return read(log).contains("|"); }) &&
		read(log).trimmed() == (QFileInfo(inbox).canonicalFilePath() + "|" +
			QFileInfo(inbox).canonicalFilePath() + " " + inbox + "/a.txt " + inbox + "/b.txt").toUtf8(),
		"run in the folder, given the folder and the new items");

	ranWith.clear();
	check(write(inbox + "/.hidden", "h") && write(inbox + "/movie.mp4.part", "p"),
		"a hidden file and a partial download");
	check(!waitFor([&] { return !ranWith.isEmpty(); }, 600), "don't count as arrivals");
	check(QFile::rename(inbox + "/movie.mp4.part", inbox + "/movie.mp4"), "the download finishes");
	check(waitFor([&] { return !ranWith.isEmpty(); }) &&
		ranWith == QStringList({ "Log Arrivals:movie.mp4" }), "and then it arrives");

	ranWith.clear();
	check(QFile::remove(inbox + "/a.txt"), "an item leaves");
	check(!waitFor([&] { return !ranWith.isEmpty(); }, 500), "leaving runs nothing");

	check(tricks.remove(inbox, "Log Arrivals") && !tricks.remove(inbox, "Log Arrivals") &&
		tricks.attached(inbox).isEmpty(), "remove the trick");
	check(FolderTricks(config, tricksDir).attached(inbox).isEmpty(), "and that's remembered");
	check(write(inbox + "/c.txt", "c"), "another arrives");
	check(!waitFor([&] { return !ranWith.isEmpty(); }, 500), "no trick runs");

	/* Contextual Menu Items. */
	const QString items = root + "/Contextual Menu Items", out = root + "/command.out";
	check(QDir().mkpath(items), "the Contextual Menu Items folder");
	const QString record = root + "/record-arguments";
	check(write(record, "#!/bin/sh\nfor f; do echo \"$f\"; done > '" + out.toUtf8() + "'\n", true),
		"a program that records its arguments");
	const auto entry = [&](const QString &file, const QByteArray &name, const QByteArray &extra) {
		return write(items + "/" + file, "[Desktop Entry]\nType=Application\nName=" + name +
			"\nExec='" + record.toUtf8() + "' %F\n" + extra);
	};
	check(entry("folder.desktop", "Open Terminal Here", "MimeType=inode/directory;\n") &&
		entry("pictures.desktop", "Rotate Picture", "MimeType=image/*;\n") &&
		entry("files.desktop", "Checksum", "MimeType=all/allfiles;\n") &&
		entry("any.desktop", "Anything", "") &&
		entry("hidden.desktop", "Hidden", "Hidden=true\n") &&
		write(items + "/broken.desktop", "not a desktop entry") &&
		entry("missing.desktop", "Missing Program", "TryExec=/nonexistent/program\n"),
		"write the entries");
	const QString picture = root + "/dot.png", text = root + "/note.txt";
	check(write(picture, QByteArray::fromHex("89504e470d0a1a0a")) && write(text, "hello"),
		"a picture and a text file");
	const auto names = [&](const QStringList &paths) {
		QStringList list;
		for (const ContextCommand &c : contextCommands(items, paths)) {
			list << c.name;
		}
		return list;
	};
	check(names({ inbox }) == QStringList({ "Anything", "Open Terminal Here" }), "for a folder");
	check(names({ picture }) == QStringList({ "Anything", "Checksum", "Rotate Picture" }),
		"for a picture");
	check(names({ text }) == QStringList({ "Anything", "Checksum" }), "for a text file");
	check(names({ picture, text }) == QStringList({ "Anything", "Checksum" }),
		"for both: only what fits every item");
	check(names({ picture, inbox }) == QStringList({ "Anything" }), "for a file and a folder");
	check(names({}).isEmpty(), "nothing for nothing");

	const QList<ContextCommand> commands = contextCommands(items, { picture, text });
	QString error;
	check(commands.size() == 2 && runContextCommand(commands[1], { picture, text }, &error),
		"run a command");
	check(waitFor([&] { return read(out).count('\n') == 2; }) &&
		read(out) == (picture + "\n" + text + "\n").toUtf8(), "given every item");
	QFile::remove(items + "/files.desktop");
	check(!runContextCommand(commands[1], { picture }, &error) && error.contains("no longer"),
		"a command taken out of the folder says so");

	return failures ? 1 : 0;
}
