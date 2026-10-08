#include "items.h"

#include <QDir>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QStandardPaths>
#include <QFile>
#include <QSet>
#include <algorithm>
#include <cerrno>
#include <sys/xattr.h>

#include "appdb.h"
#include "apptrash.h"
#include "sharingclient.h"
#include "vfs.h"

static constexpr int LABEL_SHORT_INK = 72; /* inside an 80-px grid cell, with room between */
static constexpr int LABEL_MAX_INK = 400;   /* a whole name (Mac OS allowed 31 characters) */
static constexpr int LABEL_GAP = 2;      /* icon bottom to label box top */
static constexpr int LABEL_PAD_X = 2;    /* label box padding beside the ink */
static constexpr int LABEL_H = 13;       /* label box height */
static constexpr int LABEL_BASELINE = 10;

/* `name` cut in the middle with an ellipsis so its ink fits `maxInk`, as
 * Mac OS 8 and 9 shortened long names in icon views: both the start and
 * the end (often a number or an extension) stay readable. */
static QString middleTruncated(const QString &name, int maxInk, pl_font font) {
	const auto fits = [&](const QString &s) { return Text(s, 100000, font).inkWidth() <= maxInk; };
	if (fits(name)) {
		return name;
	}
	const auto cut = [&](int keep) {
		return name.left((keep + 1) / 2) + QStringLiteral("…") + name.right(keep / 2);
	};
	int lo = 0, hi = static_cast<int>(name.size()) - 1;
	while (lo < hi) { /* the most characters that still fit */
		const int mid = (lo + hi + 1) / 2;
		if (fits(cut(mid))) {
			lo = mid;
		} else {
			hi = mid - 1;
		}
	}
	return cut(lo);
}

const Text &Item::labelText() {
	if (!label) {
		label = std::make_unique<Text>(middleTruncated(name, LABEL_SHORT_INK, nameFont()),
			LABEL_SHORT_INK, nameFont());
	}
	return *label;
}

const Text &Item::fullLabelText() {
	if (!fullLabel) {
		fullLabel = std::make_unique<Text>(name, LABEL_MAX_INK, nameFont());
	}
	return *fullLabel;
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
	if (isMacDiskImage(iconPath)) {
		return PL_ICON_DISK_IMAGE;
	}
	if (info.isDir()) {
		return PL_ICON_FOLDER;
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
	if (appTrashMarker(item->path)) {
		TrashedApplication entry;
		QString error;
		if (appTrashRead(item->path, &entry, &error)) {
			item->name = entry.name;
			item->kind = PL_ICON_APPLICATION;
			if (const AppEntry *app = appById(entry.id)) {
				item->customIcon32 = app->icon32;
				item->customIcon16 = app->icon16;
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
		if (const AppEntry *app = appByFile(target)) {
			item->customIcon32 = app->icon32;
			item->customIcon16 = app->icon16;
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
	*b = *t + LABEL_H - 1;
}

void placeIcons(const std::vector<Item *> &items, const QHash<QString, QPoint> &placed,
		const std::function<QPoint(int)> &slot, int cellW, int cellH) {
	auto footprint = [&](QPoint icon) {
		return QRect(icon.x() - (cellW - PL_ICON_LARGE) / 2, icon.y(), cellW, cellH);
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
	return item.kind == PL_ICON_FOLDER || item.kind == PL_ICON_DISK ||
		item.kind == PL_ICON_TRASH_EMPTY || item.kind == PL_ICON_TRASH_FULL;
}

bool iconLabelContains(Item &item, int x, int y, QPoint p) {
	int l, t, r, b;
	labelBox(item, x, y, &l, &t, &r, &b);
	return QRect(QPoint(l, t), QPoint(r, b)).contains(p);
}

void paintIcon(pl_canvas *c, Item &item, int x, int y, int size, bool highlight) {
	const std::vector<uint32_t> &custom = size >= 32 ? item.customIcon32 : item.customIcon16;
	if (!custom.empty()) {
		/* A real application icon: real alpha, and no Finder-label tint
		 * (it would fight the icon's own colors). */
		pl_image_blend(c, x, y, custom.data(), size, size, highlight);
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
	pl_text(c, item.shownLabel().t, l + LABEL_PAD_X, t + LABEL_BASELINE, ink);
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
