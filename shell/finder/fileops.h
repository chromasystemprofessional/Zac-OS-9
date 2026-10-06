#pragma once

#include <QMimeData>
#include <QPoint>
#include <QStringList>
#include <vector>

#include "items.h"

class QWidget;
class QDropEvent;

/* Move or copy items into a folder, Finder style:
 * - within one disk the items move; between disks they are copied;
 * - Option (Alt) forces a copy; a copy into the same folder is named
 *   "<name> copy", "<name> copy 2", ...
 * Shows a responsive Platinum progress dialog with Stop for the batch.
 * Stop keeps completed items and rolls back the current incomplete copy.
 * Returns the folders whose contents changed; conflicts are reported and
 * never overwrite existing items. */
QStringList transferItems(const QStringList &paths, const QString &destDir, bool forceCopy,
	bool *finished = nullptr);
QString createAlias(const QString &target, const QString &destDir, const QString &name);

/* Start dragging `items`, shown as dotted outlines (Mac OS 8 dragged
 * outlines, not pictures). `itemOrigins` are the items' icon positions in
 * `source` coordinates; `pointer` is where the drag began. */
/* A drag of icons that only move about their own window (the Macintosh
 * view's virtual items: no files to hand anywhere). */
inline constexpr char ICON_MOVE_MIME[] = "application/x-zacos9-icon-move";
inline constexpr char ALIAS_ITEMS_MIME[] = "application/x-zacos9-alias-items";
inline constexpr char APPLICATION_ITEMS_MIME[] = "application/x-zacos9-application-items";
/* Caller owns the file URLs or private virtual-item alias payload. */
QMimeData *itemDragMime(const std::vector<Item *> &items);
void startItemDrag(QWidget *source, const std::vector<Item *> &items,
		const std::vector<QPoint> &itemOrigins, QPoint pointer);

/* Paths carried by a Finder drag (or any file drag). */
QStringList draggedPaths(const QMimeData *mime);

/* Drop onto `target` (a folder, the disk or the Trash item) or, with
 * target == nullptr, into `folder`. Notifies the Finder of changes. */
void dropItems(QDropEvent *e, const Item *target, const QString &folder);
