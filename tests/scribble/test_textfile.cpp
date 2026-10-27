/* Scribble's files: UTF-8 or Latin-1 read, line endings kept, binary refused. */
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

#include "textfile.h"

static int fails;

static void check(bool ok, const char *what) {
	std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

static QByteArray bytesOf(const QString &path) {
	QFile f(path);
	f.open(QIODevice::ReadOnly);
	return f.readAll();
}

static void put(const QString &path, const QByteArray &bytes) {
	QFile f(path);
	f.open(QIODevice::WriteOnly);
	f.write(bytes);
}

int main() {
	QTemporaryDir dir;
	TextFile file;
	QString error;

	put(dir.filePath("mac.txt"), "Read Me\rfirst line\r");
	check(readTextFile(dir.filePath("mac.txt"), &file, &error) && file.text == "Read Me\nfirst line\n" &&
		file.ending == LineEnding::Cr, "a Mac OS Read Me's CR line endings read as lines");
	file.text += "added\n";
	check(writeTextFile(dir.filePath("mac.txt"), file, &error) &&
		bytesOf(dir.filePath("mac.txt")) == "Read Me\rfirst line\radded\r", "and are written back as CRs");

	put(dir.filePath("dos.txt"), "one\r\ntwo\r\n");
	check(readTextFile(dir.filePath("dos.txt"), &file, &error) && file.text == "one\ntwo\n" &&
		file.ending == LineEnding::CrLf, "DOS line endings likewise");

	put(dir.filePath("utf8.txt"), "caf\xc3\xa9 \xe2\x80\x94 \xf0\x9f\x90\x95\n");
	check(readTextFile(dir.filePath("utf8.txt"), &file, &error) &&
		file.text == QString::fromUtf8("café — 🐕\n"), "UTF-8 is read as UTF-8");
	put(dir.filePath("latin1.txt"), "caf\xe9\n");
	check(readTextFile(dir.filePath("latin1.txt"), &file, &error) && file.text == QString::fromUtf8("café\n"),
		"text that isn't UTF-8 is read as Latin-1");

	put(dir.filePath("picture.png"), QByteArray("\x89PNG\r\n\x1a\n\0\0\0\rIHDR", 16));
	check(!readTextFile(dir.filePath("picture.png"), &file, &error) && !error.isEmpty(),
		"a binary file is refused, with a reason");
	check(!readTextFile(dir.filePath("missing.txt"), &file, &error), "a missing file is too");

	check(writeTextFile(dir.filePath("new.txt"), TextFile{ "hello\n", LineEnding::Lf }, &error) &&
		bytesOf(dir.filePath("new.txt")) == "hello\n", "a new file is written in UTF-8 with LFs");
	return fails ? 1 : 0;
}
