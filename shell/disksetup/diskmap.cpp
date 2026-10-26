#include "diskmap.h"

#include <Qt>
#include <algorithm>
#include <climits>

#include "panelkit.h"

static constexpr int LEAST = 16; /* the smallest block: one line of text, or a grip */
static constexpr int GRAB = 3;   /* how near a line counts as on it */

uint32_t partColor(const QString &type) {
	if (type == "ext4" || type == "ext3" || type == "ext2") return RGB(0x99, 0x99, 0xFF);
	if (type == "ntfs") return RGB(0x66, 0xCC, 0xCC);
	if (type == "vfat" || type == "exfat") return RGB(0xFF, 0xCC, 0x66);
	if (type == "swap") return GRAY(0xA);
	return RGB(0xCC, 0x99, 0xCC);
}

void DiskMap::set(const DiskDrive &planned) {
	/* What's chosen, by where it starts. */
	qulonglong keep = ULLONG_MAX;
	if (const Row *r = chosen()) {
		keep = r->part >= 0 ? drive.parts[r->part].offset : gaps[r->gap].offset;
	}
	drive = planned;
	gaps = freeGaps(drive);
	rows.clear();
	int g = 0;
	for (int i = 0; i <= drive.parts.size(); i++) {
		const qulonglong before = i < drive.parts.size() ? drive.parts[i].offset : ULLONG_MAX;
		while (g < gaps.size() && gaps[g].offset < before) {
			rows << Row{ -1, g++ };
		}
		if (i < drive.parts.size()) {
			rows << Row{ i, -1 };
		}
	}
	layout();
	if (keep != ULLONG_MAX) {
		selected = -1;
		for (int i = 0; i < rows.size() && selected < 0; i++) {
			if (keep >= rowStart(i) && keep < rowEnd(i)) {
				selected = i;
			}
		}
	}
	if (selected >= rows.size()) {
		selected = rows.isEmpty() ? -1 : int(rows.size()) - 1;
	}
}

const DiskMap::Row *DiskMap::chosen() const {
	return selected >= 0 && selected < rows.size() ? &rows[selected] : nullptr;
}

int DiskMap::rowOfPart(int part) const {
	for (int i = 0; i < rows.size(); i++) {
		if (rows[i].part == part) {
			return i;
		}
	}
	return -1;
}

void DiskMap::select(int row, bool notify) {
	if (row < -1 || row >= rows.size()) {
		return;
	}
	selected = row;
	if (notify && picked) {
		picked(row);
	}
}

qulonglong DiskMap::rowStart(int row) const {
	const Row &r = rows[row];
	return r.part >= 0 ? drive.parts[r.part].offset : gaps[r.gap].offset;
}

qulonglong DiskMap::rowEnd(int row) const {
	const Row &r = rows[row];
	return r.part >= 0 ? drive.parts[r.part].offset + drive.parts[r.part].size : gaps[r.gap].offset + gaps[r.gap].size;
}

void DiskMap::layout() {
	QList<qulonglong> sizes;
	for (int i = 0; i < rows.size(); i++) {
		sizes << rowEnd(i) - rowStart(i);
	}
	m_tops.clear();
	int y = frame.top() + 1;
	for (int h : barWidths(sizes, frame.height() - 2, LEAST)) {
		m_tops << y;
		y += h;
	}
	m_tops << y;
}

int DiskMap::lineAt(QPoint p) const {
	if (!enabled || !frame.contains(p)) {
		return -1;
	}
	/* The line under each block; the last block's is the frame's bottom
	 * (a Windows filling the drive shrinks from there). */
	for (int i = 0; i < rows.size() && i + 1 < m_tops.size(); i++) {
		const int line = std::min(m_tops[i + 1] - 1, frame.bottom() - 3);
		if (std::abs(p.y() - line) <= GRAB && rows[i].part >= 0) {
			const DiskSpan s = boundaryLimits(drive, rows[i].part);
			if (s.least < s.most) {
				return rows[i].part;
			}
		}
	}
	return -1;
}

bool DiskMap::onLine(QPoint p) const {
	return m_drag >= 0 || lineAt(p) >= 0;
}

bool DiskMap::press(QPoint p) {
	if (!enabled || !frame.contains(p)) {
		return false;
	}
	const int part = lineAt(p);
	if (part >= 0) {
		const int row = rowOfPart(part);
		m_drag = part;
		m_limits = boundaryLimits(drive, part);
		m_grab = p.y() - (m_tops[row + 1] - 1);
		/* The block and what's under it, the room the line moves in. */
		int last = row;
		if (row + 1 < rows.size()) {
			const Row &next = rows[row + 1];
			const bool follows = next.gap >= 0 || (next.part >= 0 && drive.parts[next.part].block.isEmpty());
			if (follows) {
				last = row + 1;
			}
		}
		m_fromY = m_tops[row];
		m_toY = m_tops[last + 1];
		m_fromByte = rowStart(row);
		m_toByte = rowEnd(last);
		select(row, true);
		return true;
	}
	for (int i = 0; i < rows.size(); i++) {
		if (p.y() >= m_tops[i] && p.y() < m_tops[i + 1]) {
			select(i, true);
			return true;
		}
	}
	return true;
}

bool DiskMap::move(QPoint p) {
	if (m_drag < 0 || m_toY <= m_fromY) {
		return false;
	}
	const int y = std::clamp(p.y() - m_grab, m_fromY, m_toY);
	const double share = double(y - m_fromY) / double(m_toY - m_fromY);
	qulonglong at = m_fromByte + qulonglong(share * double(m_toByte - m_fromByte));
	at = (at + MiB / 2) / MiB * MiB;
	at = std::clamp(at, m_limits.least, m_limits.most);
	if (dragged) {
		dragged(m_drag, at);
	}
	return true;
}

bool DiskMap::release() {
	if (m_drag < 0) {
		return false;
	}
	m_drag = -1;
	return true;
}

bool DiskMap::key(int key) {
	if (!enabled || rows.isEmpty() || (key != Qt::Key_Up && key != Qt::Key_Down)) {
		return false;
	}
	const int row = key == Qt::Key_Up ? std::max(0, selected - 1) : std::min(int(rows.size()) - 1, selected + 1);
	select(row, row != selected);
	return true;
}

static QString blockName(const DiskPart &p) {
	return !p.label.isEmpty() ? "“" + p.label + "”" : p.efi ? QString("EFI System")
		: p.biosBoot ? QString("BIOS boot") : p.block.isEmpty() ? QString("New partition") : p.device.section('/', -1);
}

void DiskMap::paint(pl_canvas *c) const {
	const int x0 = frame.left() + 1, x1 = frame.right() - 1;
	pl_fill(c, frame.left(), frame.top(), frame.right(), frame.bottom(), C_WHITE);
	for (int i = 0; i < rows.size() && i + 1 < m_tops.size(); i++) {
		const Row &r = rows[i];
		const int y0 = m_tops[i], y1 = m_tops[i + 1] - 1;
		const DiskPart *p = r.part >= 0 ? &drive.parts[r.part] : nullptr;
		pl_fill(c, x0, y0, x1, y1, p ? partColor(p->type) : C_WHITE);
		if (y1 < frame.bottom() - 1) {
			pl_hline(c, x0, x1, y1, C_BLACK);
		}
		/* Its name, then its format and size, as many lines as fit. */
		const int h = y1 - y0;
		const int width = x1 - x0 - 12;
		QString first, second;
		if (p) {
			first = blockName(*p);
			second = formatName(p->type) + "  " + sizeText(p->size) + (p->block.isEmpty() ? "  (new)" : "")
				+ (p->inUse() ? "  in use" : "");
		} else {
			first = "Free space";
			second = sizeText(gaps[r.gap].size);
		}
		const uint32_t ink = enabled ? C_BLACK : GRAY(0x7);
		if (h >= 30) {
			panelText(c, first, x0 + 6, y0 + 14, PL_FONT_SYSTEM, ink, width);
			panelText(c, second, x0 + 6, y0 + 27, PL_FONT_VIEWS, ink, width);
		} else if (h >= LEAST - 1) {
			panelText(c, first + "  " + second, x0 + 6, y0 + (h + 9) / 2, PL_FONT_VIEWS, ink, width);
		}
		if (i == selected) {
			pl_outline(c, x0, y0, x1, y1 - (y1 < frame.bottom() - 1 ? 1 : 0), C_BLACK);
			pl_outline(c, x0 + 1, y0 + 1, x1 - 1, y1 - (y1 < frame.bottom() - 1 ? 2 : 1), C_BLACK);
		}
		/* A grip on a line that can be dragged (inside the frame for the last). */
		if (p && enabled) {
			const DiskSpan s = boundaryLimits(drive, r.part);
			if (s.least < s.most) {
				const int cx = (x0 + x1) / 2, gy = std::min(y1, frame.bottom() - 3);
				pl_fill(c, cx - 10, gy - 2, cx + 10, gy + 2, GRAY(0xD));
				pl_outline(c, cx - 10, gy - 2, cx + 10, gy + 2, C_BLACK);
				pl_hline(c, cx - 7, cx + 7, gy, GRAY(0x6));
			}
		}
	}
	pl_outline(c, frame.left(), frame.top(), frame.right(), frame.bottom(), C_BLACK);
	if (drive.size > 0 && drive.table.isEmpty() && drive.parts.isEmpty()) {
		panelText(c, "No partition table", frame.left() + 8, frame.top() + 20, PL_FONT_VIEWS);
	}
}
