#include "items.h"

#include "clippings.h"
#include "desktopprinter.h"

#include <QDir>
#include <QBuffer>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QMimeDatabase>
#include <QPainter>
#include <QStandardPaths>
#include <QFile>
#include <QSet>
#include <QTextBoundaryFinder>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sys/xattr.h>

#include "appdb.h"
#include "fileassoc.h"
#include "apptrash.h"
#include "sharingclient.h"
#include "thumbnails.h"
#include "fonts.h"
#include "vfs.h"
#include "settings.h"

static constexpr int LABEL_MAX_INK = 400;   /* a whole name on one line (list view) */
static constexpr int LABEL_GAP = 2;      /* icon bottom to label box top */
static constexpr int LABEL_PAD_X = 2;    /* label box padding beside the ink */
static constexpr int LABEL_H = 13;       /* label box height */
static constexpr int LABEL_BASELINE = 10;
static constexpr const char *CUSTOM_ICON_ATTR = "user.zacos9.custom-icon";
static constexpr ssize_t CUSTOM_ICON_MAX = 64 * 1024;

/* A large icon's name wraps to its grid cell's width (Wide 80 px, Tight
 * 64 px), less room between neighbours. */
static int labelWrapInk() {
	char v[16]; /* viewoptions.h's tightGrid(), which the file portal lacks */
	return pl_setting("grid-spacing", v, sizeof(v)) && strcmp(v, "tight") == 0 ? 56 : 72;
}

/* `name` broken into lines whose ink (`ink`) fits `maxInk`: at spaces and
 * after hyphens where it can (Unicode line breaks), else between characters
 * for a word too long for a line. Spaces at a break are dropped. */
static QStringList wrapName(const QString &name, int maxInk,
		const std::function<int(const QString &)> &ink) {
	const auto line = [&](int from, int to) {
		while (to > from && name.at(to - 1).isSpace()) {
			to--;
		}
		return name.mid(from, to - from);
	};
	QList<int> breaks;
	QTextBoundaryFinder lineBreaks(QTextBoundaryFinder::Line, name);
	for (int p = lineBreaks.toNextBoundary(); p > 0; p = lineBreaks.toNextBoundary()) {
		breaks.push_back(p);
	}
	QStringList lines;
	int start = 0;
	while (start < name.size()) {
		int end = -1;
		for (int b : breaks) { /* the furthest break that still fits */
			if (b <= start) {
				continue;
			}
			if (ink(line(start, b)) > maxInk) {
				break;
			}
			end = b;
		}
		if (end < 0) { /* one word wider than a line: as much of it as fits */
			QTextBoundaryFinder chars(QTextBoundaryFinder::Grapheme, name);
			chars.setPosition(start);
			end = chars.toNextBoundary();
			for (int p = chars.toNextBoundary(); p > 0 && ink(line(start, p)) <= maxInk;
					p = chars.toNextBoundary()) {
				end = p;
			}
		}
		lines.push_back(line(start, end));
		start = end;
	}
	if (lines.isEmpty()) {
		lines.push_back(name);
	}
	return lines;
}

/* Lines drawn one under another, each centred (under an icon) or from the
 * left (beside one), as one text: the label code draws and measures it as
 * it would a single line. */
static Text stacked(std::vector<Text> &lines, bool centred) {
	int width = 0, pitch = 0;
	for (const Text &l : lines) {
		if (!l.t) {
			return std::move(lines.front());
		}
		width = std::max(width, l.inkWidth());
		pitch = std::max(pitch, l.t->height);
	}
	if (lines.size() == 1 || width == 0) {
		return std::move(lines.front());
	}
	auto *t = static_cast<plat_text *>(calloc(1, sizeof(plat_text)));
	t->width = t->stride = t->advance = width;
	t->height = pitch * static_cast<int>(lines.size());
	t->baseline = lines.front().t->baseline;
	t->ink_l = 0;
	t->ink_r = width - 1;
	t->mask = static_cast<uint8_t *>(calloc(static_cast<size_t>(t->stride * t->height), 1));
	for (size_t i = 0; i < lines.size(); i++) {
		const plat_text *l = lines[i].t;
		if (l->ink_l < 0) {
			continue;
		}
		const int x0 = (centred ? (width - lines[i].inkWidth()) / 2 : 0) - l->ink_l;
		const int y0 = pitch * static_cast<int>(i);
		for (int y = 0; y < l->height; y++) {
			for (int x = l->ink_l; x <= l->ink_r; x++) {
				t->mask[(y0 + y) * t->stride + x0 + x] = l->mask[y * l->stride + x];
			}
		}
	}
	Text result;
	result.t = t;
	return result;
}

/* Fills `cache` with the name wrapped to `ink`, unless it already is. */
static const Text &wrapped(const Item &item, Item::WrappedLabel &cache, int ink, bool centred) {
	if (!cache.text || cache.width != ink) {
		const QStringList parts = wrapName(item.visibleName(), ink,
			[&](const QString &s) { return item.renderString(s, 100000).inkWidth(); });
		std::vector<Text> lines;
		for (const QString &part : parts) {
			lines.push_back(item.renderString(part, 100000));
		}
		cache.width = ink;
		cache.lines = static_cast<int>(lines.size());
		cache.text = std::make_unique<Text>(stacked(lines, centred));
	}
	return *cache.text;
}

const Text &Item::labelText() {
	return wrapped(*this, label, labelWrapInk(), true);
}

const Text &Item::sideLabelText(int ink) {
	return wrapped(*this, sideLabel, ink, false);
}

int Item::labelBoxHeight(const WrappedLabel &l, int plainH) const {
	const bool measured = l.text && l.text->t && (!fontFile.isEmpty() || l.lines > 1);
	return measured ? l.text->t->height + 2 : plainH;
}

int Item::labelBaseline(const WrappedLabel &l, int plainBaseline) const {
	const bool measured = l.text && l.text->t && (!fontFile.isEmpty() || l.lines > 1);
	return measured ? l.text->t->baseline + 1 : plainBaseline;
}

const Text &Item::fullLabelText() {
	if (!fullLabel) {
		fullLabel = std::make_unique<Text>(renderName(LABEL_MAX_INK));
	}
	return *fullLabel;
}

Text Item::renderName(int maxWidth) const {
	return renderString(visibleName(), maxWidth);
}

Text Item::renderString(const QString &s, int maxWidth) const {
	if (fontFile.isEmpty()) {
		return Text(s, maxWidth, nameFont());
	}
	Text result;
	result.t = fontRenderName(s, maxWidth, fontFile, fontFaceIndex);
	return result;
}

const QString &Item::kindName() {
	if (kindText.isEmpty()) {
		if (kind == PL_ICON_DISK_IMAGE) {
			kindText = "Macintosh disk image";
		} else if (isDir) {
			kindText = "folder";
		} else if (kind == PL_ICON_WINDOWS) {
			kindText = "Windows application";
		} else if (kind == PL_ICON_CLASSIC) {
			kindText = "classic application";
		} else if (kind == PL_ICON_APPLICATION) {
			kindText = "application program";
		} else if (kind == PL_ICON_CLIPPING) {
			kindText = clippingKind(path) == ClippingKind::Picture ? "picture clipping" : "text clipping";
		} else if (kind == PL_ICON_INTERNET_LOCATION) {
			kindText = "Internet location";
		} else if (kind == PL_ICON_PRINTER) {
			kindText = "desktop printer";
		} else {
			static QMimeDatabase db;
			kindText = db.mimeTypeForFile(path).comment();
		}
	}
	return kindText;
}

QString trashFilesPath() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
		"/Trash/files";
}

QString favoritesPath() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		"/zacos9/Favorites";
}

QString toyBoxPath() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		"/zacos9/Toy Box Items";
}

QString displayName(const QString &path) {
	if (vfsIsVirtual(path)) {
		return vfsName(path);
	}
	if (path == "/") {
		/* Debian's own filesystem; the startup disk is the Macintosh
		 * view, which has a name of its own (see vfs.h). */
		return "Unix";
	}
	if (QDir(path) == QDir(trashFilesPath())) {
		return "Trash";
	}
	return QFileInfo(path).fileName();
}

bool isWindowsExecutable(const QString &path) {
	const QFileInfo info(path);
	if (!info.isFile()) {
		return false;
	}
	const QString suffix = info.suffix().toLower();
	return suffix == "exe" || suffix == "msi";
}

bool isClassicApplication(const QString &path) {
	const QFileInfo info(path);
	if (!info.isFile()) {
		return false;
	}
	QFile finf(info.absolutePath() + "/.finf/" + info.fileName());
	if (!finf.open(QIODevice::ReadOnly)) {
		return false;
	}
	return finf.read(4) == "APPL"; /* FInfo.fdType comes first */
}

bool isSharedFolder(const QString &path) {
	/* The list, read again when it changes. */
	static QSet<QString> paths;
	static QDateTime stamp;
	const QFileInfo list(sharingConfFile("shared-folders"));
	if (list.lastModified() != stamp) {
		stamp = list.lastModified();
		paths.clear();
		for (const SharedFolder &f : sharedFolders()) {
			paths.insert(f.path);
		}
	}
	return !paths.isEmpty() && paths.contains(QFileInfo(path).canonicalFilePath());
}

pl_icon_kind iconKindFor(const QString &path) {
	QFileInfo info(path);
	if (info.isSymLink()) {
		const QString target = info.canonicalFilePath();
		if (target.isEmpty() || QFileInfo(target).isSymLink()) {
			return PL_ICON_DOCUMENT;
		}
		info.setFile(target);
	}
	const QString iconPath = info.absoluteFilePath();
	if (isMacDiskImage(iconPath) || isDiscImage(iconPath)) {
		return PL_ICON_DISK_IMAGE;
	}
	if (info.isDir()) {
		return PL_ICON_FOLDER;
	}
	switch (clippingKind(iconPath)) {
	case ClippingKind::Text:
	case ClippingKind::Picture: return PL_ICON_CLIPPING;
	case ClippingKind::Location: return PL_ICON_INTERNET_LOCATION;
	case ClippingKind::None: break;
	}
	if (!desktopPrinterQueue(iconPath).isEmpty()) {
		return PL_ICON_PRINTER;
	}
	if (isWindowsExecutable(iconPath)) {
		return PL_ICON_WINDOWS;
	}
	if (isClassicApplication(iconPath)) {
		return PL_ICON_CLASSIC;
	}
	static QMimeDatabase db;
	const QString mime = db.mimeTypeForFile(info).name();
	if (mime == "application/x-executable" || mime == "application/x-sharedlib" ||
			mime == "application/x-pie-executable" ||
			mime == "application/x-desktop") {
		return PL_ICON_APPLICATION;
	}
	return PL_ICON_DOCUMENT;
}

/* A CD or DVD image (ISO 9660, "CD001" in its first volume descriptor). */
bool isDiscImage(const QString &path) {
	const QFileInfo info(path);
	if (!info.isFile() || info.size() < 34 * 1024 || info.suffix().compare("iso", Qt::CaseInsensitive) != 0) {
		return false;
	}
	QFile f(path);
	return f.open(QIODevice::ReadOnly) && f.seek(32769) && f.read(5) == "CD001";
}

bool isMacDiskImage(const QString &path) {
	static const QStringList suffixes = {
		"dsk", "img", "hfv", "hda", "toast", "iso", "cdr", "image",
	};
	QFileInfo info(path);
	if (info.isDir() && info.suffix().compare("sparsebundle", Qt::CaseInsensitive) == 0) {
		return QFileInfo(path + "/Info.plist").isFile();
	}
	if (info.isFile() && (info.suffix().compare("dmg", Qt::CaseInsensitive) == 0 ||
			info.suffix().compare("sparseimage", Qt::CaseInsensitive) == 0)) {
		return true;
	}
	if (!info.isFile() || info.size() < 400 * 1024 ||
			!suffixes.contains(info.suffix().toLower())) {
		return false;
	}
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		return false;
	}
	const QByteArray head = f.read(1026);
	if (head.size() < 1026) {
		return false;
	}
	const QByteArray sig = head.mid(1024, 2);
	return sig == "BD" || sig == "H+" || sig == "HX" || head.startsWith("ER") ||
		head.mid(32, 4) == "NXSB" || head.mid(512, 8) == "EFI PART" ||
		(head.mid(82, 2) == QByteArray("\x01\x00", 2) && static_cast<unsigned char>(head[0]) <= 63);
}

int readLabel(const QString &path) {
	char buf[8];
	ssize_t n = lgetxattr(QFile::encodeName(path).constData(), LABEL_ATTR, buf, sizeof(buf) - 1);
	if (n <= 0) {
		return 0;
	}
	buf[n] = '\0';
	int label = atoi(buf);
	return label > 0 && label < PL_LABEL_COUNT ? label : 0;
}

bool writeLabel(const QString &path, int label) {
	const QByteArray name = QFile::encodeName(path);
	if (label <= 0) {
		return lremovexattr(name.constData(), LABEL_ATTR) == 0 || errno == ENODATA;
	}
	const QByteArray value = QByteArray::number(label);
	return lsetxattr(name.constData(), LABEL_ATTR, value.constData(), value.size(), 0) == 0;
}

QImage customIconImage(const QString &path) {
	const QByteArray name = QFile::encodeName(path);
	const ssize_t size = lgetxattr(name.constData(), CUSTOM_ICON_ATTR, nullptr, 0);
	if (size <= 0 || size > CUSTOM_ICON_MAX) {
		return {};
	}
	QByteArray encoded(static_cast<int>(size), Qt::Uninitialized);
	if (lgetxattr(name.constData(), CUSTOM_ICON_ATTR, encoded.data(),
			static_cast<size_t>(encoded.size())) != size) {
		return {};
	}
	QBuffer buffer(&encoded);
	if (!buffer.open(QIODevice::ReadOnly)) {
		return {};
	}
	QImageReader reader(&buffer, "PNG");
	if (reader.size() != QSize(PL_ICON_LARGE, PL_ICON_LARGE)) {
		return {};
	}
	const QImage image = reader.read();
	if (image.isNull()) {
		return {};
	}
	return image.convertToFormat(QImage::Format_ARGB32);
}

bool writeCustomIconImage(const QString &path, const QImage &source) {
	if (source.isNull() || source.width() > 4096 || source.height() > 4096 ||
			static_cast<qint64>(source.width()) * source.height() > 16 * 1024 * 1024) {
		errno = source.isNull() ? EINVAL : EFBIG;
		return false;
	}
	QImage image(PL_ICON_LARGE, PL_ICON_LARGE, QImage::Format_ARGB32);
	image.fill(Qt::transparent);
	const QImage scaled = source.scaled(PL_ICON_LARGE, PL_ICON_LARGE, Qt::KeepAspectRatio,
		Qt::SmoothTransformation);
	QPainter painter(&image);
	painter.drawImage((PL_ICON_LARGE - scaled.width()) / 2,
		(PL_ICON_LARGE - scaled.height()) / 2, scaled);
	QByteArray encoded;
	QBuffer buffer(&encoded);
	if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG") ||
			encoded.isEmpty() || encoded.size() > CUSTOM_ICON_MAX) {
		errno = EFBIG;
		return false;
	}
	const QByteArray name = QFile::encodeName(path);
	return lsetxattr(name.constData(), CUSTOM_ICON_ATTR, encoded.constData(),
		static_cast<size_t>(encoded.size()), 0) == 0;
}

bool clearCustomIconImage(const QString &path) {
	const QByteArray name = QFile::encodeName(path);
	return lremovexattr(name.constData(), CUSTOM_ICON_ATTR) == 0 || errno == ENODATA;
}

/* Folders a classic Mac makes on any file server it writes to, and hides
 * there with its own invisible flag. */
static bool macHousekeeping(const QString &name) {
	static const QStringList names = { "Network Trash Folder", "TheVolumeSettingsFolder",
		"TheFindByContentFolder", "Temporary Items", "Icon\r" };
	return names.contains(name);
}

std::vector<std::unique_ptr<Item>> listFolder(const QString &path) {
	if (vfsIsVirtual(path)) {
		return vfsList(path);
	}
	std::vector<std::unique_ptr<Item>> items;
	QDir dir(path);
	const auto entries = dir.entryInfoList(
		QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System, QDir::Name | QDir::IgnoreCase);
	for (const QFileInfo &info : entries) {
		if (!macHousekeeping(info.fileName())) {
			items.push_back(makeItem(info));
		}
	}
	return items;
}

std::unique_ptr<Item> makeItem(const QFileInfo &info) {
	auto item = std::make_unique<Item>();
	item->name = info.fileName();
	item->path = info.absoluteFilePath();
	item->kind = iconKindFor(item->path);
	item->isDir = info.isDir();
	item->isAlias = info.isSymLink();
	item->shared = item->isDir && isSharedFolder(item->path);
	item->size = info.isDir() ? 0 : info.size();
	item->modified = info.lastModified();
	item->labelIndex = readLabel(item->path);
	const QImage custom = customIconImage(item->path);
	if (!custom.isNull()) {
		const QImage small = custom.scaled(PL_ICON_SMALL, PL_ICON_SMALL,
			Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		item->customIcon32.resize(PL_ICON_LARGE * PL_ICON_LARGE);
		item->customIcon16.resize(PL_ICON_SMALL * PL_ICON_SMALL);
		for (int y = 0; y < PL_ICON_LARGE; y++) {
			memcpy(item->customIcon32.data() + y * PL_ICON_LARGE, custom.constScanLine(y),
				PL_ICON_LARGE * sizeof(uint32_t));
		}
		for (int y = 0; y < PL_ICON_SMALL; y++) {
			memcpy(item->customIcon16.data() + y * PL_ICON_SMALL, small.constScanLine(y),
				PL_ICON_SMALL * sizeof(uint32_t));
		}
	}
	if (!item->isDir) {
		const QString shown = nameWithoutExtension(item->name);
		if (shown != item->name) {
			item->shownName = shown;
		}
	}
	if (appTrashMarker(item->path)) {
		item->shownName.clear();
		TrashedApplication entry;
		QString error;
		if (appTrashRead(item->path, &entry, &error)) {
			item->name = entry.name;
			item->kind = PL_ICON_APPLICATION;
			if (item->customIcon32.empty()) {
				if (const AppEntry *app = appById(entry.id)) {
					item->customIcon32 = app->icon32;
					item->customIcon16 = app->icon16;
				}
			}
		} else {
			qWarning().noquote() << error;
		}
	}
	/* An application's alias: a link to its desktop file. It looks and
	 * opens like the application. */
	const QString target = item->isAlias ? info.canonicalFilePath() : QString();
	if (target.endsWith(QLatin1String(".desktop"))) {
		item->kind = PL_ICON_APPLICATION;
		if (item->customIcon32.empty()) {
			if (const AppEntry *app = appByFile(target)) {
				item->customIcon32 = app->icon32;
				item->customIcon16 = app->icon16;
			}
		}
	}
	return item;
}

/* The label's box, centred under the icon; with `limitW`, moved sideways
 * to stay within 0..limitW-1 (a whole name near a screen's edge). */
static void labelBox(Item &item, int x, int y, int *l, int *t, int *r, int *b, int limitW = 0) {
	int w = item.shownLabel().inkWidth();
	int cx = x + PL_ICON_LARGE / 2;
	*l = cx - w / 2 - LABEL_PAD_X;
	*r = *l + w - 1 + 2 * LABEL_PAD_X;
	if (limitW > 0) {
		const int shift = *r > limitW - 1 ? limitW - 1 - *r : *l < 0 ? -*l : 0;
		*l += shift;
		*r += shift;
	}
	*t = y + PL_ICON_LARGE + LABEL_GAP;
	*b = *t + item.labelBoxHeight(item.label, LABEL_H) - 1;
}

int nearestFreeSlot(QPoint p, const std::function<QPoint(int)> &slot, int slotCount,
		const QSet<int> &taken) {
	int best = -1;
	qint64 bestDist = 0;
	for (int s = 0; s < slotCount; s++) {
		if (taken.contains(s)) {
			continue;
		}
		const QPoint d = slot(s) - p;
		const qint64 dist = qint64(d.x()) * d.x() + qint64(d.y()) * d.y();
		if (best < 0 || dist < bestDist) {
			best = s;
			bestDist = dist;
		}
	}
	return best;
}

void placeIcons(const std::vector<Item *> &items, const QHash<QString, QPoint> &placed,
		const std::function<QPoint(int)> &slot, int cellW, int cellH, int iconInset) {
	const int inset = iconInset >= 0 ? iconInset : (cellW - PL_ICON_LARGE) / 2;
	auto footprint = [&](QPoint icon) {
		return QRect(icon.x() - inset, icon.y(), cellW, cellH);
	};
	std::vector<QRect> taken;
	std::vector<Item *> unplaced;
	for (Item *item : items) {
		auto it = placed.find(item->key());
		if (it != placed.end()) {
			item->pos = *it;
			taken.push_back(footprint(item->pos));
		} else {
			unplaced.push_back(item);
		}
	}
	int s = 0;
	for (Item *item : unplaced) {
		for (;; s++) {
			const QRect cell = footprint(slot(s));
			bool clash = false;
			for (const QRect &r : taken) {
				clash |= r.intersects(cell);
			}
			if (!clash) {
				break;
			}
		}
		item->pos = slot(s);
		taken.push_back(footprint(item->pos));
		s++;
	}
}

bool acceptsDrops(const Item &item) {
	/* A curated folder has nowhere to put a file; only the ones standing
	 * for a real directory take drops. */
	if (item.isVirtual) {
		return vfsAcceptsDrops(item.path);
	}
	/* A desktop printer prints what's dropped on it. */
	return item.kind == PL_ICON_FOLDER || item.kind == PL_ICON_DISK ||
		item.kind == PL_ICON_TRASH_EMPTY || item.kind == PL_ICON_TRASH_FULL || item.kind == PL_ICON_PRINTER;
}

bool iconLabelContains(Item &item, int x, int y, QPoint p) {
	int l, t, r, b;
	labelBox(item, x, y, &l, &t, &r, &b);
	return QRect(QPoint(l, t), QPoint(r, b)).contains(p);
}

namespace {

/* `pixels` (straight-alpha ARGB, from x from) scaled to `to` square. */
std::vector<uint32_t> scaledArgb(const std::vector<uint32_t> &pixels, int from, int to) {
	if (from == to) {
		return pixels;
	}
	const QImage src(reinterpret_cast<const uchar *>(pixels.data()), from, from,
		from * 4, QImage::Format_ARGB32);
	const QImage img = src.scaled(to, to, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
		.convertToFormat(QImage::Format_ARGB32);
	std::vector<uint32_t> out(static_cast<size_t>(to) * to);
	for (int y = 0; y < to; y++) {
		memcpy(out.data() + static_cast<size_t>(y) * to, img.constScanLine(y),
			static_cast<size_t>(to) * sizeof(uint32_t));
	}
	return out;
}

/* The Platinum document page with its text lines wiped, and the
 * application's icon laid on it (16 px on the large page, 8 on the
 * small), as the Mac drew an application's documents. */
std::vector<uint32_t> composeDocument(const std::vector<uint32_t> &appIcon16, int size) {
	Pixels page(size, size);
	pl_icon_paint(&page.c, 0, 0, PL_ICON_DOCUMENT, size, false);
	const bool large = size >= 32;
	/* Inside the page's frame, below its folded corner. */
	if (large) {
		pl_fill(&page.c, 7, 8, 24, 29, C_WHITE);
	} else {
		pl_fill(&page.c, 4, 5, 11, 13, C_WHITE);
	}
	const int glyph = large ? 16 : 8;
	const std::vector<uint32_t> icon = scaledArgb(appIcon16, 16, glyph);
	pl_image_blend(&page.c, large ? 8 : 4, large ? 12 : 5, icon.data(), glyph, glyph, false);
	std::vector<uint32_t> out(static_cast<size_t>(size) * size);
	for (int y = 0; y < size; y++) {
		memcpy(out.data() + static_cast<size_t>(y) * size, page.img.constScanLine(y),
			static_cast<size_t>(size) * sizeof(uint32_t));
	}
	return out;
}

} // namespace

const std::vector<uint32_t> *documentIcon(const QString &mimeType, int size) {
	struct Icons {
		std::vector<uint32_t> large, small;
	};
	static QHash<QString, Icons> cache;
	static unsigned generation = 0;
	static bool watchingApps = false;
	if (!watchingApps) {
		watchingApps = true;
		/* A newly installed (or removed) application can change the
		 * default for any type, and brings its icon. */
		appOnChange([] { fileAssocInvalidate(); });
	}
	if (generation != fileAssocGeneration()) {
		generation = fileAssocGeneration();
		cache.clear();
	}
	if (mimeType.isEmpty()) {
		return nullptr;
	}
	auto it = cache.find(mimeType);
	if (it == cache.end()) {
		Icons icons;
		const QString id = defaultAppFor(mimeType);
		std::vector<uint32_t> glyph;
		if (!id.isEmpty()) {
			const AppEntry *app = appById(id);
			glyph = app && !app->icon16.empty() ? app->icon16 : appIconPixels(id, 16);
		}
		if (glyph.size() == 16 * 16) {
			icons.large = composeDocument(glyph, PL_ICON_LARGE);
			icons.small = composeDocument(glyph, PL_ICON_SMALL);
		}
		it = cache.insert(mimeType, icons);
	}
	const std::vector<uint32_t> &icon = size >= 32 ? it->large : it->small;
	return icon.empty() ? nullptr : &icon;
}

void paintIcon(pl_canvas *c, Item &item, int x, int y, int size, bool highlight) {
	if (!item.thumbnailResolved) {
		if (item.isVirtual || item.isDir || item.kind != PL_ICON_DOCUMENT ||
				!item.customIcon32.empty() ||
				thumbnailFor(item.path, &item.customIcon32, &item.customIcon16) !=
					ThumbnailState::Pending) {
			item.thumbnailResolved = true;
		}
	}
	const std::vector<uint32_t> *custom = size >= 32 ? &item.customIcon32 : &item.customIcon16;
	if (custom->empty() && !item.isVirtual && !item.isDir && item.kind == PL_ICON_DOCUMENT) {
		if (!item.mimeResolved) {
			item.mimeType = fileMimeType(item.path);
			item.mimeResolved = true;
		}
		if (const std::vector<uint32_t> *doc = documentIcon(item.mimeType, size)) {
			custom = doc;
		}
	}
	if (!custom->empty()) {
		/* A real application icon: real alpha, and no Finder-label tint
		 * (it would fight the icon's own colors). */
		pl_image_blend(c, x, y, custom->data(), size, size, highlight);
	} else {
		pl_icon_paint_label(c, x, y, item.iconKind(), size, highlight, item.labelColor());
		if (item.isUsbVolume && item.kind == PL_ICON_DISK) {
			const int badgeSize = size >= 32 ? 16 : 8;
			Pixels badge(PL_ICON_SMALL, PL_ICON_SMALL);
			pl_icon_paint_label(&badge.c, 0, 0, PL_ICON_EXT_USB,
				PL_ICON_SMALL, highlight, item.labelColor());
			const QImage scaled = badge.img.scaled(badgeSize, badgeSize,
				Qt::IgnoreAspectRatio, Qt::FastTransformation);
			pl_image(c, x + size - badgeSize, y + size - badgeSize,
				reinterpret_cast<const uint32_t *>(scaled.constBits()), badgeSize, badgeSize);
		}
	}
}

void paintIconItem(pl_canvas *c, Item &item, int x, int y, bool onDesktop, bool showLabel) {
	const bool highlight = item.selected || item.dropTarget;
	paintIcon(c, item, x, y, PL_ICON_LARGE, highlight);
	/* Mac OS 9's default desktop printer: a bold outline round its icon. */
	if (item.kind == PL_ICON_PRINTER) {
		const QString def = desktopDefaultPrinter();
		if (!def.isEmpty() && desktopPrinterQueue(item.path) == def) {
			for (int i = 0; i < 2; i++) {
				pl_outline(c, x - 3 + i, y - 3 + i, x + PL_ICON_LARGE + 2 - i, y + PL_ICON_LARGE + 2 - i, C_BLACK);
			}
		}
	}
	if (showLabel) {
		paintIconLabel(c, item, x, y, onDesktop);
	}
}

void paintIconLabel(pl_canvas *c, Item &item, int x, int y, bool onDesktop) {
	int l, t, r, b;
	labelBox(item, x, y, &l, &t, &r, &b, c->width);
	uint32_t ink = C_BLACK;
	if (item.selected) {
		pl_fill(c, l, t, r, b, C_BLACK);
		ink = C_WHITE;
	} else if (onDesktop) {
		pl_fill(c, l, t, r, b, C_WHITE);
	}
	const auto *text = item.shownLabel().t;
	pl_text(c, text, l + LABEL_PAD_X, t + item.labelBaseline(item.label, LABEL_BASELINE), ink);
}

bool iconItemContains(Item &item, int x, int y, QPoint p) {
	if (QRect(x, y, PL_ICON_LARGE, PL_ICON_LARGE).contains(p)) {
		return true;
	}
	int l, t, r, b;
	labelBox(item, x, y, &l, &t, &r, &b);
	return QRect(QPoint(l, t), QPoint(r, b)).contains(p);
}

QRect iconItemRect(Item &item, int x, int y) {
	int l, t, r, b;
	labelBox(item, x, y, &l, &t, &r, &b);
	return QRect(x, y, PL_ICON_LARGE, PL_ICON_LARGE).united(QRect(QPoint(l, t), QPoint(r, b)));
}

void paintMarquee(pl_canvas *c, const QRect &r) {
	const QRect visible = r.intersected(QRect(c->x, c->y, c->width, c->height));
	if (visible.isEmpty()) {
		return;
	}
	auto dot = [&](int x, int y) {
		if (((x + y) & 1) == 0) {
			pl_put(c, x, y, GRAY(0x5));
		}
	};
	for (int x = visible.left(); x <= visible.right(); x++) {
		dot(x, r.top());
		dot(x, r.bottom());
	}
	for (int y = visible.top(); y <= visible.bottom(); y++) {
		dot(r.left(), y);
		dot(r.right(), y);
	}
}

void sortIcons(std::vector<Item *> &items, Arrange by) {
	const auto byName = [](Item *a, Item *b) {
		return QString::localeAwareCompare(a->name, b->name) < 0;
	};
	/* Newest first; items without a date (virtual ones) after the rest. */
	const auto newest = [&](const QDateTime &da, const QDateTime &db, Item *a, Item *b) {
		if (da.isValid() != db.isValid()) {
			return da.isValid();
		}
		return da != db ? da > db : byName(a, b);
	};
	switch (by) {
	case Arrange::CleanUp:
	case Arrange::Name:
		std::stable_sort(items.begin(), items.end(), byName);
		break;
	case Arrange::Modified:
		std::stable_sort(items.begin(), items.end(), [&](Item *a, Item *b) {
			return newest(a->modified, b->modified, a, b);
		});
		break;
	case Arrange::Created: {
		QHash<Item *, QDateTime> born;
		for (Item *item : items) {
			born.insert(item, item->isVirtual ? QDateTime() : QFileInfo(item->path).birthTime());
		}
		std::stable_sort(items.begin(), items.end(), [&](Item *a, Item *b) {
			return newest(born.value(a), born.value(b), a, b);
		});
		break;
	}
	case Arrange::Size:
		std::stable_sort(items.begin(), items.end(), [&](Item *a, Item *b) {
			return a->size != b->size ? a->size > b->size : byName(a, b);
		});
		break;
	case Arrange::Kind:
		std::stable_sort(items.begin(), items.end(), [&](Item *a, Item *b) {
			const int c = QString::localeAwareCompare(a->kindName(), b->kindName());
			return c != 0 ? c < 0 : byName(a, b);
		});
		break;
	case Arrange::Label:
		std::stable_sort(items.begin(), items.end(), [&](Item *a, Item *b) {
			const int ra = a->labelIndex ? a->labelIndex : PL_LABEL_COUNT;
			const int rb = b->labelIndex ? b->labelIndex : PL_LABEL_COUNT;
			return ra != rb ? ra < rb : byName(a, b);
		});
		break;
	}
}

void arrangeIcons(std::vector<Item *> items, Arrange how,
		const std::function<QPoint(int)> &slot, int slotCount) {
	if (how != Arrange::CleanUp) {
		sortIcons(items, how);
		for (int i = 0; i < static_cast<int>(items.size()) && i < slotCount; i++) {
			items[static_cast<size_t>(i)]->pos = slot(i);
		}
		return;
	}
	std::stable_sort(items.begin(), items.end(), [](Item *a, Item *b) {
		return a->pos.y() != b->pos.y() ? a->pos.y() < b->pos.y() : a->pos.x() < b->pos.x();
	});
	std::vector<bool> taken(static_cast<size_t>(std::max(0, slotCount)), false);
	for (Item *item : items) {
		int best = -1;
		qint64 bestDist = 0;
		for (int s = 0; s < slotCount; s++) {
			if (taken[static_cast<size_t>(s)]) {
				continue;
			}
			const QPoint d = slot(s) - item->pos;
			const qint64 dist = qint64(d.x()) * d.x() + qint64(d.y()) * d.y();
			if (best < 0 || dist < bestDist) {
				best = s;
				bestDist = dist;
			}
		}
		if (best < 0) {
			break;
		}
		taken[static_cast<size_t>(best)] = true;
		item->pos = slot(best);
	}
}
