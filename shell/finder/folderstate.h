#pragma once

#include <QHash>
#include <QPoint>
#include <QSize>
#include <QString>

/*
 * What the Finder remembers about each folder's window, as the Mac kept it
 * in the folder itself: where the window was, its size, how it was viewed.
 * Stored in ~/.local/share/zacos9/finder/folders.ini, keyed by path.
 */
struct FolderState {
	bool known = false; /* ever opened before */
	QPoint position;    /* frame top-left, layout coordinates */
	bool hasPosition = false;
	QSize size;
	int viewMode = 0;   /* 0 icons, 1 list */
	int sortColumn = 0;
	/* Icons the user has placed, by item name (icon top-left, content
	 * coordinates). Unplaced icons flow into free grid cells. */
	QHash<QString, QPoint> icons;

	static FolderState load(const QString &path);
	void save(const QString &path) const;
};
