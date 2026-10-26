#include "plan.h"

#include <QLocale>
#include <algorithm>

static QString bytesText(const QVariant &v) {
	QByteArray b = v.toByteArray();
	while (b.endsWith('\0')) {
		b.chop(1);
	}
	return QString::fromUtf8(b);
}

static QStringList mountPointsOf(const UDisksInterfaces &ifs) {
	const QVariant v = ifs.value("org.freedesktop.UDisks2.Filesystem").value("MountPoints");
	QStringList out;
	const QDBusArgument arg = v.value<QDBusArgument>();
	if (arg.currentType() == QDBusArgument::ArrayType) {
		arg.beginArray();
		while (!arg.atEnd()) {
			QByteArray b;
			arg >> b;
			out << QString::fromUtf8(b).remove(QChar('\0'));
		}
		arg.endArray();
	} else {
		for (const QByteArray &b : v.value<QList<QByteArray>>()) {
			out << QString::fromUtf8(b).remove(QChar('\0'));
		}
	}
	return out;
}

const QString EfiType = "c12a7328-f81f-11d2-ba4b-00a0c93ec93b", EfiTypeMbr = "0xef",
	BiosBootType = "21686148-6449-6e6f-744e-656564454649";

QString sizeText(qulonglong bytes) {
	return QLocale().formattedDataSize(qint64(bytes), 1, QLocale::DataSizeSIFormat);
}

QList<DiskDrive> parseDrives(const UDisksObjects &objects) {
	QList<DiskDrive> drives;
	/* Whole disks: a block with a drive, not itself a partition. */
	for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
		const UDisksInterfaces &ifs = it.value();
		if (!ifs.contains("org.freedesktop.UDisks2.Block") || ifs.contains("org.freedesktop.UDisks2.Partition")) {
			continue;
		}
		const QVariantMap b = ifs.value("org.freedesktop.UDisks2.Block");
		const QString drivePath = b.value("Drive").value<QDBusObjectPath>().path();
		if (drivePath.isEmpty() || drivePath == "/" || b.value("Size").toULongLong() == 0 ||
				b.value("HintIgnore").toBool() || bytesText(b.value("Device")).startsWith("/dev/loop")) {
			continue;
		}
		DiskDrive d;
		d.drive = drivePath;
		d.block = it.key().path();
		d.device = bytesText(b.value("Device"));
		d.size = b.value("Size").toULongLong();
		const QVariantMap drive = objects.value(QDBusObjectPath(drivePath)).value("org.freedesktop.UDisks2.Drive");
		d.model = (drive.value("Vendor").toString() + ' ' + drive.value("Model").toString()).trimmed();
		if (ifs.contains("org.freedesktop.UDisks2.PartitionTable")) {
			d.table = ifs.value("org.freedesktop.UDisks2.PartitionTable").value("Type").toString();
		}
		/* A whole disk with a filesystem and no table (a USB stick, say). */
		const QStringList wholeMounts = mountPointsOf(ifs);
		if (!wholeMounts.isEmpty() || b.value("IdType").toString() == "iso9660") {
			d.live = d.live || wholeMounts.contains("/run/live/medium") || b.value("IdType").toString() == "iso9660";
			d.system = d.system || wholeMounts.contains("/");
		}
		drives << d;
	}
	/* Their partitions. */
	for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
		const UDisksInterfaces &ifs = it.value();
		if (!ifs.contains("org.freedesktop.UDisks2.Partition")) {
			continue;
		}
		const QVariantMap p = ifs.value("org.freedesktop.UDisks2.Partition");
		const QString table = p.value("Table").value<QDBusObjectPath>().path();
		auto d = std::find_if(drives.begin(), drives.end(), [&](const DiskDrive &x) { return x.block == table; });
		if (d == drives.end()) {
			continue;
		}
		if (p.value("IsContainer").toBool()) {
			d->containers << DiskGap{ p.value("Offset").toULongLong(), p.value("Size").toULongLong() };
			continue;
		}
		const QVariantMap b = ifs.value("org.freedesktop.UDisks2.Block");
		DiskPart part;
		part.block = it.key().path();
		part.device = bytesText(b.value("Device"));
		part.number = p.value("Number").toInt();
		part.offset = p.value("Offset").toULongLong();
		part.size = p.value("Size").toULongLong();
		part.type = b.value("IdType").toString();
		part.label = b.value("IdLabel").toString();
		part.mountPoints = mountPointsOf(ifs);
		const QString kind = p.value("Type").toString().toLower();
		part.efi = kind == EfiType || kind == EfiTypeMbr;
		part.biosBoot = kind == BiosBootType;
		part.logical = p.value("IsContained").toBool();
		d->live = d->live || part.mountPoints.contains("/run/live/medium") || part.type == "iso9660";
		d->system = d->system || part.mountPoints.contains("/");
		d->parts << part;
	}
	for (DiskDrive &d : drives) {
		std::sort(d.parts.begin(), d.parts.end(), [](const DiskPart &a, const DiskPart &b) { return a.offset < b.offset; });
	}
	std::sort(drives.begin(), drives.end(), [](const DiskDrive &a, const DiskDrive &b) { return a.device < b.device; });
	return drives;
}

QList<DiskGap> freeGaps(const DiskDrive &drive) {
	QList<DiskGap> gaps;
	if (drive.table.isEmpty()) {
		return gaps;
	}
	const qulonglong end = drive.size >= 2 * MiB ? drive.size - MiB : 0;
	qulonglong at = MiB;
	auto gap = [&](qulonglong from, qulonglong to) {
		from = (from + MiB - 1) / MiB * MiB;
		to = to / MiB * MiB;
		if (to > from && to - from >= 8 * MiB) {
			gaps << DiskGap{ from, to - from };
		}
	};
	QList<DiskGap> used = drive.containers;
	for (const DiskPart &p : drive.parts) {
		used << DiskGap{ p.offset, p.size };
	}
	std::sort(used.begin(), used.end(), [](const DiskGap &a, const DiskGap &b) { return a.offset < b.offset; });
	for (const DiskGap &u : used) {
		if (u.offset > at) {
			gap(at, u.offset);
		}
		at = std::max(at, u.offset + u.size);
	}
	if (end > at) {
		gap(at, end);
	}
	return gaps;
}

QStringList diskFormats() {
	return { "ext4", "vfat", "exfat", "ntfs" };
}

bool resizable(const QString &fsType) {
	return fsType == "ext2" || fsType == "ext3" || fsType == "ext4" || fsType == "ntfs";
}

QString formatName(const QString &type) {
	return type == "vfat" ? "FAT32" : type == "exfat" ? "exFAT" : type == "ntfs" ? "NTFS"
		: type == "ext4" ? "ZacOS (ext4)" : type == "befs" ? "Haiku (BFS)"
		: type == "hfsplus" ? "Mac OS Extended" : type == "apfs" ? "APFS"
		: type.isEmpty() || type == "empty" ? QString("unformatted") : type;
}

static QString partName(const DiskPart &p) {
	const QString name = !p.label.isEmpty() ? "“" + p.label + "”" : p.efi ? QString("the EFI System partition")
		: p.biosBoot ? QString("the BIOS boot partition") : p.device.section('/', -1);
	return name.isEmpty() ? QString("the new partition") : name;
}

QString DiskOp::describe(const DiskDrive &d) const {
	const DiskPart *p = part >= 0 && part < d.parts.size() ? &d.parts[part] : nullptr;
	switch (kind) {
	case NewTable:
		return QString("Erase %1 and give it a new %2 partition table").arg(d.device, tableType == "dos" ? "MBR" : "GPT");
	case Delete:
		return QString("Delete %1 (%2)").arg(p ? partName(*p) : "?", p ? sizeText(p->size) : "");
	case Create:
		if (partType == BiosBootType) {
			return QString("Make a %1 BIOS boot partition, for starting up with GRUB").arg(sizeText(size));
		}
		if (partType == EfiType || partType == EfiTypeMbr) {
			return QString("Make a %1 EFI System partition, for starting up").arg(sizeText(size));
		}
		return QString("Make a %1 %2 partition%3").arg(sizeText(size), formatName(fsType),
			label.isEmpty() ? QString() : " named “" + label + "”");
	case Format:
		return QString("Erase %1 as %2%3").arg(p ? partName(*p) : "?", formatName(fsType),
			label.isEmpty() ? QString() : ", named “" + label + "”");
	case Resize:
		return QString("Resize %1 to %2").arg(p ? partName(*p) : "?", sizeText(size));
	}
	return {};
}

/* An MBR drive's four: its partitions, an extended one counted once. */
static int primaries(const DiskDrive &d) {
	int n = int(d.containers.size());
	for (const DiskPart &p : d.parts) {
		n += p.logical ? 0 : 1;
	}
	return n;
}

QString refusal(const DiskDrive &d, const DiskOp &op) {
	if (d.live) {
		return "ZacOS is running from this drive, so it can't be changed.";
	}
	const DiskPart *p = op.part >= 0 && op.part < d.parts.size() ? &d.parts[op.part] : nullptr;
	switch (op.kind) {
	case DiskOp::NewTable:
		if (d.system) {
			return "The running system is on this drive, so it can't be erased.";
		}
		for (const DiskPart &x : d.parts) {
			if (x.inUse()) {
				return QString("%1 is in use. Eject it first.").arg(partName(x));
			}
		}
		return op.tableType == "gpt" || op.tableType == "dos" ? QString() : QString("Choose GPT or MBR.");
	case DiskOp::Delete:
	case DiskOp::Format:
	case DiskOp::Resize:
		if (!p) {
			return "Choose a partition.";
		}
		if (p->block.isEmpty() && op.kind != DiskOp::Delete) {
			return "Apply the changes so far first.";
		}
		if (p->inUse()) {
			return QString("%1 is in use. Eject it first.").arg(partName(*p));
		}
		if (p->efi && d.system && op.kind != DiskOp::Resize) {
			return "The running system starts up from the EFI System partition, so it can't be deleted or erased.";
		}
		if (op.kind == DiskOp::Format && !diskFormats().contains(op.fsType)) {
			return "Choose a format.";
		}
		if (op.kind == DiskOp::Resize) {
			if (!resizable(p->type)) {
				return QString("%1 can't be resized: only ZacOS (ext4) and NTFS partitions can.").arg(partName(*p));
			}
			const DiskGap around = roomAround(d, op.part);
			const qulonglong room = around.offset + around.size;
			if (op.size < 16 * MiB || p->offset + op.size > room) {
				return QString("Choose a size between 16 MB and %1.").arg(sizeText(room - p->offset));
			}
			if (p->minSize > 0 && op.size < p->minSize) {
				return QString("%1 can't be made smaller than %2: its files need that much.")
					.arg(partName(*p), sizeText(p->minSize));
			}
		}
		return {};
	case DiskOp::Create: {
		if (d.table.isEmpty()) {
			return "Give the drive a partition table first.";
		}
		if (!diskFormats().contains(op.fsType) && !(op.fsType == "empty" && op.partType == BiosBootType)) {
			return "Choose a format.";
		}
		if (!op.partType.isEmpty() && op.partType != (d.table == "gpt" ? EfiType : EfiTypeMbr) &&
				!(op.partType == BiosBootType && d.table == "gpt")) {
			return "That kind of partition can't go on this drive.";
		}
		if (d.table == "dos" && primaries(d) >= 4) {
			return "An MBR drive holds four partitions at most.";
		}
		for (const DiskGap &g : freeGaps(d)) {
			if (op.offset >= g.offset && op.offset + op.size <= g.offset + g.size && op.size >= MiB) {
				return {};
			}
		}
		return "There isn't room for that partition there.";
	}
	}
	return {};
}

DiskDrive planned(DiskDrive d, const DiskOp &op) {
	switch (op.kind) {
	case DiskOp::NewTable:
		d.table = op.tableType;
		d.parts.clear();
		break;
	case DiskOp::Delete:
		d.parts.removeAt(op.part);
		break;
	case DiskOp::Create: {
		DiskPart p;
		p.offset = op.offset;
		p.size = op.size;
		p.type = op.fsType;
		p.label = op.label;
		p.efi = op.partType == EfiType || op.partType == EfiTypeMbr;
		p.biosBoot = op.partType == BiosBootType;
		d.parts << p;
		std::sort(d.parts.begin(), d.parts.end(), [](const DiskPart &a, const DiskPart &b) { return a.offset < b.offset; });
		break;
	}
	case DiskOp::Format:
		d.parts[op.part].type = op.fsType;
		d.parts[op.part].label = op.label;
		break;
	case DiskOp::Resize:
		d.parts[op.part].size = op.size;
		break;
	}
	return d;
}

QList<int> barWidths(const QList<qulonglong> &sizes, int width, int least) {
	QList<int> out;
	if (sizes.isEmpty()) {
		return out;
	}
	least = std::min(least, width / int(sizes.size()));
	/* Pieces too small for `least` get it; the rest share what's left. */
	QList<bool> small(sizes.size(), false);
	for (bool changed = true; changed;) {
		changed = false;
		qulonglong big = 0;
		int smallCount = 0;
		for (int i = 0; i < sizes.size(); i++) {
			if (small[i]) { smallCount++; } else { big += sizes[i]; }
		}
		const int room = width - smallCount * least;
		for (int i = 0; i < sizes.size(); i++) {
			if (!small[i] && big > 0 && double(sizes[i]) / double(big) * room < least) {
				small[i] = changed = true;
			}
		}
	}
	qulonglong big = 0;
	int smallCount = 0;
	for (int i = 0; i < sizes.size(); i++) {
		if (small[i]) { smallCount++; } else { big += sizes[i]; }
	}
	const int room = width - smallCount * least;
	int used = 0, last = -1;
	for (int i = 0; i < sizes.size(); i++) {
		const int w = small[i] ? least : big > 0 ? int(double(sizes[i]) / double(big) * room) : least;
		out << w;
		used += w;
		if (!small[i]) {
			last = i;
		}
	}
	out[last >= 0 ? last : out.size() - 1] += width - used; /* rounding goes to a big piece */
	return out;
}

QStringList erasedBy(const DiskDrive &start, const QList<DiskOp> &ops) {
	QStringList out;
	DiskDrive d = start;
	for (const DiskOp &op : ops) {
		const DiskPart *p = op.part >= 0 && op.part < d.parts.size() ? &d.parts[op.part] : nullptr;
		if (op.kind == DiskOp::NewTable) {
			for (const DiskPart &x : d.parts) {
				if (!x.block.isEmpty()) {
					out << QString("%1 (%2)").arg(partName(x), sizeText(x.size));
				}
			}
			if (d.parts.isEmpty() && !d.table.isEmpty()) {
				out << "the partition table of " + d.device;
			}
		} else if ((op.kind == DiskOp::Delete || op.kind == DiskOp::Format) && p && !p->block.isEmpty()) {
			out << QString("%1 (%2)").arg(partName(*p), sizeText(p->size));
		}
		d = planned(d, op);
	}
	out.removeDuplicates();
	return out;
}

DiskGap roomAround(const DiskDrive &d, int part) {
	const DiskPart &p = d.parts[part];
	qulonglong from = MiB, to = d.size >= 2 * MiB ? d.size - MiB : 0;
	QList<DiskGap> used;
	for (int i = 0; i < d.parts.size(); i++) {
		if (i != part) {
			used << DiskGap{ d.parts[i].offset, d.parts[i].size };
		}
	}
	for (const DiskGap &c : d.containers) {
		if (p.offset >= c.offset && p.offset < c.offset + c.size) {
			from = std::max(from, c.offset); /* a logical partition stays in its extended one */
			to = std::min(to, c.offset + c.size);
		} else {
			used << c;
		}
	}
	for (const DiskGap &u : used) {
		if (u.offset + u.size <= p.offset) {
			from = std::max(from, u.offset + u.size);
		} else if (u.offset >= p.offset) {
			to = std::min(to, u.offset);
		}
	}
	from = (from + MiB - 1) / MiB * MiB;
	to = to / MiB * MiB;
	return DiskGap{ from, to > from ? to - from : 0 };
}

DiskSpan endLimits(const DiskDrive &d, int part) {
	const DiskPart &p = d.parts[part];
	const qulonglong end = p.offset + p.size;
	const DiskSpan fixed{ end, end };
	if (d.live || p.logical || p.efi || p.biosBoot || p.inUse()) {
		return fixed;
	}
	const DiskGap room = roomAround(d, part);
	DiskSpan span;
	if (p.block.isEmpty()) {
		span.least = p.offset + 8 * MiB;
	} else if (resizable(p.type)) {
		/* Smaller only once it's known how small its files let it be. */
		span.least = p.offset + std::max(16 * MiB, p.minSize > 0 ? p.minSize : p.size);
	} else {
		return fixed;
	}
	span.least = (span.least + MiB - 1) / MiB * MiB;
	span.most = (room.offset + room.size) / MiB * MiB;
	/* Rounding up never puts the least past where it ends now. */
	span.least = std::min(span.least, std::max(end, p.offset + 8 * MiB));
	return span.least <= span.most ? span : fixed;
}

bool allowed(const DiskDrive &start, const QList<DiskOp> &ops) {
	DiskDrive d = start;
	for (const DiskOp &op : ops) {
		if (!refusal(d, op).isEmpty()) {
			return false;
		}
		d = planned(d, op);
	}
	return true;
}

bool reshape(const DiskDrive &start, QList<DiskOp> *ops, int part, qulonglong offset, qulonglong size) {
	DiskDrive d = start;
	for (const DiskOp &op : *ops) {
		d = planned(d, op);
	}
	if (part < 0 || part >= d.parts.size()) {
		return false;
	}
	const DiskPart p = d.parts[part];
	QList<DiskOp> changed = *ops;
	if (p.block.isEmpty()) {
		/* New: its Create step, found by where it starts. */
		int at = -1;
		for (int i = 0; i < changed.size(); i++) {
			if (changed[i].kind == DiskOp::Create && changed[i].offset == p.offset) {
				at = i;
			}
		}
		if (at < 0) {
			return false;
		}
		changed[at].offset = offset;
		changed[at].size = size;
	} else {
		if (offset != p.offset) {
			return false; /* UDisks resizes from the end only */
		}
		/* Its Resize step, if it has one, found by its block as each step leaves the drive. */
		DiskDrive at = start;
		int found = -1;
		for (int i = 0; i < changed.size(); i++) {
			const DiskOp &op = changed[i];
			if (op.kind == DiskOp::Resize && op.part >= 0 && op.part < at.parts.size() &&
					at.parts[op.part].block == p.block) {
				found = i;
			}
			at = planned(at, op);
		}
		qulonglong was = 0;
		for (const DiskPart &x : start.parts) {
			if (x.block == p.block) {
				was = x.size;
			}
		}
		if (found >= 0 && size == was) {
			changed.removeAt(found);
		} else if (found >= 0) {
			changed[found].size = size;
		} else if (size != p.size) {
			DiskOp grow{ DiskOp::Resize };
			grow.part = part;
			grow.size = size;
			changed << grow;
		}
	}
	if (!allowed(start, changed)) {
		return false;
	}
	*ops = changed;
	return true;
}

BesidePlan besidePlan(const DiskDrive &d, bool uefi, const QString &name) {
	BesidePlan plan;
	if (d.live) {
		plan.why = "ZacOS is running from this drive, so it can't be installed on it.";
		return plan;
	}
	if (d.table.isEmpty()) {
		plan.why = "This drive has no partition table, so ZacOS 9 can only go on it by erasing it.";
		return plan;
	}
	/* The boot partition this computer starts up from, if the drive hasn't one. */
	DiskOp boot{ DiskOp::Create };
	bool needBoot = false;
	if (uefi) {
		needBoot = std::none_of(d.parts.begin(), d.parts.end(), [](const DiskPart &p) { return p.efi; });
		boot.size = 512 * MiB;
		boot.fsType = "vfat";
		boot.label = "EFI";
		boot.partType = d.table == "gpt" ? EfiType : EfiTypeMbr;
	} else if (d.table == "gpt") {
		needBoot = std::none_of(d.parts.begin(), d.parts.end(), [](const DiskPart &p) { return p.biosBoot; });
		boot.size = MiB;
		boot.fsType = "empty";
		boot.partType = BiosBootType;
	}
	const qulonglong bootSize = needBoot ? boot.size : 0;
	if (d.table == "dos" && primaries(d) + 1 + (needBoot ? 1 : 0) > 4) {
		plan.why = "This drive already has as many partitions as an MBR drive can hold. "
			"Delete one first, or erase the whole disk.";
		return plan;
	}
	const qulonglong wanted = bootSize + ZacosLeast;
	/* The largest free space: someone made it for ZacOS, so all of it. */
	DiskGap best;
	for (const DiskGap &g : freeGaps(d)) {
		if (g.size > best.size) {
			best = g;
		}
	}
	qulonglong from = 0, to = 0;
	if (best.size >= wanted) {
		from = best.offset;
		to = best.offset + best.size;
	} else {
		/* Else room from the end of the partition with the most to spare. */
		int shrink = -1;
		qulonglong spare = 0, room = 0;
		for (int i = 0; i < d.parts.size(); i++) {
			const DiskPart &p = d.parts[i];
			if (p.block.isEmpty() || !resizable(p.type) || p.inUse() || p.logical || p.minSize == 0) {
				continue;
			}
			const DiskGap around = roomAround(d, i);
			const qulonglong end = around.offset + around.size;
			/* Keep a tenth again of its files' room, and at least 2 GB, for it to work in. */
			const qulonglong keep = p.minSize + std::max(p.minSize / 10, qulonglong(2000) * MiB);
			const qulonglong free = end > p.offset + keep ? end - (p.offset + keep) : 0;
			if (free > spare) {
				shrink = i;
				spare = free;
				room = end;
			}
		}
		if (shrink < 0 || spare < wanted) {
			plan.why = "There isn't room for ZacOS 9 on this drive: it needs 16 GB. Free some space first, "
				"in the other system or in Disk Setup, or erase the whole disk.";
			return plan;
		}
		const qulonglong give = std::max(std::min(spare, ZacosRoomy + bootSize), spare / 2) / MiB * MiB;
		DiskOp resize{ DiskOp::Resize };
		resize.part = shrink;
		resize.size = (room - give) / MiB * MiB - d.parts[shrink].offset;
		plan.ops << resize;
		from = d.parts[shrink].offset + resize.size;
		to = room;
	}
	const bool bootAfter = !plan.ops.isEmpty(); /* room taken from a partition */
	DiskOp zacos{ DiskOp::Create };
	zacos.offset = bootAfter ? from : from + bootSize;
	zacos.size = (to - from - bootSize) / MiB * MiB;
	zacos.fsType = "ext4";
	zacos.label = name;
	if (needBoot) {
		boot.offset = bootAfter ? zacos.offset + zacos.size : from;
	}
	if (needBoot && !bootAfter) {
		plan.ops << boot;
	}
	plan.ops << zacos;
	if (needBoot && bootAfter) {
		plan.ops << boot;
	}
	plan.offset = zacos.offset;
	if (!allowed(d, plan.ops)) {
		plan.ops.clear();
		plan.why = "There isn't room for ZacOS 9 on this drive. Free some space first, or erase the whole disk.";
	}
	return plan;
}

/* The new partition right after `part`, whose start moves with its line, or -1. */
static int follower(const DiskDrive &d, int part) {
	const int next = part + 1;
	if (next >= d.parts.size()) {
		return -1;
	}
	const DiskPart &p = d.parts[part], &n = d.parts[next];
	return n.block.isEmpty() && !n.efi && !n.biosBoot && n.offset == p.offset + p.size ? next : -1;
}

DiskSpan boundaryLimits(const DiskDrive &d, int part) {
	DiskSpan span = endLimits(d, part);
	const int next = follower(d, part);
	/* One that can't change itself holds the line where it is. */
	if (next >= 0 && span.least < span.most) {
		/* Bigger into the new one, keeping 8 MiB of it. */
		const DiskPart &n = d.parts[next];
		span.most = std::max(span.most, (n.offset + n.size - 8 * MiB) / MiB * MiB);
	}
	return span;
}

bool moveBoundary(const DiskDrive &start, QList<DiskOp> *ops, int part, qulonglong at) {
	DiskDrive d = start;
	for (const DiskOp &op : *ops) {
		d = planned(d, op);
	}
	if (part < 0 || part >= d.parts.size()) {
		return false;
	}
	const DiskPart p = d.parts[part];
	const qulonglong end = p.offset + p.size;
	if (at == end) {
		return true;
	}
	if (at < p.offset + MiB) {
		return false;
	}
	const int next = follower(d, part);
	QList<DiskOp> changed = *ops;
	if (next < 0) {
		if (!reshape(start, &changed, part, p.offset, at - p.offset)) {
			return false;
		}
	} else {
		const DiskPart n = d.parts[next];
		const qulonglong nextEnd = n.offset + n.size;
		if (at + MiB > nextEnd) {
			return false;
		}
		/* The one that makes room goes first, so no step overlaps the other. */
		if (at > end) {
			if (!reshape(start, &changed, next, at, nextEnd - at) ||
					!reshape(start, &changed, part, p.offset, at - p.offset)) {
				return false;
			}
		} else if (!reshape(start, &changed, part, p.offset, at - p.offset) ||
				!reshape(start, &changed, next, at, nextEnd - at)) {
			return false;
		}
	}
	*ops = changed;
	return true;
}
