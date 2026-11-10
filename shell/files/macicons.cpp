#include "macicons.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStringList>
#include <array>

#include <algorithm>

/*
 * Mac OS 9 stored a custom icon in a resource fork, which Unix filesystems
 * cannot hold. ustar, unar and macOS write it beside the file as an
 * AppleDouble "._" companion instead; Netatalk uses ".rsrc"; an HFS mount
 * exposes the fork as "..namedfork/rsrc". The icon resources themselves
 * are plain QuickDraw data:
 *
 *   icl8/ics8   8-bit indices into the standard Mac 256-colour table
 *   icl4/ics4   4-bit indices into the standard 16-colour table
 *   ICN#/ics#   a 1-bit bitmap, then its 1-bit opacity mask
 *
 * The colour resources have no palette of their own: they index the
 * system's standard tables. Those are generated below; no Apple data is
 * shipped, and this file only reads the user's own image.
 */

static constexpr qint64 MAX_FORK = 8 * 1024 * 1024;
static constexpr int ICON_SIZE = 32;
static constexpr int SMALL_ICON_SIZE = 16;

static quint16 be16(const QByteArray &data, int at) {
	return static_cast<quint16>(static_cast<uchar>(data[at]) << 8) |
		static_cast<uchar>(data[at + 1]);
}

static quint32 be32(const QByteArray &data, int at) {
	return static_cast<quint32>(static_cast<uchar>(data[at])) << 24 |
		static_cast<quint32>(static_cast<uchar>(data[at + 1])) << 16 |
		static_cast<quint32>(static_cast<uchar>(data[at + 2])) << 8 |
		static_cast<uchar>(data[at + 3]);
}

/* The standard Mac 256-colour system table (clut -1/8): a 6x6x6 cube of
 * 51-step components with white at 0, the primaries and grays after it,
 * and black last. */
static QRgb mac8Colour(int index) {
	static const std::array<QRgb, 256> colours = [] {
		std::array<QRgb, 256> table{};
		for (int i = 0; i < 215; i++) {
			const int r = 5 - i / 36;
			const int g = 5 - (i / 6) % 6;
			const int b = 5 - i % 6;
			table[i] = qRgb(r * 51, g * 51, b * 51);
		}
		static const int shades[] = { 0xEE, 0xDD, 0xBB, 0xAA, 0x88,
			0x77, 0x55, 0x44, 0x22, 0x11 };
		for (int i = 0; i < 10; i++) {
			table[215 + i] = qRgb(shades[i], 0, 0);
			table[225 + i] = qRgb(0, shades[i], 0);
			table[235 + i] = qRgb(0, 0, shades[i]);
			table[245 + i] = qRgb(shades[i], shades[i], shades[i]);
		}
		table[255] = qRgb(0, 0, 0);
		return table;
	}();
	return colours[static_cast<uchar>(index)];
}

/* The standard 16-colour Mac palette, used by icl4/ics4 and the System's
 * black-and-white icon masks. */
static QRgb mac4Colour(int index) {
	static const std::array<QRgb, 16> colours = {
		qRgb(255, 255, 255), qRgb(255, 255, 0), qRgb(255, 102, 0),
		qRgb(221, 0, 0), qRgb(255, 0, 153), qRgb(51, 0, 153),
		qRgb(0, 0, 221), qRgb(0, 153, 255), qRgb(0, 187, 0),
		qRgb(0, 102, 0), qRgb(102, 51, 0), qRgb(153, 102, 51),
		qRgb(204, 204, 204), qRgb(136, 136, 136), qRgb(68, 68, 68),
		qRgb(0, 0, 0),
	};
	return colours[index & 0x0F];
}

/* The resource fork inside an AppleDouble companion (magic 0x00051607),
 * or empty when there is none. */
static QByteArray appleDoubleFork(const QByteArray &data) {
	if (data.size() < 26) {
		return {};
	}
	if (data.left(4) != QByteArray("\x00\x05\x16\x07", 4) ||
			data.mid(4, 4) != QByteArray("\x00\x02\x00\x00", 4)) {
		return {};
	}
	const int count = be16(data, 24);
	const int tableEnd = 26 + 12 * count;
	if (count > 1024 || tableEnd > data.size()) {
		return {};
	}
	for (int i = 0; i < count; i++) {
		const int at = 26 + 12 * i;
		const quint32 kind = be32(data, at);
		const quint32 offset = be32(data, at + 4);
		const quint32 length = be32(data, at + 8);
		if (kind != 2) {
			continue;
		}
		if (offset < static_cast<quint32>(tableEnd) ||
				static_cast<qint64>(offset) + length > data.size()) {
			return {};
		}
		return data.mid(static_cast<int>(offset), static_cast<int>(length));
	}
	return {};
}

static bool looksLikeResourceFork(const QByteArray &data) {
	if (data.size() < 16) {
		return false;
	}
	const quint32 dataOffset = be32(data, 0);
	const quint32 mapOffset = be32(data, 4);
	const quint32 dataLength = be32(data, 8);
	const quint32 mapLength = be32(data, 12);
	if (mapOffset == 0 || mapLength < 28 || dataOffset < 16 ||
			static_cast<qint64>(dataOffset) + dataLength > data.size() ||
			static_cast<qint64>(mapOffset) + mapLength > data.size()) {
		return false;
	}
	return true;
}

/* A path's fork, whether a raw resource fork (an HFS named fork) or an
 * AppleDouble companion. Empty data files, such as unar's "Icon\r", are
 * cheap and simply yield nothing. */
static QByteArray readFork(const QString &path) {
	const QFileInfo info(path);
	if (!info.exists() || info.isDir() || info.isSymLink() ||
			info.size() <= 0 || info.size() > MAX_FORK) {
		return {};
	}
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	const QByteArray data = file.read(MAX_FORK + 1);
	if (data.size() > MAX_FORK) {
		return {};
	}
	const QByteArray fork = appleDoubleFork(data);
	if (!fork.isEmpty()) {
		return fork;
	}
	return looksLikeResourceFork(data) ? data : QByteArray();
}

/* The icon resources in a fork, by type, keeping the first of each. The
 * map is followed defensively: a corrupt one must not copy a large body
 * once per type. */
static QHash<QByteArray, QByteArray> iconResources(const QByteArray &fork) {
	QHash<QByteArray, QByteArray> icons;
	if (fork.size() < 16) {
		return icons;
	}
	const quint32 dataOffset = be32(fork, 0);
	const quint32 mapOffset = be32(fork, 4);
	const quint32 dataLength = be32(fork, 8);
	const quint32 mapLength = be32(fork, 12);
	if (mapOffset < 16 || mapLength < 28 ||
			static_cast<qint64>(dataOffset) + dataLength > fork.size() ||
			static_cast<qint64>(mapOffset) + mapLength > fork.size()) {
		return icons;
	}
	const int typeList = static_cast<int>(mapOffset) + be16(fork, static_cast<int>(mapOffset) + 24);
	const qint64 mapEnd = static_cast<qint64>(mapOffset) + mapLength;
	if (typeList + 2 > mapEnd) {
		return icons;
	}
	const int types = be16(fork, typeList) + 1;
	if (types > 4096) {
		return icons;
	}
	int at = typeList + 2;
	for (int t = 0; t < types; t++) {
		if (at + 8 > mapEnd) {
			break;
		}
		const QByteArray type = fork.mid(at, 4);
		const int count = be16(fork, at + 4) + 1;
		const int referenceList = typeList + be16(fork, at + 6);
		at += 8;
		if (count > 20000 || referenceList < typeList ||
				referenceList + 12 * count > mapEnd) {
			continue;
		}
		for (int i = 0; i < count; i++) {
			const int reference = referenceList + 12 * i;
			const quint32 offset = dataOffset +
				((static_cast<quint32>(static_cast<uchar>(fork[reference + 5])) << 16) |
				 (static_cast<quint32>(static_cast<uchar>(fork[reference + 6])) << 8) |
				 static_cast<uchar>(fork[reference + 7]));
			if (static_cast<qint64>(offset) + 4 > fork.size()) {
				continue;
			}
			const quint32 length = be32(fork, static_cast<int>(offset));
			if (static_cast<qint64>(offset) + 4 + length > fork.size()) {
				continue;
			}
			if (!icons.contains(type)) {
				icons.insert(type, fork.mid(static_cast<int>(offset) + 4,
					static_cast<int>(length)));
			}
		}
	}
	return icons;
}

static bool maskBit(const QByteArray &mask, int size, int x, int y) {
	const int rowBytes = size / 8;
	const int at = size * rowBytes + y * rowBytes + x / 8;
	return at < mask.size() && (static_cast<uchar>(mask[at]) & (0x80 >> (x % 8)));
}

/* 8- or 4-bit colour pixels; the mask says which are opaque. */
static QImage decodeColourIcon(const QByteArray &body, const QByteArray &mask,
		int size, bool eightBit) {
	const int needed = eightBit ? size * size : size * size / 2;
	if (body.size() < needed) {
		return {};
	}
	QImage image(size, size, QImage::Format_ARGB32);
	image.fill(Qt::transparent);
	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			int index;
			if (eightBit) {
				index = static_cast<uchar>(body[y * size + x]);
			} else {
				const uchar byte = static_cast<uchar>(body[y * (size / 2) + x / 2]);
				index = x % 2 == 0 ? byte >> 4 : byte & 0x0F;
			}
			if (!mask.isEmpty() && !maskBit(mask, size, x, y)) {
				continue;
			}
			image.setPixel(x, y, eightBit ? mac8Colour(index) : mac4Colour(index));
		}
	}
	return image;
}

/* A 1-bit icon: black where the bitmap bit is set, white elsewhere, with
 * the mask deciding opacity. */
static QImage decodeMonoIcon(const QByteArray &body, int size) {
	const int rowBytes = size / 8;
	const int bitmapBytes = rowBytes * size;
	if (body.size() < bitmapBytes + rowBytes * size) {
		return {};
	}
	QImage image(size, size, QImage::Format_ARGB32);
	image.fill(Qt::transparent);
	for (int y = 0; y < size; y++) {
		for (int x = 0; x < size; x++) {
			const int at = y * rowBytes + x / 8;
			const uchar bit = 0x80 >> (x % 8);
			const bool ink = static_cast<uchar>(body[at]) & bit;
			const bool opaque = static_cast<uchar>(body[bitmapBytes + at]) & bit;
			if (opaque) {
				image.setPixel(x, y, ink ? qRgb(0, 0, 0) : qRgb(255, 255, 255));
			}
		}
	}
	return image;
}

static QImage decodeIconFork(const QByteArray &fork) {
	const QHash<QByteArray, QByteArray> icons = iconResources(fork);
	if (icons.isEmpty()) {
		return {};
	}
	QImage image;
	if (icons.contains("icl8")) {
		image = decodeColourIcon(icons.value("icl8"), icons.value("ICN#"),
			ICON_SIZE, true);
	}
	if (image.isNull() && icons.contains("icl4")) {
		image = decodeColourIcon(icons.value("icl4"), icons.value("ICN#"),
			ICON_SIZE, false);
	}
	if (image.isNull() && icons.contains("ICN#")) {
		image = decodeMonoIcon(icons.value("ICN#"), ICON_SIZE);
	}
	if (image.isNull() && icons.contains("ics8")) {
		image = decodeColourIcon(icons.value("ics8"), icons.value("ics#"),
			SMALL_ICON_SIZE, true);
	}
	if (image.isNull() && icons.contains("ics4")) {
		image = decodeColourIcon(icons.value("ics4"), icons.value("ics#"),
			SMALL_ICON_SIZE, false);
	}
	if (image.isNull() && icons.contains("ics#")) {
		image = decodeMonoIcon(icons.value("ics#"), SMALL_ICON_SIZE);
	}
	if (image.isNull()) {
		return {};
	}
	if (image.width() != ICON_SIZE) {
		image = image.scaled(ICON_SIZE, ICON_SIZE, Qt::IgnoreAspectRatio,
			Qt::FastTransformation);
	}
	return image;
}

QImage macResourceIconImage(const QString &path) {
	const QFileInfo info(path);
	if (!info.exists() || info.isSymLink()) {
		return {};
	}
	QStringList forks;
	if (info.isDir()) {
		const QString base = info.fileName();
		forks << path + "/._Icon\r";
		forks << path + "/.rsrc/Icon\r";
		forks << path + "/Icon\r/..namedfork/rsrc";
		forks << path + "/Icon\r";
		forks << path + "/..namedfork/rsrc";
		if (!base.isEmpty()) {
			forks << info.absolutePath() + "/._" + base;
		}
	} else {
		const QString base = info.fileName();
		forks << path + "/..namedfork/rsrc";
		forks << info.absolutePath() + "/._" + base;
		forks << info.absolutePath() + "/.rsrc/" + base;
	}
	for (const QString &fork : forks) {
		const QImage image = decodeIconFork(readFork(fork));
		if (!image.isNull()) {
			return image;
		}
	}
	return {};
}
