#pragma once

/*
 * The partition map: a drive drawn as Drive Setup and Disk Utility draw
 * one, a tall column from its start at the top to its end at the bottom,
 * a block for each partition (in its format's colour, with its name,
 * format and size) and white for free space. Click a block to choose it;
 * drag the line under a partition that can change size (it has a grip)
 * to make it bigger or smaller. Disk Setup's Partition tab and the
 * installer's "next to what's there" both use it.
 *
 * It changes nothing itself: `dragged` asks the window to move the line
 * (moveBoundary in plan.h), which then sets the drive as planned again.
 */

#include <QList>
#include <QPoint>
#include <QRect>
#include <functional>

#include "draw.h"
#include "plan.h"

/* A partition's colour in the map, by its format. */
uint32_t partColor(const QString &fsType);

struct DiskMap {
	QRect frame; /* the black frame */
	struct Row {
		int part = -1, gap = -1; /* a partition of `drive`, or a gap of `gaps` */
	};
	DiskDrive drive;     /* as planned */
	QList<DiskGap> gaps; /* its free space */
	QList<Row> rows;     /* top to bottom */
	int selected = -1;   /* a row */
	bool enabled = true;
	std::function<void(int)> picked; /* a row was chosen */
	/* The line under partition `part` dragged to byte `at`. */
	std::function<void(int part, qulonglong at)> dragged;

	/* The drive as planned now. The chosen block stays chosen, found by
	 * where it starts. */
	void set(const DiskDrive &planned);
	const Row *chosen() const;
	/* The row showing partition `part`, or -1. */
	int rowOfPart(int part) const;
	void select(int row, bool notify);

	void paint(pl_canvas *c) const;
	bool press(QPoint p);
	bool move(QPoint p);
	bool release();
	bool key(int key); /* up and down arrows */
	/* Over a line that can be dragged (for the cursor), or dragging one. */
	bool onLine(QPoint p) const;
	bool dragging() const { return m_drag >= 0; }

private:
	QList<int> m_tops; /* each row's top, and the bottom after the last */
	int m_drag = -1;   /* the partition whose line is being dragged */
	int m_grab = 0;    /* how far below the line it was pressed */
	/* Frozen when the drag starts: these pixels show these bytes. */
	int m_fromY = 0, m_toY = 0;
	qulonglong m_fromByte = 0, m_toByte = 0;
	DiskSpan m_limits;

	void layout();
	qulonglong rowStart(int row) const;
	qulonglong rowEnd(int row) const;
	int lineAt(QPoint p) const; /* the partition whose draggable line is at p, or -1 */
};
