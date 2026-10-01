#include "items.h"

#include <QDir>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QStandardPaths>
#include <algorithm>

static constexpr int LABEL_MAX_INK = 110;
static constexpr int LABEL_GAP = 2;      /* icon bottom to label box top */
static constexpr int LABEL_PAD_X = 2;    /* label box padding beside the ink */
static constexpr int LABEL_H = 13;       /* label box height */
static constexpr int LABEL_BASELINE = 10;

const Text &Item::labelText() {
	if (!label) {
		label = std::make_unique<Text>(name, LABEL_MAX_INK, PL_FONT_VIEWS);
	}
	return *label;
}

const QString &Item::kindName() {
	if (kindText.isEmpty()) {
		if (isDir) {
			kindText = "folder";
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

pl_icon_kind iconKindFor(const QString &path) {
	QFileInfo info(path);
	if (info.isDir()) {
		return PL_ICON_FOLDER;
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

std::vector<std::unique_ptr<Item>> listFolder(const QString &path) {
	std::vector<std::unique_ptr<Item>> items;
	QDir dir(path);
	const auto entries = dir.entryInfoList(
		QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System, QDir::Name | QDir::IgnoreCase);
	for (const QFileInfo &info : entries) {
		auto item = std::make_unique<Item>();
		item->name = info.fileName();
		item->path = info.absoluteFilePath();
		item->kind = iconKindFor(item->path);
		item->isDir = info.isDir();
		item->size = info.isDir() ? 0 : info.size();
		item->modified = info.lastModified();
		items.push_back(std::move(item));
	}
	return items;
}

static void labelBox(Item &item, int x, int y, int *l, int *t, int *r, int *b) {
	int w = item.labelText().inkWidth();
	int cx = x + PL_ICON_LARGE / 2;
	*l = cx - w / 2 - LABEL_PAD_X;
	*r = *l + w - 1 + 2 * LABEL_PAD_X;
	*t = y + PL_ICON_LARGE + LABEL_GAP;
	*b = *t + LABEL_H - 1;
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
	pl_icon_paint(c, x, y, item.kind, PL_ICON_LARGE, item.selected || item.dropTarget);
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
