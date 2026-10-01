#pragma once

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
	std::unique_ptr<Text> label;

	/* Label below the icon: views font, centred, truncated to fit. */
	const Text &labelText();
};

/* Directory listing as Finder items, folders and files mixed, by name
 * (the Finder's default "by Name" arrangement). Hidden files are skipped. */
std::vector<std::unique_ptr<Item>> listFolder(const QString &path);

pl_icon_kind iconKindFor(const QString &path);

/* "Hard Disk" for /, "Trash" for the trash, else the last path component. */
QString displayName(const QString &path);
QString trashFilesPath();

/* Paint an icon with its label; `onDesktop` labels get a white box when
 * unselected (desktop pattern behind them). TODO: measure label metrics. */
void paintIconItem(pl_canvas *c, Item &item, int x, int y, bool onDesktop,
		bool showLabel = true);
/* Is `p` on the label of the icon at (x, y)? */
bool iconLabelContains(Item &item, int x, int y, QPoint p);
/* Can items be dropped into this one (folder, disk, Trash)? */
bool acceptsDrops(const Item &item);
/* Hit area of an icon item at (x, y): the icon square or its label. */
bool iconItemContains(Item &item, int x, int y, QPoint p);
