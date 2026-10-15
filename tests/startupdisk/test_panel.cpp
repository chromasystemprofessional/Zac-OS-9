/* The Startup Disk panel (shell/panels/startupdisk.cpp) with a fake helper:
 * it lists the systems with the current one selected, hides other
 * versions until asked, and choosing one asks the helper to set it. With a
 * fake Classic launcher: Old Dog's disks with a System Folder (HFS, HFS+,
 * in a partition map), the chosen one, and choosing another. */
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QtEndian>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <cstring>

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

/* A disk image: `kind` "hfs", "hfs+" or "apm" (HFS in a partition map). */
static void image(const QString &path, const QString &kind, const QByteArray &name, bool blessed) {
	QByteArray data(64 * 1024, '\0');
	qint64 vol = 0;
	if (kind == "apm") {
		data.replace(0, 2, "ER");
		QByteArray e(512, '\0');
		e.replace(0, 2, "PM");
		qToBigEndian<quint32>(64, e.data() + 8); /* starts at block 64 */
		e.replace(48, 9, "Apple_HFS");
		QByteArray driver(512, '\0');
		driver.replace(0, 2, "PM");
		driver.replace(48, 14, "Apple_Driver43");
		data.replace(512, 512, driver);
		data.replace(1024, 512, e);
		vol = 64 * 512;
	}
	char *h = data.data() + vol + 1024;
	if (kind == "hfs+") {
		memcpy(h, "H+", 2);
		qToBigEndian<quint32>(blessed ? 77 : 0, h + 80);
	} else {
		memcpy(h, "BD", 2);
		h[36] = static_cast<char>(name.size());
		memcpy(h + 37, name.constData(), name.size());
		qToBigEndian<quint32>(blessed ? 19 : 0, h + 92);
	}
	QFile f(path);
	f.open(QIODevice::WriteOnly);
	f.write(data);
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

	// Old Dog's disks: an installed one, a CD, one partitioned, a blank one.
	const QString hd = dir.path() + "/Macintosh HD.dsk", cd = dir.path() + "/macos_90.iso",
		apm = dir.path() + "/Partitioned.hfv", plus = dir.path() + "/Plus.dsk", blank = dir.path() + "/Blank.dsk";
	image(hd, "hfs", "Macintosh HD", true);
	image(cd, "hfs", "Mac OS 9", true);
	image(apm, "apm", "Extra", true);
	image(plus, "hfs+", "", true);
	image(blank, "hfs", "Untitled", false);
	const QString launcher = dir.path() + "/zacos9-classic";
	QFile cl(launcher);
	cl.open(QIODevice::WriteOnly);
	cl.write(QString("#!/bin/sh\nfor d in '%1' '%2' '%3' '%4' '%5'; do echo \"disk: $d\"; done\n"
		"echo 'disk: %6/Made.dsk (blank, made at first start)'\n"
		"s=$(sed -n 's/^startup-disk=//p' \"$XDG_CONFIG_HOME/zacos9/classic.conf\" 2>/dev/null)\n"
		"[ -z \"$s\" ] || echo \"startup: $s\"\n").arg(hd, cd, apm, plus, blank, dir.path()).toUtf8());
	cl.close();
	cl.setPermissions(cl.permissions() | QFileDevice::ExeOwner);
	qputenv("ZACOS9_CLASSIC", launcher.toUtf8());
	qputenv("XDG_CONFIG_HOME", (dir.path() + "/config").toUtf8());
	QDir().mkpath(dir.path() + "/config/zacos9");
	QFile conf(dir.path() + "/config/zacos9/classic.conf");
	conf.open(QIODevice::WriteOnly);
	conf.write("other=kept\n");
	conf.close();
	QApplication app(argc, argv);

	const ClassicDisk read = classicDisk(apm);
	check(read.system && read.name == "Extra" && !read.cd, "a volume in a partition map: its name and System Folder");
	check(classicDisk(cd).cd && classicDisk(cd).name == "Mac OS 9", "a CD image is a CD");
	check(!classicDisk(blank).system && classicDisk(plus).system && classicDisk(plus).name == "Plus",
		"no blessed folder, no system; HFS+ named by its file");

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

	check(panel.classicRows() == QStringList({ "Macintosh HD", "Mac OS 9 (CD)", "Extra", "Plus" }),
		"Old Dog's disks with a System Folder");
	check(panel.classicSelected() == 0 && panel.classicProblem().isEmpty(),
		"no choice yet: the first, which the Mac starts from");
	panel.pickClassic(1);
	settle();
	QFile c(dir.path() + "/config/zacos9/classic.conf");
	c.open(QIODevice::ReadOnly);
	const QString kept = QString::fromUtf8(c.readAll());
	check(kept == "other=kept\nstartup-disk=" + cd + "\n", "choosing the CD writes classic.conf, other lines kept");
	check(panel.classicSelected() == 1, "and it shows chosen, read back from the launcher");
	return fails ? 1 : 0;
}
