/*
 * key=value settings files (shell/common/conf.cpp): reading, the first
 * line of a key counting, spaces trimmed, set() in place and at the end,
 * other lines and sections kept, blank lines dropped, saving all at once.
 */
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

#include "conf.h"

static int fails;

static void check(bool ok, const char *what) {
	std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	QTemporaryDir dir;

	const KeyValueFile missing = KeyValueFile::read(dir.path() + "/nothing.conf");
	check(!missing.exists() && missing.value("a", "fallback") == "fallback", "a missing file: empty, the fallback");

	KeyValueFile f = KeyValueFile::parse("[General]\n# a comment\nname = Studio Mac \nafp=yes\n\nurl=a=b\nafp=no\n");
	check(f.value("name") == "Studio Mac", "spaces around a key and its value are trimmed");
	check(f.yes("afp"), "the first line of a key counts");
	check(f.value("url") == "a=b", "a value may hold =");
	check(!f.contains("General") && !f.contains("# a comment"), "sections and comments aren't keys");

	f.set("name", "Office");
	f.set("smb", "yes");
	f.setYes("afp", false);
	check(f.text() == "[General]\n# a comment\nname=Office\nafp=no\nurl=a=b\nsmb=yes\n",
		"set() replaces in place (later lines of the key go), adds at the end, keeps the rest");
	f.remove("url");
	check(!f.contains("url") && f.text().count('\n') == 5, "remove()");

	const QString path = dir.path() + "/sharing.conf";
	QString error;
	check(f.save(path, &error) && error.isEmpty(), "saved");
	const KeyValueFile back = KeyValueFile::read(path);
	check(back.exists() && back.value("name") == "Office" && back.text() == f.text(), "and read back the same");
	check(!f.save(dir.path() + "/no/such/folder.conf", &error) && !error.isEmpty(), "a save that can't happen says why");
	return fails ? 1 : 0;
}
