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
 * Returns the folders whose contents changed.
 * TODO: the Mac asks before replacing an existing item; conflicts are
 * skipped until Platinum alerts exist. */
QStringList transferItems(const QStringList &paths, const QString &destDir, bool forceCopy);

/* Start dragging `items`, shown as dotted outlines (Mac OS 8 dragged
 * outlines, not pictures). `itemOrigins` are the items' icon positions in
 * `source` coordinates; `pointer` is where the drag began. */
/* A drag of icons that only move about their own window (the Macintosh
 * view's virtual items: no files to hand anywhere). */
inline constexpr char ICON_MOVE_MIME[] = "application/x-zacos9-icon-move";
void startItemDrag(QWidget *source, const std::vector<Item *> &items,
		const std::vector<QPoint> &itemOrigins, QPoint pointer);

/* Paths carried by a Finder drag (or any file drag). */
QStringList draggedPaths(const QMimeData *mime);

/* Drop onto `target` (a folder, the disk or the Trash item) or, with
 * target == nullptr, into `folder`. Notifies the Finder of changes. */
void dropItems(QDropEvent *e, const Item *target, const QString &folder);
