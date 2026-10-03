#pragma once

#include <QDateTime>
#include <QPoint>
#include <QString>
#include <memory>
#include <vector>

#include "pixels.h"

/* Something the Finder shows as an icon: a file, folder, disk or the Trash. */
struct Item {
	QString name;
	QString path;
	pl_icon_kind kind = PL_ICON_DOCUMENT;
	QPoint pos; /* icon top-left in its view's coordinates */
	bool selected = false;
	bool dropTarget = false; /* a drag is hovering over this folder */
	/* Key for remembered positions; the name unless set (disk, Trash). */
	QString stateKey;
	const QString &key() const { return stateKey.isEmpty() ? name : stateKey; }
	std::unique_ptr<Text> label;

	/* Finder label, 0 (None) .. PL_LABEL_COUNT-1, kept in LABEL_ATTR. */
	int labelIndex = 0;
	uint32_t labelColor() const { return pl_labels[labelIndex].color; }

	/* A folder shared from Get Info: drawn on a network line. */
	bool shared = false;

	/* A mounted network volume (AFP or SMB) on the desktop: real files,
	 * at a real path, but Put Away ejects it instead of trying to move
	 * it anywhere — there is nothing on this computer to put away. Like
	 * the startup disk, it can't be dragged at all yet (desktop.cpp
	 * excludes every "fixed" item from dragging; the Mac let you drag a
	 * disk to the Trash to eject it, a gap that predates this and isn't
	 * closed here either — see its TODO in Desktop::mouseMoveEvent).
	 * See shell/network/netvolumes.h. */
	bool isNetworkVolume = false;
	pl_icon_kind iconKind() const {
		return shared && kind == PL_ICON_FOLDER ? PL_ICON_SHARED_FOLDER : kind;
	}

	/* A virtual item: `path` names a node in the Macintosh view of this
	 * computer ("vfs:/applications/..."), not a file. See vfs.h. */
	bool isVirtual = false;
	/* An application's own icon, resolved from the freedesktop icon
	 * theme (straight-alpha ARGB, 32x32 and 16x16): drawn instead of the
	 * compiled icon for `kind` when not empty. See appdb.h. */
	std::vector<uint32_t> customIcon32, customIcon16;

	/* An alias (a symbolic link): its name is shown in italics. */
	bool isAlias = false;
	pl_font nameFont() const { return isAlias ? PL_FONT_VIEWS_ITALIC : PL_FONT_VIEWS; }

	/* For list view. */
	bool isDir = false;
	qint64 size = 0;
	QDateTime modified;
	QString kindText; /* filled in lazily: "folder", "Plain text document", ... */
	const QString &kindName();

	/* Label below the icon: views font, centred, truncated to fit. */
	const Text &labelText();
};

/* Directory listing as Finder items, folders and files mixed, by name
 * (the Finder's default "by Name" arrangement). Hidden files are skipped. */
std::vector<std::unique_ptr<Item>> listFolder(const QString &path);
class QFileInfo;
std::unique_ptr<Item> makeItem(const QFileInfo &info);

pl_icon_kind iconKindFor(const QString &path);
/* Is this folder shared (Get Info > Sharing)? */
bool isSharedFolder(const QString &path);
/* A Macintosh disk or CD image (HFS, HFS+ or partitioned): opening one
 * starts it in Classic. */
bool isMacDiskImage(const QString &path);
/* A classic Mac OS application: a file copied in through the emulator's
 * "Unix" volume, whose Finder info (in .finf/<name>, SheepShaver's ExtFS
 * format) has type 'APPL'. Opening one starts Classic. */
bool isClassicApplication(const QString &path);

/* The extended attribute holding an item's label, as a decimal index. */
inline constexpr const char *LABEL_ATTR = "user.zacos9.label";
int readLabel(const QString &path);
/* Label 0 removes the attribute. Returns false if it can't be stored. */
bool writeLabel(const QString &path, int label);

/* The node's name for a virtual path, "Unix" for /, "Trash" for the
 * trash, else the last path component. */
QString displayName(const QString &path);
QString trashFilesPath();

/* Paint an icon with its label; `onDesktop` labels get a white box when
 * unselected (desktop pattern behind them). TODO: measure label metrics. */
void paintIconItem(pl_canvas *c, Item &item, int x, int y, bool onDesktop,
		bool showLabel = true);
/* The icon alone, at `size` (32 or 16): an application's own icon if one
 * was resolved, else the compiled icon for its kind. Used by the list
 * and Find windows, which draw the label text themselves. */
void paintIcon(pl_canvas *c, Item &item, int x, int y, int size, bool highlight);
/* Is `p` on the label of the icon at (x, y)? */
bool iconLabelContains(Item &item, int x, int y, QPoint p);
/* Icon view placement: items whose names are in `placed` go there; the
 * rest take the first free grid slot (`slot(i)` gives slot i's icon
 * position) so they never land on a placed icon. Cells are cellW x cellH. */
#include <QHash>
#include <functional>
void placeIcons(const std::vector<Item *> &items, const QHash<QString, QPoint> &placed,
		const std::function<QPoint(int)> &slot, int cellW, int cellH);

/* Can items be dropped into this one (folder, disk, Trash)? */
bool acceptsDrops(const Item &item);
/* Hit area of an icon item at (x, y): the icon square or its label. */
bool iconItemContains(Item &item, int x, int y, QPoint p);
