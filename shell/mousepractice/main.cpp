/*
 * zacos9-mousepractice: Mouse Practice, the first lesson of Learn ZacOS
 * (docs/tutorial.md): point at a bone, click it, double-click the
 * doghouse, drag the ball to Zac. Practice ▸ Puppy School has the
 * children's wording and bigger targets; it's on to begin with for a
 * Puppy Mode account, or with --kids.
 */

#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>

#include "platinumshell.h"
#include "practice.h"
#include "settings.h"

namespace {

constexpr int W = 640, H = 480, BAND = 84, BAR = 40;
constexpr int PLAY_H = H - BAND - BAR;

const QColor GRASS(0xC8, 0xE2, 0xB8), INK(0x22, 0x22, 0x22), SOFT(0x55, 0x55, 0x55);

void drawBone(QPainter &p, const QRectF &r) {
	const double k = r.width() * 0.13; /* the knobs' radius */
	QPainterPath bone;
	bone.addRoundedRect(QRectF(r.left() + k, r.center().y() - k * 0.6, r.width() - 2 * k, k * 1.2), 3, 3);
	for (const double x : { r.left() + k * 1.1, r.right() - k * 1.1 }) {
		for (const double dy : { -k * 0.75, k * 0.75 }) {
			QPainterPath knob;
			knob.addEllipse(QPointF(x, r.center().y() + dy), k, k);
			bone = bone.united(knob); /* one outline */
		}
	}
	p.setPen(QPen(INK, 2));
	p.setBrush(QColor(0xF6, 0xF0, 0xDE));
	p.drawPath(bone);
}

void drawDoghouse(QPainter &p, const QRectF &r) {
	const QRectF walls(r.left() + r.width() * 0.12, r.top() + r.height() * 0.42, r.width() * 0.76, r.height() * 0.55);
	p.setPen(QPen(INK, 2));
	p.setBrush(QColor(0xD9, 0xA4, 0x65));
	p.drawRect(walls);
	const QPolygonF roof{ QPointF(r.left(), walls.top() + 4), QPointF(r.center().x(), r.top() + 4),
		QPointF(r.right(), walls.top() + 4) };
	p.setBrush(QColor(0xB0, 0x30, 0x30));
	p.drawPolygon(roof);
	QPainterPath door;
	const QRectF d(walls.center().x() - walls.width() * 0.2, walls.top() + walls.height() * 0.3,
		walls.width() * 0.4, walls.height() * 0.7);
	door.moveTo(d.bottomLeft());
	door.lineTo(d.left(), d.top() + d.width() / 2);
	door.arcTo(QRectF(d.left(), d.top(), d.width(), d.width()), 180, -180);
	door.lineTo(d.bottomRight());
	door.closeSubpath();
	p.setBrush(INK);
	p.drawPath(door);
}

void drawBall(QPainter &p, const QRectF &r) {
	p.setPen(QPen(INK, 2));
	p.setBrush(QColor(0xE0, 0x40, 0x40));
	p.drawEllipse(r);
	/* A band round its middle: the top of a wider ellipse, cut to the ball. */
	p.save();
	QPainterPath ball;
	ball.addEllipse(r);
	p.setClipPath(ball);
	p.setPen(QPen(Qt::white, r.width() * 0.1));
	p.setBrush(Qt::NoBrush);
	p.drawEllipse(QRectF(r.left() - r.width() * 0.5, r.center().y() - r.height() * 0.1, r.width() * 2, r.height() * 1.4));
	p.restore();
	p.setPen(QPen(INK, 2));
	p.setBrush(Qt::NoBrush);
	p.drawEllipse(r);
}

class Board : public QWidget {
public:
	explicit Board(bool kids)
		: m_practice(QSizeF(W, PLAY_H), kids),
		  m_zac(QString::fromUtf8(pl_data_dir()) + "/boot/happy-zac-4x.png") {
		setFixedSize(W, H);
		setMouseTracking(true); /* pointing needs the pointer without a button */
		m_again = new QPushButton("Start Over", this);
		m_again->setGeometry(W / 2 - 60, BAND + PLAY_H - 56, 120, 28);
		m_again->hide();
		connect(m_again, &QPushButton::clicked, this, [this] { restart(); });
	}

	void restart() {
		m_practice.restart();
		m_score = 0;
		changed();
	}
	void setKids(bool kids) {
		m_practice.setKids(kids);
		changed();
	}

protected:
	void paintEvent(QPaintEvent *) override {
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		paintBand(p);
		p.fillRect(QRect(0, BAND, W, PLAY_H), GRASS);
		p.save();
		p.translate(0, BAND);
		paintPlay(p);
		p.restore();
		paintProgress(p);
	}

	void mouseMoveEvent(QMouseEvent *e) override {
		m_practice.move(play(e));
		changed();
	}
	void mousePressEvent(QMouseEvent *e) override {
		if (e->button() == Qt::LeftButton) {
			m_practice.press(play(e));
			changed();
		}
	}
	void mouseReleaseEvent(QMouseEvent *e) override {
		if (e->button() == Qt::LeftButton) {
			m_practice.release(play(e));
			changed();
		}
	}
	void mouseDoubleClickEvent(QMouseEvent *e) override {
		if (e->button() == Qt::LeftButton) {
			m_practice.doubleClick(play(e));
			changed();
		}
	}

private:
	Practice m_practice;
	QImage m_zac;
	QPushButton *m_again;
	int m_score = 0;

	static QPointF play(QMouseEvent *e) {
		return e->position() - QPointF(0, BAND);
	}

	void changed() {
		if (m_practice.score() > m_score) {
			pl_sound_event("menu-command");
		}
		m_score = m_practice.score();
		m_again->setVisible(m_practice.step() == Practice::Done);
		update();
	}

	QFont sized(double scale, bool bold = false) const {
		QFont f = font();
		f.setPointSizeF(f.pointSizeF() * scale);
		f.setBold(bold);
		return f;
	}

	void paintBand(QPainter &p) {
		p.fillRect(QRect(0, 0, W, BAND), Qt::white);
		p.setPen(QColor(0x88, 0x88, 0x88));
		p.drawLine(0, BAND - 1, W, BAND - 1);
		const bool kids = m_practice.kids();
		p.setPen(INK);
		p.setFont(sized(kids ? 1.6 : 1.25, kids));
		const QRect text(16, 8, W - 32, BAND - 16);
		QRect used;
		p.drawText(text, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, m_practice.prompt(), &used);
		if (!m_practice.feedback().isEmpty()) {
			p.setPen(SOFT);
			p.setFont(sized(1.0));
			p.drawText(QRect(16, used.bottom() + 4, W - 32, BAND - used.bottom() - 8),
				Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, m_practice.feedback());
		}
	}

	void paintPlay(QPainter &p) {
		switch (m_practice.step()) {
		case Practice::Point:
		case Practice::Click:
			drawBone(p, m_practice.target());
			break;
		case Practice::DoubleClick:
			drawDoghouse(p, m_practice.target());
			break;
		case Practice::Drag: {
			const QRectF goal = m_practice.goal();
			if (m_practice.holding()) {
				p.setPen(QPen(INK, 2, Qt::DashLine));
				p.setBrush(Qt::NoBrush);
				p.drawRoundedRect(goal.adjusted(-6, -6, 6, 6), 10, 10);
			}
			drawZac(p, goal);
			drawBall(p, m_practice.target());
			break;
		}
		case Practice::Done:
			drawZac(p, QRectF(W / 2 - 110, 24, 220, 220));
			break;
		}
	}

	void drawZac(QPainter &p, const QRectF &r) {
		if (m_zac.isNull()) {
			p.setPen(QPen(INK, 2));
			p.setBrush(QColor(0xF0, 0xD0, 0x90));
			p.drawEllipse(r);
			return;
		}
		const QImage fitted = m_zac.scaled(r.size().toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
		p.drawImage(QPointF(r.center().x() - fitted.width() / 2.0, r.center().y() - fitted.height() / 2.0), fitted);
	}

	/* The four steps along the bottom: done ones ticked, this one with
	 * a dot for each goal. */
	void paintProgress(QPainter &p) {
		const int top = H - BAR;
		p.fillRect(QRect(0, top, W, BAR), QColor(0xDD, 0xDD, 0xDD));
		p.setPen(QColor(0x88, 0x88, 0x88));
		p.drawLine(0, top, W, top);
		const int cell = W / 4;
		for (int i = 0; i < 4; ++i) {
			const auto s = Practice::Step(i);
			QString label = Practice::stepName(s);
			if (s < m_practice.step()) {
				label = "✓ " + label;
			} else if (s == m_practice.step()) {
				label += "  " + QString("●").repeated(m_practice.reached()) +
					QString("○").repeated(m_practice.goals() - m_practice.reached());
			}
			p.setPen(s <= m_practice.step() ? INK : QColor(0x99, 0x99, 0x99));
			p.setFont(sized(1.0, s == m_practice.step()));
			p.drawText(QRect(i * cell, top, cell, BAR), Qt::AlignCenter, label);
		}
	}
};

class Window : public QMainWindow {
public:
	explicit Window(bool kids) {
		setWindowTitle("Mouse Practice");
		auto *board = new Board(kids);
		setCentralWidget(board);
		QMenu *practice = menuBar()->addMenu("Practice");
		practice->addAction("Start Over", QKeySequence::New, board, [board] { board->restart(); });
		practice->addSeparator();
		QAction *puppy = practice->addAction("Puppy School");
		puppy->setCheckable(true);
		puppy->setChecked(kids);
		connect(puppy, &QAction::toggled, board, [board](bool on) { board->setKids(on); });
	}
};

} // namespace

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Mouse Practice");
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("zacos9-mousepractice"); /* Wayland app_id */
	platinumShellInit();
	Window w(app.arguments().contains("--kids") || pl_puppy_mode());
	w.show();
	return app.exec();
}
