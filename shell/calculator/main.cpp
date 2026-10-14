/*
 * zacos9-calculator: the Calculator desk accessory (Missing Manual ch. 13):
 * a display and a keypad, clicked or typed. ⌘C copies the display, ⌘V
 * pastes a number in; Escape is C (Clear), Return and Enter are =.
 */
#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWidget>

#include "engine.h"
#include "pixels.h"
#include "settings.h"
#include "widgets.h"

namespace {

constexpr int MARGIN = 8, KEY_W = 32, KEY_H = 24, GAP = 4, DISPLAY_H = 24;

/* The keypad, as the Mac's: + is tall and 0 is wide. */
struct Key {
	QChar key;
	const char *label;
	int col, row, cols, rows;
};
const Key KEYS[] = {
	{ 'C', "C", 0, 0, 1, 1 }, { 'E', "E", 1, 0, 1, 1 }, { '=', "=", 2, 0, 1, 1 }, { '*', "*", 3, 0, 1, 1 },
	{ '7', "7", 0, 1, 1, 1 }, { '8', "8", 1, 1, 1, 1 }, { '9', "9", 2, 1, 1, 1 }, { '/', "/", 3, 1, 1, 1 },
	{ '4', "4", 0, 2, 1, 1 }, { '5', "5", 1, 2, 1, 1 }, { '6', "6", 2, 2, 1, 1 }, { '-', "-", 3, 2, 1, 1 },
	{ '1', "1", 0, 3, 1, 1 }, { '2', "2", 1, 3, 1, 1 }, { '3', "3", 2, 3, 1, 1 }, { '+', "+", 3, 3, 1, 2 },
	{ '0', "0", 0, 4, 2, 1 }, { '.', ".", 2, 4, 1, 1 },
};

QRect keyRect(const Key &k) {
	const int top = MARGIN + DISPLAY_H + MARGIN;
	return QRect(MARGIN + k.col * (KEY_W + GAP), top + k.row * (KEY_H + GAP),
		k.cols * KEY_W + (k.cols - 1) * GAP, k.rows * KEY_H + (k.rows - 1) * GAP);
}

class Calculator : public QWidget {
public:
	Calculator() {
		setWindowTitle("Calculator");
		setFixedSize(2 * MARGIN + 4 * KEY_W + 3 * GAP,
			MARGIN + DISPLAY_H + MARGIN + 5 * KEY_H + 4 * GAP + MARGIN);
		m_flash.setSingleShot(true);
		m_flash.callOnTimeout([this] { m_lit = -1; update(); });
	}

protected:
	void paintEvent(QPaintEvent *) override {
		Pixels px(width(), height());
		pl_canvas *c = &px.c;
		pl_fill(c, 0, 0, width() - 1, height() - 1, GRAY(0xD));
		/* The display: a white field, the number at its right. */
		const QRect d(MARGIN, MARGIN, width() - 2 * MARGIN, DISPLAY_H);
		pl_edit_frame_paint(c, d.left(), d.top(), d.right(), d.bottom());
		Text shown(m_engine.display(), d.width() - 10, PL_FONT_SYSTEM);
		pl_text(c, shown.t, d.right() - 5 - shown.inkWidth(), d.top() + 17, C_BLACK);
		for (int i = 0; i < static_cast<int>(std::size(KEYS)); i++) {
			const QRect r = keyRect(KEYS[i]);
			const bool down = i == m_lit || (i == m_pressed && m_inside);
			pl_bevel_button_paint(c, r.x(), r.y(), r.width(), r.height(), down);
			Text t(KEYS[i].label, r.width(), PL_FONT_SYSTEM);
			pl_text(c, t.t, r.center().x() - t.inkWidth() / 2 + down, r.center().y() + 5 + down, C_BLACK);
		}
		QPainter p(this);
		px.blit(p);
	}

	void mousePressEvent(QMouseEvent *e) override {
		m_pressed = keyAt(e->position().toPoint());
		m_inside = m_pressed >= 0;
		update();
	}
	void mouseMoveEvent(QMouseEvent *e) override {
		const bool inside = m_pressed >= 0 && keyRect(KEYS[m_pressed]).contains(e->position().toPoint());
		if (inside != m_inside) {
			m_inside = inside;
			update();
		}
	}
	void mouseReleaseEvent(QMouseEvent *) override {
		if (m_pressed >= 0 && m_inside) {
			m_engine.press(KEYS[m_pressed].key);
		}
		m_pressed = -1;
		m_inside = false;
		update();
	}

	void keyPressEvent(QKeyEvent *e) override {
		if (e->modifiers() & Qt::ControlModifier) {
			if (e->key() == Qt::Key_C) {
				QApplication::clipboard()->setText(m_engine.display());
			} else if (e->key() == Qt::Key_V) {
				if (!m_engine.enter(QApplication::clipboard()->text())) {
					pl_beep();
				}
				update();
			} else if (e->key() == Qt::Key_W || e->key() == Qt::Key_Q) {
				close();
			}
			return;
		}
		QChar key;
		switch (e->key()) {
		case Qt::Key_Return:
		case Qt::Key_Enter: key = '='; break;
		case Qt::Key_Escape:
		case Qt::Key_Clear: key = 'C'; break;
		case Qt::Key_Backspace: key = '\b'; break;
		default: key = e->text().isEmpty() ? QChar() : e->text().at(0).toUpper(); break;
		}
		if (key.isNull() || !m_engine.press(key)) {
			QWidget::keyPressEvent(e);
			return;
		}
		/* The key it stands for lights up, as if clicked. */
		for (int i = 0; i < static_cast<int>(std::size(KEYS)); i++) {
			if (KEYS[i].key == key) {
				m_lit = i;
				m_flash.start(120);
			}
		}
		update();
	}

private:
	int keyAt(QPoint p) const {
		for (int i = 0; i < static_cast<int>(std::size(KEYS)); i++) {
			if (keyRect(KEYS[i]).contains(p)) {
				return i;
			}
		}
		return -1;
	}

	CalcEngine m_engine;
	int m_pressed = -1, m_lit = -1;
	bool m_inside = false;
	QTimer m_flash;
};

} // namespace

int main(int argc, char *argv[]) {
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Calculator");
	QGuiApplication::setDesktopFileName("zacos9-calculator");
	Calculator calculator;
	calculator.show();
	return app.exec();
}
