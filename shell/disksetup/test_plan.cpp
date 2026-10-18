/* Disk Setup's partitioning plan (plan.cpp): free space, refusals, erasing. */
#include <QCoreApplication>
#include <cstdio>

#include "plan.h"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

static constexpr qulonglong GiB = 1024 * MiB;

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	DiskDrive d;
	d.device = "/dev/sdb";
	d.table = "gpt";
	d.size = 100 * GiB;
	DiskPart win;
	win.block = "/b/sdb1";
	win.device = "/dev/sdb1";
	win.offset = MiB;
	win.size = 40 * GiB;
	win.type = "ntfs";
	win.label = "Windows";
	DiskPart data = win;
	data.block = "/b/sdb2";
	data.device = "/dev/sdb2";
	data.offset = 60 * GiB;
	data.size = 20 * GiB;
	data.type = "ext4";
	data.label = "Data";
	data.mountPoints = { "/media/data" };
	d.parts = { win, data };

	/* Free space: between them and after; the last MiB kept. */
	const QList<DiskGap> gaps = freeGaps(d);
	CHECK(gaps.size() == 2 && gaps[0].offset == 40 * GiB + MiB && gaps[0].size == 20 * GiB - MiB);
	CHECK(gaps[1].offset == 80 * GiB && gaps[1].size == 20 * GiB - MiB);

	/* In use: not deleted, formatted or resized; the drive not erased. */
	DiskOp del{ DiskOp::Delete };
	del.part = 1;
	CHECK(refusal(d, del).contains("in use"));
	DiskOp table{ DiskOp::NewTable, "dos" };
	CHECK(refusal(d, table).contains("in use"));
	d.parts[1].mountPoints.clear();
	CHECK(refusal(d, del).isEmpty() && refusal(d, table).isEmpty());

	/* The running system's drive: never erased; its unused partitions may change. */
	d.system = true;
	CHECK(refusal(d, table).contains("running system") && refusal(d, del).isEmpty());
	d.system = false;
	/* The live system's drive: nothing at all. */
	d.live = true;
	CHECK(refusal(d, del).contains("running from"));
	d.live = false;

	/* A new partition in free space, not over another. */
	DiskOp make{ DiskOp::Create };
	make.offset = gaps[0].offset;
	make.size = gaps[0].size;
	make.fsType = "ext4";
	make.label = "Zacintosh HD";
	CHECK(refusal(d, make).isEmpty());
	make.size += MiB;
	CHECK(refusal(d, make).contains("room"));
	make.size -= MiB;
	make.fsType = "zfs";
	CHECK(refusal(d, make).contains("format"));
	make.fsType = "ext4";

	/* Resize: ext4 and NTFS, up to the next partition. */
	DiskOp grow{ DiskOp::Resize };
	grow.part = 0;
	grow.size = 60 * GiB - MiB;
	CHECK(refusal(d, grow).isEmpty());
	grow.size = 60 * GiB;
	CHECK(refusal(d, grow).contains("between"));
	d.parts[0].type = "vfat";
	grow.size = 50 * GiB;
	CHECK(refusal(d, grow).contains("can't be resized"));
	d.parts[0].type = "ntfs";

	/* Planned: the new partition, which can't be formatted until applied. */
	DiskDrive after = planned(d, make);
	CHECK(after.parts.size() == 3 && after.parts[1].label == "Zacintosh HD" && after.parts[1].block.isEmpty());
	DiskOp format{ DiskOp::Format };
	format.part = 1;
	format.fsType = "vfat";
	CHECK(refusal(after, format).contains("Apply"));

	/* What Apply erases: deleted and reformatted partitions, everything for a new table. */
	format.part = 0;
	CHECK(erasedBy(d, { make, format }) == QStringList({ "“Windows” (42.9 GB)" }));
	CHECK(erasedBy(d, { table }).size() == 2);
	CHECK(table.describe(d).contains("MBR"));
	/* The layout bar: tiny partitions still show. */
	const QList<int> widths = barWidths({ 100 * MiB, 500 * GiB, MiB * 16 }, 480);
	CHECK(widths.size() == 3 && widths[0] == 8 && widths[2] == 8 && widths[0] + widths[1] + widths[2] == 480);
	/* The running system's EFI partition stays. */
	d.parts[0].efi = true;
	d.system = true;
	DiskOp wipe{ DiskOp::Format };
	wipe.part = 0;
	wipe.fsType = "vfat";
	CHECK(refusal(d, wipe).contains("EFI"));
	std::puts("ok: Disk Setup plans partitions safely");
	return 0;
}
