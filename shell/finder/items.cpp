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

#include "sharingclient.h"

static constexpr int LABEL_MAX_INK = 110;
static constexpr int LABEL_GAP = 2;      /* icon bottom to label box top */
static constexpr int LABEL_PAD_X = 2;    /* label box padding beside the ink */
static constexpr int LABEL_H = 13;       /* label box height */
static constexpr int LABEL_BASELINE = 10;

const Text &Item::labelText() {
	if (!label) {
		label = std::make_unique<Text>(name, LABEL_MAX_INK, nameFont());
	}
	return *label;
}

const QString &Item::kindName() {
	if (kindText.isEmpty()) {
		if (isDir) {
			kindText = "folder";
		} else if (kind == PL_ICON_DISK_IMAGE) {
			kindText = "Macintosh disk image";
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
	if (path == "/") {
		return "Hard Disk";
	}
	if (QDir(path) == QDir(trashFilesPath())) {
		return "Trash";
	}
	return QFileInfo(path).fileName();
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
	if (info.isDir()) {
		return PL_ICON_FOLDER;
	}
	if (isMacDiskImage(path)) {
		return PL_ICON_DISK_IMAGE;
	}
	if (isClassicApplication(path)) {
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
	return sig == "BD" || sig == "H+" || sig == "HX" || head.startsWith("ER");
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
	item->shared = item->isDir && !item->isAlias && isSharedFolder(item->path);
	item->size = info.isDir() ? 0 : info.size();
	item->modified = info.lastModified();
	item->labelIndex = readLabel(item->path);
	return item;
}

static void labelBox(Item &item, int x, int y, int *l, int *t, int *r, int *b) {
	int w = item.labelText().inkWidth();
	int cx = x + PL_ICON_LARGE / 2;
	*l = cx - w / 2 - LABEL_PAD_X;
	*r = *l + w - 1 + 2 * LABEL_PAD_X;
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
	return item.kind == PL_ICON_FOLDER || item.kind == PL_ICON_DISK ||
		item.kind == PL_ICON_TRASH_EMPTY || item.kind == PL_ICON_TRASH_FULL;
}

bool iconLabelContains(Item &item, int x, int y, QPoint p) {
	int l, t, r, b;
	labelBox(item, x, y, &l, &t, &r, &b);
	return QRect(QPoint(l, t), QPoint(r, b)).contains(p);
}

void paintIconItem(pl_canvas *c, Item &item, int x, int y, bool onDesktop, bool showLabel) {
	pl_icon_paint_label(c, x, y, item.iconKind(), PL_ICON_LARGE, item.selected || item.dropTarget,
		item.labelColor());
	if (!showLabel) {
		return;
	}
	int l, t, r, b;
	labelBox(item, x, y, &l, &t, &r, &b);
	uint32_t ink = C_BLACK;
	if (item.selected) {
		pl_fill(c, l, t, r, b, C_BLACK);
		ink = C_WHITE;
	} else if (onDesktop) {
		pl_fill(c, l, t, r, b, C_WHITE);
	}
	pl_text(c, item.labelText().t, l + LABEL_PAD_X, t + LABEL_BASELINE, ink);
}

bool iconItemContains(Item &item, int x, int y, QPoint p) {
	if (QRect(x, y, PL_ICON_LARGE, PL_ICON_LARGE).contains(p)) {
		return true;
	}
	int l, t, r, b;
	labelBox(item, x, y, &l, &t, &r, &b);
	return QRect(QPoint(l, t), QPoint(r, b)).contains(p);
}
