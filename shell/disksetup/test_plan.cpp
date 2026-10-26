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
	d.parts[0].efi = false;
	d.system = false;

	/* Dragging: an existing partition grows to the next one; it shrinks only
	 * once its files' size is known, and never below it. */
	d.parts[1].mountPoints.clear();
	DiskSpan span = endLimits(d, 0);
	CHECK(span.least == d.parts[0].offset + d.parts[0].size && span.most == 60 * GiB);
	d.parts[0].minSize = 10 * GiB;
	span = endLimits(d, 0);
	CHECK(span.least == MiB + 10 * GiB && span.most == 60 * GiB);
	DiskOp tiny{ DiskOp::Resize };
	tiny.part = 0;
	tiny.size = 5 * GiB;
	CHECK(refusal(d, tiny).contains("files"));
	/* Size of its files not known: it can only grow, to the drive's last MiB. */
	span = endLimits(d, 1);
	CHECK(span.least == 80 * GiB && span.most == 100 * GiB - MiB);
	/* In use: it doesn't move. */
	d.parts[1].mountPoints = { "/media/data" };
	span = endLimits(d, 1);
	CHECK(span.least == span.most);
	d.parts[1].mountPoints.clear();

	/* reshape: one Resize step per partition, gone again at its old size. */
	QList<DiskOp> ops;
	CHECK(reshape(d, &ops, 0, MiB, 30 * GiB) && ops.size() == 1 && ops[0].kind == DiskOp::Resize);
	CHECK(reshape(d, &ops, 0, MiB, 35 * GiB) && ops.size() == 1 && ops[0].size == 35 * GiB);
	CHECK(!reshape(d, &ops, 0, MiB, 2 * GiB) && ops[0].size == 35 * GiB);
	CHECK(reshape(d, &ops, 0, MiB, 40 * GiB) && ops.isEmpty());
	/* A new partition: its Create step moves and changes size. */
	ops = { make };
	DiskDrive withNew = planned(d, make);
	CHECK(reshape(d, &ops, 1, make.offset + GiB, 5 * GiB) && ops.size() == 1 &&
		ops[0].offset == make.offset + GiB && ops[0].size == 5 * GiB);
	CHECK(!reshape(d, &ops, 1, make.offset, 30 * GiB)); /* over Data */
	(void)withNew;

	/* An MBR drive's extended partition: its space isn't free, it counts once. */
	DiskDrive mbr;
	mbr.device = "/dev/sdc";
	mbr.table = "dos";
	mbr.size = 100 * GiB;
	DiskPart haiku = win;
	haiku.block = "/b/sdc1";
	haiku.device = "/dev/sdc1";
	haiku.size = 30 * GiB;
	haiku.type = "befs";
	haiku.label = "Haiku";
	haiku.minSize = 0;
	mbr.parts = { haiku };
	mbr.containers = { DiskGap{ 50 * GiB, 20 * GiB } };
	const QList<DiskGap> mbrGaps = freeGaps(mbr);
	CHECK(mbrGaps.size() == 2 && mbrGaps[0].size == 20 * GiB - MiB && mbrGaps[1].offset == 70 * GiB);
	CHECK(formatName("befs") == "Haiku (BFS)");
	span = endLimits(mbr, 0);
	CHECK(span.least == span.most); /* BFS can't be resized here */

	/* Beside: BIOS on MBR needs no boot partition; ZacOS gets the largest free space. */
	BesidePlan beside = besidePlan(mbr, false, "Zacintosh HD");
	CHECK(beside.why.isEmpty() && beside.ops.size() == 1 && beside.ops[0].offset == 70 * GiB &&
		beside.ops[0].fsType == "ext4" && beside.ops[0].label == "Zacintosh HD" && beside.offset == 70 * GiB);
	/* UEFI without an EFI System partition: one first, then ZacOS after it. */
	beside = besidePlan(mbr, true, "Zacintosh HD");
	CHECK(beside.ops.size() == 2 && beside.ops[0].partType == EfiTypeMbr && beside.ops[0].size == 512 * MiB &&
		beside.offset == 70 * GiB + 512 * MiB && allowed(mbr, beside.ops));
	/* Four already: no room in the table. */
	mbr.containers << DiskGap{ 80 * GiB, GiB } << DiskGap{ 90 * GiB, GiB } << DiskGap{ 95 * GiB, GiB };
	CHECK(besidePlan(mbr, false, "Z").why.contains("as many partitions"));

	/* BIOS on GPT: GRUB's BIOS boot partition too. */
	DiskDrive gpt = d;
	gpt.parts[1].mountPoints.clear();
	beside = besidePlan(gpt, false, "Zacintosh HD");
	CHECK(beside.ops.size() == 2 && beside.ops[0].partType == BiosBootType && beside.ops[0].size == MiB &&
		beside.ops[0].offset == 40 * GiB + MiB && allowed(gpt, beside.ops));
	/* No free space big enough: room from the partition with most to spare. */
	gpt.size = 81 * GiB;
	gpt.parts[0].size = 60 * GiB - MiB; /* Windows fills up to Data */
	gpt.parts[0].minSize = 20 * GiB;
	gpt.parts[1].minSize = 19 * GiB;
	beside = besidePlan(gpt, true, "Zacintosh HD");
	CHECK(beside.why.isEmpty() && beside.ops.size() == 3 && beside.ops[0].kind == DiskOp::Resize &&
		beside.ops[0].part == 0 && beside.ops[0].size >= 20 * GiB && allowed(gpt, beside.ops));
	DiskDrive done = gpt;
	for (const DiskOp &op : beside.ops) {
		done = planned(done, op);
	}
	CHECK(done.parts.size() == 4 && done.parts[1].efi && done.parts[2].offset == beside.offset &&
		done.parts[2].size >= ZacosLeast);
	/* Files too big to make 16 GB: no room, and it says why. */
	gpt.parts[0].minSize = 50 * GiB;
	CHECK(besidePlan(gpt, true, "Z").why.contains("16 GB"));
	/* Not known yet: it can't be planned from. */
	gpt.parts[0].minSize = 0;
	CHECK(!besidePlan(gpt, true, "Z").why.isEmpty());
	std::puts("ok: Disk Setup plans partitions safely");
	return 0;
}
