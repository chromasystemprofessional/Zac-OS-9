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
		if (d == drives.end() || p.value("IsContainer").toBool()) {
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
		part.efi = kind == "c12a7328-f81f-11d2-ba4b-00a0c93ec93b" || kind == "0xef";
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
	for (const DiskPart &p : drive.parts) {
		if (p.offset > at) {
			gap(at, p.offset);
		}
		at = std::max(at, p.offset + p.size);
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

static QString formatName(const QString &type) {
	return type == "vfat" ? "FAT32" : type == "exfat" ? "exFAT" : type == "ntfs" ? "NTFS"
		: type == "ext4" ? "ZacOS (ext4)" : type.isEmpty() ? QString("unformatted") : type;
}

static QString partName(const DiskPart &p) {
	const QString name = !p.label.isEmpty() ? "“" + p.label + "”" : p.efi ? QString("the EFI System partition")
		: p.device.section('/', -1);
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
			qulonglong room = d.size - MiB;
			for (const DiskPart &x : d.parts) {
				if (x.offset > p->offset) {
					room = std::min(room, x.offset);
				}
			}
			if (op.size < 16 * MiB || p->offset + op.size > room) {
				return QString("Choose a size between 16 MB and %1.").arg(sizeText(room - p->offset));
			}
		}
		return {};
	case DiskOp::Create: {
		if (d.table.isEmpty()) {
			return "Give the drive a partition table first.";
		}
		if (!diskFormats().contains(op.fsType)) {
			return "Choose a format.";
		}
		if (d.table == "dos" && d.parts.size() >= 4) {
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
