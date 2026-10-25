#include "practice.h"

#include <QLineF>
#include <algorithm>

namespace {

constexpr int POINTS = 3, CLICKS = 5, DOUBLES = 3, DRAGS = 3;
constexpr double ZAC = 150, MARGIN = 12;

} // namespace

Practice::Practice(const QSizeF &area, bool kids, quint32 seed) : m_area(area), m_kids(kids), m_rng(seed) {
	restart();
}

void Practice::restart() {
	m_score = 0;
	m_feedback.clear();
	begin(Point);
}

void Practice::setKids(bool kids) {
	m_kids = kids;
	m_holding = m_pressedOn = false;
	if (m_step != Done) {
		place(); /* the targets' size goes with the wording */
	}
}

int Practice::goals() const {
	switch (m_step) {
	case Point: return POINTS;
	case Click: return CLICKS;
	case DoubleClick: return DOUBLES;
	case Drag: return DRAGS;
	case Done: break;
	}
	return 0;
}

void Practice::begin(Step s) {
	m_step = s;
	m_reached = m_singles = 0;
	m_holding = m_pressedOn = m_ignoreRelease = false;
	m_goal = s == Drag ? QRectF(m_area.width() - ZAC - MARGIN * 2, (m_area.height() - ZAC) / 2, ZAC, ZAC) : QRectF();
	if (s == Done) {
		m_target = QRectF();
	} else {
		place();
	}
}

void Practice::reachedOne(const QString &well) {
	++m_score;
	m_singles = 0;
	if (++m_reached < goals()) {
		m_feedback = well;
		place();
		return;
	}
	m_feedback = m_step == Drag ? QString()
		: m_kids ? QStringLiteral("Great job! Now something new.") : QStringLiteral("Well done. On to the next step.");
	begin(Step(m_step + 1));
}

double Practice::size() const {
	if (m_kids) {
		return m_step == Drag ? 90 : 110;
	}
	switch (m_step) {
	case Click: return std::max(52.0, 100.0 - 12 * m_reached); /* smaller each time */
	case Drag: return 70;
	default: return 100;
	}
}

/* Somewhere new, well away from the pointer, so it can't be reached
 * without moving; the ball starts on the left, away from Zac. */
void Practice::place() {
	const double s = size();
	const double right = m_step == Drag ? m_area.width() * 0.4 : m_area.width();
	QRectF r;
	for (int tries = 0; tries < 40; ++tries) {
		const double x = MARGIN + m_rng.bounded(std::max(1.0, right - s - 2 * MARGIN));
		const double y = MARGIN + m_rng.bounded(std::max(1.0, m_area.height() - s - 2 * MARGIN));
		r = QRectF(x, y, s, s);
		if (QLineF(r.center(), m_pointer).length() > s && QLineF(r.center(), m_target.center()).length() > s / 2) {
			break;
		}
	}
	m_target = r;
}

void Practice::move(const QPointF &at) {
	m_pointer = at;
	if (m_step == Point && m_target.contains(at)) {
		reachedOne(m_kids ? "You found it!" : "That's it: the arrow is on the bone.");
	} else if (m_step == Drag && m_holding) {
		m_target.moveTopLeft(at - m_grab);
	}
}

void Practice::press(const QPointF &at) {
	m_pointer = at;
	if (m_step == Click) {
		m_pressedOn = m_target.contains(at);
		if (!m_pressedOn) {
			m_feedback = m_kids ? "Oops, missed! Put the arrow on the bone first."
				: "Missed: the arrow has to be on the bone when you press.";
		}
	} else if (m_step == Drag) {
		m_holding = m_target.contains(at);
		m_grab = at - m_target.topLeft();
		if (!m_holding) {
			m_feedback = m_kids ? "Put the arrow on the ball, then hold the button down."
				: "Put the arrow on the ball first, then press and hold.";
		}
	}
}

void Practice::release(const QPointF &at) {
	m_pointer = at;
	if (m_ignoreRelease) { /* the end of a double-click */
		m_ignoreRelease = false;
		return;
	}
	switch (m_step) {
	case Click:
		if (m_pressedOn && m_target.contains(at)) {
			reachedOne(m_kids ? "Caught it!" : "Good click.");
		} else if (m_pressedOn) {
			m_feedback = m_kids ? "The arrow slipped off! Try to keep the mouse still."
				: "The arrow moved off the bone before you let go. Try to keep the mouse still while you click.";
		}
		m_pressedOn = false;
		break;
	case DoubleClick:
		if (!m_target.contains(at)) {
			break;
		}
		// One click. A double-click's first click lands here too, and the
		// second resets the count.
		m_feedback = m_kids ? "That was one click. Do two, really fast!" : "That was one click. Click twice, quickly.";
		if (++m_singles >= 3) {
			m_feedback += m_kids ? " A grown-up can make it easier in the Mouse control panel."
				: " If your double-clicks are too slow, the Mouse control panel sets how fast they must be.";
		}
		break;
	case Drag:
		if (!m_holding) {
			break;
		}
		m_holding = false;
		if (m_goal.contains(m_target.center())) {
			reachedOne(m_kids ? "Zac loves it! Again!" : "Dropped on Zac. That's a drag.");
		} else {
			m_feedback = m_kids ? "Almost! Let go when the ball is on Zac." : "Let go when the ball is over Zac.";
		}
		break;
	default:
		break;
	}
}

void Practice::doubleClick(const QPointF &at) {
	switch (m_step) {
	case DoubleClick:
		m_pointer = at;
		if (m_target.contains(at)) {
			m_ignoreRelease = true;
			reachedOne(m_kids ? "The doghouse opened! Woof!" : "It opened. That's a double-click.");
		} else {
			m_feedback = "Double-click on the doghouse itself.";
		}
		break;
	case Point:
		move(at);
		break;
	default:
		press(at); /* to clicking and dragging, just another press */
		break;
	}
}

QString Practice::prompt() const {
	switch (m_step) {
	case Point:
		return m_kids ? "Move the mouse to touch the bone with the arrow!"
			: "Move the mouse (or slide a finger on the trackpad) until the arrow on the screen touches the bone.";
	case Click:
		return m_kids ? "Click the bone to catch it! Press the button and let go."
			: "Now click the bone: with the arrow on it, press the mouse button once and let go.";
	case DoubleClick:
		return m_kids ? "Knock knock! Click the doghouse two times, really fast."
			: "Double-click the doghouse to open it: click twice, quickly, without moving the mouse. "
			  "This is how you open things in ZacOS.";
	case Drag:
		return m_kids ? "Give Zac the ball! Hold the button down on the ball, move it to Zac, then let go."
			: "Drag the ball to Zac: put the arrow on the ball, hold the button down, move the mouse, "
			  "then let go over Zac.";
	case Done:
		return m_kids ? "Good dog! You earned your Mouse badge!"
			: "Well done. You can point, click, double-click and drag: all you need to use ZacOS.";
	}
	return QString();
}

QString Practice::stepName(Step s) {
	switch (s) {
	case Point: return "Point";
	case Click: return "Click";
	case DoubleClick: return "Double-click";
	case Drag: return "Drag";
	case Done: break;
	}
	return QString();
}
