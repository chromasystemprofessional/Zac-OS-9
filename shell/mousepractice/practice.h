#pragma once

/*
 * Mouse Practice's lessons, without a window: point at the bone, click
 * it, double-click the doghouse, drag the ball to Zac. The window passes
 * the pointer's events in and draws what's here, so a test can play the
 * whole game. Two wordings, as the tutorial has (docs/tutorial.md): the
 * newcomers' and Puppy School's (kids), which also keeps the targets big.
 */

#include <QPointF>
#include <QRandomGenerator>
#include <QRectF>
#include <QSizeF>
#include <QString>

class Practice {
public:
	enum Step { Point, Click, DoubleClick, Drag, Done };

	Practice(const QSizeF &area, bool kids, quint32 seed = QRandomGenerator::global()->generate());

	void restart();
	void setKids(bool kids);
	bool kids() const { return m_kids; }

	/* The pointer, in the area's coordinates. A double-click's second
	 * press comes as doubleClick, not press. */
	void move(const QPointF &at);
	void press(const QPointF &at);
	void release(const QPointF &at);
	void doubleClick(const QPointF &at);

	Step step() const { return m_step; }
	int goals() const;                 /* how many this step asks for */
	int reached() const { return m_reached; }
	int score() const { return m_score; } /* every goal reached so far: the window chimes when it grows */

	QRectF target() const { return m_target; } /* bone, doghouse or ball */
	QRectF goal() const { return m_goal; }     /* Zac, while dragging */
	bool holding() const { return m_holding; }

	QString prompt() const;              /* what to do */
	QString feedback() const { return m_feedback; } /* how the last try went */

	static QString stepName(Step s);

private:
	QSizeF m_area;
	bool m_kids;
	QRandomGenerator m_rng;
	Step m_step = Point;
	int m_reached = 0, m_score = 0, m_singles = 0;
	QRectF m_target, m_goal;
	QPointF m_pointer, m_grab;
	bool m_holding = false, m_pressedOn = false, m_ignoreRelease = false;
	QString m_feedback;

	void begin(Step s);
	void reachedOne(const QString &well);
	void place();
	double size() const;
};
