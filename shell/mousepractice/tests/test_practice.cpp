/* Mouse Practice's lessons (shell/mousepractice/practice.cpp), played by
 * hand: each step's goals, misses and what they say, the double-click's
 * single clicks, drags that miss Zac, both wordings. */
#include <QTextStream>

#include "practice.h"

static int fails;
static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

static const QSizeF AREA(640, 356);

/* A point well away from the target, inside the area. */
static QPointF away(const Practice &p) {
	const QRectF t = p.target();
	return QPointF(t.center().x() < AREA.width() / 2 ? AREA.width() - 5 : 5,
		t.center().y() < AREA.height() / 2 ? AREA.height() - 5 : 5);
}

static void click(Practice &p, const QPointF &at) {
	p.move(at);
	p.press(at);
	p.release(at);
}

static void doubleClick(Practice &p, const QPointF &at) {
	p.move(at);
	p.press(at);
	p.release(at);
	p.doubleClick(at);
	p.release(at);
}

static void drag(Practice &p, const QPointF &to) {
	const QPointF from = p.target().center();
	p.move(from);
	p.press(from);
	p.move((from + to) / 2);
	p.move(to);
	p.release(to);
}

static bool inside(const Practice &p) {
	return QRectF(QPointF(0, 0), AREA).contains(p.target());
}

int main() {
	for (quint32 seed = 1; seed <= 20; ++seed) {
		Practice p(AREA, false, seed);
		bool ok = p.step() == Practice::Point && inside(p);
		for (int i = 0; ok && i < 3; ++i) {
			p.move(away(p));
			ok = p.reached() == i;
			const QPointF at = p.target().center();
			p.move(at);
			ok = ok && inside(p) && !p.target().contains(at);
		}
		check(ok && p.step() == Practice::Click && p.score() == 3,
			QString("seed %1: pointing at the bone three times, each time somewhere new").arg(seed));
	}

	Practice p(AREA, false, 7);
	for (int i = 0; i < 3; ++i) {
		p.move(p.target().center());
	}
	check(p.step() == Practice::Click && p.reached() == 0 && p.goals() == 5 && p.prompt().contains("click the bone"),
		"then clicking");
	const double first = p.target().width();
	click(p, away(p));
	check(p.reached() == 0 && p.feedback().startsWith("Missed"), "a click beside the bone misses, and says so");
	const QPointF on = p.target().center();
	p.move(on);
	p.press(on);
	p.release(away(p));
	check(p.reached() == 0 && p.feedback().contains("moved off"), "pressing on it and letting go off it doesn't count");
	click(p, p.target().center());
	check(p.reached() == 1 && p.feedback() == "Good click." && p.target().width() < first,
		"a click on it counts, and the next bone is smaller");
	for (int i = 0; i < 4; ++i) {
		click(p, p.target().center());
	}
	check(p.step() == Practice::DoubleClick && p.score() == 8, "five clicks, then double-clicking");

	click(p, p.target().center());
	click(p, p.target().center());
	check(p.reached() == 0 && p.feedback() == "That was one click. Click twice, quickly.",
		"a single click on the doghouse isn't a double-click");
	click(p, p.target().center());
	check(p.feedback().contains("Mouse control panel"), "after three, the Mouse control panel is suggested");
	doubleClick(p, away(p));
	check(p.reached() == 0, "a double-click beside the doghouse doesn't open it");
	doubleClick(p, p.target().center());
	check(p.reached() == 1 && p.feedback().startsWith("It opened"),
		"a double-click opens it; its first click and last release don't count as more");
	doubleClick(p, p.target().center());
	doubleClick(p, p.target().center());
	check(p.step() == Practice::Drag && !p.goal().isEmpty() && !p.goal().intersects(p.target()),
		"three, then dragging: the ball away from Zac");

	const QRectF start = p.target();
	drag(p, QPointF(10, 10));
	check(p.reached() == 0 && !p.holding() && p.target() != start && p.feedback().contains("over Zac"),
		"a ball dropped off Zac stays where it's dropped");
	p.move(away(p));
	p.press(QPointF(-50, -50));
	check(!p.holding() && p.feedback().contains("on the ball first"), "pressing off the ball holds nothing");
	p.release(QPointF(-50, -50));
	drag(p, p.goal().center());
	check(p.reached() == 1 && !p.goal().contains(p.target().center()), "dropped on Zac it counts, and a new ball comes");
	drag(p, p.goal().center());
	drag(p, p.goal().center());
	check(p.step() == Practice::Done && p.score() == 14 && p.prompt().startsWith("Well done"), "all done");
	click(p, QPointF(100, 100));
	check(p.step() == Practice::Done && p.score() == 14, "nothing more to do");
	p.restart();
	check(p.step() == Practice::Point && p.score() == 0 && p.feedback().isEmpty(), "Start Over");

	Practice kids(AREA, true, 3);
	check(kids.prompt().contains("bone") && kids.prompt() != p.prompt() && kids.target().width() == 110,
		"Puppy School: its own wording, big targets");
	for (int i = 0; i < 3; ++i) {
		kids.move(kids.target().center());
	}
	click(kids, kids.target().center());
	check(kids.feedback() == "Caught it!" && kids.target().width() == 110, "and they stay big");
	kids.setKids(false);
	check(kids.target().width() < 110 && kids.prompt().startsWith("Now click"), "switching wording keeps the place");
	return fails ? 1 : 0;
}
