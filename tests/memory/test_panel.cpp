/* The Memory panel (shell/panels/memory.cpp) with a fake helper: the
 * status it shows, the sizes its sliders offer, each control asking the
 * helper for its change, Use Defaults, and a refusal shown as a problem
 * with the controls left as the system really is. */
#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "memory.h"

static int fails;
static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

static QString read(const QString &path) {
	QFile f(path);
	return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

static void write(const QString &path, const QByteArray &data) {
	QFile f(path);
	if (f.open(QIODevice::WriteOnly)) {
		f.write(data);
	}
}

static constexpr qint64 MB = 1024 * 1024, GB = 1024 * MB;

int main(int argc, char **argv) {
	/* ---- the pure parts */
	const MemoryStatus s = parseMemoryStatus(
		"memory\t33554432000\t25000000000\t14000000000\n"
		"swapfile\t17179869184\t104857600\tyes\n"
		"swapfile-room\t74000000000\n"
		"other-swap\t/dev/zram0\t4294967296\t0\n"
		"guard\tyes\tno\n"
		"ramdisk\tno\t0\t0\tyes\t536870912\n");
	check(s.valid && s.total == 33554432000 && s.cache == 14000000000 && s.swapOn &&
			s.swapSize == 16 * GB && s.swapUsed == 100 * MB && s.swapRoom == 74000000000,
		"the helper's status is read: memory, the disk cache and virtual memory");
	check(s.otherSwap == QStringList{ "/dev/zram0, 4 GB" } && s.guardInstalled && !s.guardOn &&
			!s.ramDiskOn && s.ramDiskSave && s.ramDiskSize == 512 * MB,
		"and other swap, the guard, and the RAM Disk as it's set though off");
	check(!parseMemoryStatus("nonsense\n").valid, "status without memory figures isn't valid");
	check(memorySizeText(16 * GB) == "16 GB" && memorySizeText(1536 * MB) == "1.5 GB" &&
			memorySizeText(512 * MB) == "512 MB", "sizes read as Mac OS 9 would put them");
	const QList<int> swap = memorySwapSteps(10 * GB, 0);
	check(swap.front() == 1024 && swap.back() == 8192, "virtual memory sizes stop where the disk would have under 2 GB free");
	check(memorySwapSteps(10 * GB, 3000 * MB).contains(3000), "the current virtual memory size is always offered");
	const QList<int> ram = memoryRamDiskSteps(8 * GB, 0);
	check(ram.front() == 64 && ram.back() == 4096, "RAM Disk sizes stop at half of memory");

	/* ---- the panel, with a fake helper */
	QTemporaryDir dir;
	const QString state = dir.path() + "/state", log = dir.path() + "/log", fail = dir.path() + "/fail";
	write(state, "SWAP=8589934592\nSWAPON=yes\nGUARD=no\nRAM=no\nRAMSIZE=0\nSAVE=no\n");
	const QString helper = dir.path() + "/helper";
	write(helper, QString(
		"#!/bin/sh\n"
		"STATE='%1'; LOG='%2'; FAIL='%3'\n"
		". \"$STATE\"\n"
		"if [ \"$1\" = status ]; then\n"
		"  printf 'memory\\t17179869184\\t12000000000\\t3000000000\\n'\n"
		"  printf 'swapfile\\t%s\\t0\\t%s\\nswapfile-room\\t60000000000\\n' \"$SWAP\" \"$SWAPON\"\n"
		"  printf 'guard\\tyes\\t%s\\n' \"$GUARD\"\n"
		"  printf 'ramdisk\\t%s\\t%s\\t0\\t%s\\t%s\\n' \"$RAM\" \"$RAMSIZE\" \"$SAVE\" \"$RAMSIZE\"\n"
		"  exit 0\n"
		"fi\n"
		"echo \"$*\" >> \"$LOG\"\n"
		"if [ -e \"$FAIL\" ]; then echo 'Quit some programs, then try again.' >&2; exit 1; fi\n"
		"case \"$1 $2\" in\n"
		"  'swap off') SWAPON=no ;;\n"
		"  swap*) SWAP=$(($2 * 1048576)); SWAPON=yes ;;\n"
		"  'guard on') GUARD=yes ;; 'guard off') GUARD=no ;;\n"
		"  'ramdisk off') RAM=no ;;\n"
		"  ramdisk*) RAM=yes; RAMSIZE=$(($2 * 1048576)); [ \"$3\" = save ] && SAVE=yes || SAVE=no ;;\n"
		"esac\n"
		"printf 'SWAP=%s\\nSWAPON=%s\\nGUARD=%s\\nRAM=%s\\nRAMSIZE=%s\\nSAVE=%s\\n' "
		"\"$SWAP\" \"$SWAPON\" \"$GUARD\" \"$RAM\" \"$RAMSIZE\" \"$SAVE\" > \"$STATE\"\n").arg(state, log, fail).toUtf8());
	QFile::setPermissions(helper, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
	qputenv("ZACOS9_MEMORY_HELPER", helper.toUtf8());
	QApplication app(argc, argv);
	MemoryPanel panel;
	check(panel.status().valid && panel.status().swapOn && panel.status().swapSize == 8 * GB &&
			panel.problem().isEmpty(), "the panel shows what the helper reports");
	check(!panel.grab().isNull(), "the panel draws offscreen");

	panel.setVirtualMemorySize(16384);
	check(read(log).endsWith("swap 16384\n") && panel.status().swapSize == 16 * GB,
		"choosing a size asks for that much virtual memory");
	panel.setVirtualMemory(false);
	check(read(log).endsWith("swap off\n") && !panel.status().swapOn, "Off turns virtual memory off");
	panel.setVirtualMemory(true);
	check(read(log).endsWith("swap 16384\n") && panel.status().swapOn,
		"On turns it on again at the size the slider shows");
	panel.setGuard(true);
	check(read(log).endsWith("guard on\n") && panel.status().guardOn, "the checkbox turns the out-of-memory guard on");
	panel.setRamDisk(true);
	check(read(log).endsWith("ramdisk 512 nosave\n") && panel.status().ramDiskOn &&
			panel.status().ramDiskSize == 512 * MB, "On makes a 512 MB RAM Disk, not saved");
	panel.setRamDiskSave(true);
	check(read(log).endsWith("ramdisk 512 save\n") && panel.status().ramDiskSave, "Save on Shut Down is asked for");
	panel.setRamDiskSize(1024);
	check(read(log).endsWith("ramdisk 1024 save\n") && panel.status().ramDiskSize == GB,
		"a new size resizes the RAM Disk, still saved");

	write(fail, "");
	panel.setVirtualMemory(false);
	check(panel.problem().contains("Quit some programs") && panel.status().swapOn,
		"a refusal is shown, and virtual memory is shown as still on");
	QFile::remove(fail);

	write(state, "SWAP=4294967296\nSWAPON=no\nGUARD=no\nRAM=yes\nRAMSIZE=536870912\nSAVE=no\n");
	write(log, "");
	MemoryPanel fresh;
	fresh.useDefaults();
	check(read(log) == "swap 16384\nguard on\nramdisk off\n" && fresh.status().swapOn &&
			fresh.status().guardOn && !fresh.status().ramDiskOn && fresh.problem().isEmpty(),
		"Use Defaults: 16 GB of virtual memory, the guard on, no RAM Disk");
	return fails ? 1 : 0;
}
