/* The Startup Disk panel (shell/panels/startupdisk.cpp) with a fake helper:
 * it lists the systems with the current one selected, hides other
 * versions until asked, and choosing one asks the helper to set it. */
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>

#include "icons.h"

#include "startupdisk.h"

static int fails;
static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

static void settle(int ms = 300) {
	QElapsedTimer t;
	t.start();
	while (t.elapsed() < ms) {
		QCoreApplication::processEvents();
		QThread::msleep(5);
	}
}

int main(int argc, char **argv) {
	QTemporaryDir dir;
	const QString state = dir.path() + "/current", log = dir.path() + "/log", helper = dir.path() + "/helper";
	QFile s(state);
	s.open(QIODevice::WriteOnly);
	s.write("linux-main");
	s.close();
	QFile h(helper);
	h.open(QIODevice::WriteOnly);
	h.write(QString("#!/bin/sh\necho \"$@\" >> '%1'\ncase $1 in\n"
		"list) printf 'default\\t%s\\n' \"$(cat '%2')\"\n"
		"  printf 'entry\\tlinux-main\\tZacOS 9\\tlinux\\t0\\n'\n"
		"  printf 'entry\\tadv>linux-old\\tZacOS 9, older kernel\\tlinux\\t1\\n'\n"
		"  printf 'entry\\twin\\tWindows Boot Manager\\twindows\\t0\\n'\n"
		"  printf 'entry\\tfw\\tUEFI Firmware Settings\\tfirmware\\t0\\n' ;;\n"
		"set) printf %s \"$2\" > '%2' ;;\n"
		"detect) ;;\nesac\n").arg(log, state).toUtf8());
	h.close();
	h.setPermissions(h.permissions() | QFileDevice::ExeOwner);
	qputenv("ZACOS9_STARTUPDISK_HELPER", helper.toUtf8());
	QApplication app(argc, argv);

	QString current;
	const QList<StartupEntry> parsed = parseStartupEntries(
		"default\twin\nentry\twin\tWindows\twindows\t0\nentry\ta>b\tOld\tlinux\t1\nnoise\n", &current);
	check(current == "win" && parsed.size() == 2 && parsed[1].id == "a>b" && parsed[1].depth == 1,
		"the helper's list is read: entries and the current one");
	check(startupEntryIcon({ "w", "W", "windows", 0 }) == PL_ICON_WINDOWS &&
		startupEntryIcon({ "f", "F", "firmware", 0 }) == PL_ICON_EXT_CHIP &&
		startupEntryIcon({ "l", "L", "linux", 0 }) == PL_ICON_DISK, "each kind has its icon");

	StartupDiskPanel panel;
	panel.show();
	settle();
	check(panel.rows() == QStringList({ "ZacOS 9", "Windows Boot Manager", "UEFI Firmware Settings" }),
		"the systems, other versions of one left out");
	check(panel.selectedRow() == 0, "the one it starts up with selected");
	panel.setShowEveryVersion(true);
	check(panel.rows().size() == 4 && panel.rows()[1] == "ZacOS 9, older kernel", "Show every version shows them");
	panel.pick(2);
	settle();
	QFile l(log);
	l.open(QIODevice::ReadOnly);
	const QString asked = QString::fromUtf8(l.readAll());
	check(asked.contains("set win\n"), "choosing Windows asks the helper to start up with it");
	check(panel.current() == "win" && panel.selectedRow() == 2, "and the list shows it chosen, read back from GRUB");
	return fails ? 1 : 0;
}
