#include "alert.h"

#include <QKeyEvent>
#include <QMouseEvent>

#include "platinumshell.h"
#include "widgets.h"

static constexpr int MARGIN = 13;         /* window edge to content (estimate) */
static constexpr int TEXT_X = MARGIN + PL_ICON_LARGE + 15;
static constexpr int TEXT_W = 280;
static constexpr int LINE_H = 16;         /* HIG: 12-point Chicago is 16 px */
static constexpr int BUTTON_GAP = 12;     /* HIG: between push buttons */
static constexpr int BUTTON_TEXT_PAD = 14;

/* Greedy word wrap with the system font. */
static std::vector<std::unique_ptr<Text>> wrap(const QString &message, int width) {
	std::vector<std::unique_ptr<Text>> lines;
	const QStringList words = message.split(' ', Qt::SkipEmptyParts);
	QString line;
	for (const QString &word : words) {
		QString candidate = line.isEmpty() ? word : line + ' ' + word;
		Text probe(candidate, 100000, PL_FONT_SYSTEM);
		if (!line.isEmpty() && probe.inkWidth() > width) {
			lines.push_back(std::make_unique<Text>(line, width, PL_FONT_SYSTEM));
			line = word;
		} else {
			line = candidate;
		}
	}
	if (!line.isEmpty()) {
		lines.push_back(std::make_unique<Text>(line, width, PL_FONT_SYSTEM));
	}
	return lines;
}

Alert::Alert(const QString &message, const QString &okLabel, const QString &cancelLabel) {
	setWindowTitle(" "); /* stripes all the way across */
	m_lines = wrap(message, TEXT_W);

	const int textBottom = MARGIN + static_cast<int>(m_lines.size()) * LINE_H;
	const int contentBottom = std::max(MARGIN + PL_ICON_LARGE, textBottom);
	const int W = TEXT_X + TEXT_W + MARGIN;
	const int H = contentBottom + 16 + PL_BUTTON_H + MARGIN;
	setFixedSize(W, H);

	auto addButton = [&](const QString &label, bool isDefault) {
		Button b;
		b.label = std::make_unique<Text>(label, 200, PL_FONT_SYSTEM);
		int w = std::max(PL_BUTTON_MIN_W, b.label->inkWidth() + 2 * BUTTON_TEXT_PAD);
		b.rect = QRect(0, H - MARGIN - PL_BUTTON_H, w, PL_BUTTON_H);
		b.isDefault = isDefault;
		m_buttons.push_back(std::move(b));
	};
	if (!cancelLabel.isEmpty()) {
		addButton(cancelLabel, false);
	}
	addButton(okLabel, true);
	/* Right-aligned, default button rightmost. */
	int x = W - MARGIN;
	for (auto it = m_buttons.rbegin(); it != m_buttons.rend(); ++it) {
		x -= it->rect.width();
		it->rect.moveLeft(x);
		x -= BUTTON_GAP;
	}
}

bool Alert::ask(const QString &message, const QString &ok, const QString &cancel) {
	Alert alert(message, ok, cancel);
	return alert.exec() == QDialog::Accepted;
}

void Alert::showEvent(QShowEvent *e) {
	QDialog::showEvent(e);
	platinumSetFrameStyle(this, FrameStyle::MovableModal);
}

void Alert::paintEvent(QPaintEvent *) {
	Pixels px(width(), height());
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, width() - 1, height() - 1, GRAY(0xD));
	pl_icon_paint(c, MARGIN, MARGIN, PL_ICON_CAUTION, PL_ICON_LARGE, false);
	int baseline = MARGIN + 12;
	for (auto &line : m_lines) {
		pl_text(c, line->t, TEXT_X, baseline, C_BLACK);
		baseline += LINE_H;
	}
	for (size_t i = 0; i < m_buttons.size(); i++) {
		const Button &b = m_buttons[i];
		unsigned flags = b.isDefault ? PL_BUTTON_DEFAULT : 0;
		if (static_cast<int>(i) == m_tracking && m_inside) {
			flags |= PL_BUTTON_PRESSED;
		}
		pl_button_paint(c, b.rect.x(), b.rect.y(), b.rect.width(), b.label->t, flags);
	}
	QPainter p(this);
	px.blit(p);
}

int Alert::buttonAt(QPoint p) const {
	for (size_t i = 0; i < m_buttons.size(); i++) {
		if (m_buttons[i].rect.contains(p)) {
			return static_cast<int>(i);
		}
	}
	return -1;
}

void Alert::mousePressEvent(QMouseEvent *e) {
	m_tracking = buttonAt(e->position().toPoint());
	m_inside = m_tracking >= 0;
	update();
}

void Alert::mouseMoveEvent(QMouseEvent *e) {
	if (m_tracking < 0) {
		return;
	}
	/* Buttons track like the Mac's: pressed only while the pointer is in. */
	bool inside = m_buttons[m_tracking].rect.contains(e->position().toPoint());
	if (inside != m_inside) {
		m_inside = inside;
		update();
	}
}

void Alert::mouseReleaseEvent(QMouseEvent *) {
	int chosen = m_inside ? m_tracking : -1;
	m_tracking = -1;
	m_inside = false;
	update();
	if (chosen >= 0) {
		m_buttons[chosen].isDefault ? accept() : reject();
	}
}

void Alert::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		accept();
	} else if (e->key() == Qt::Key_Escape ||
			(e->key() == Qt::Key_Period && (e->modifiers() & Qt::ControlModifier))) {
		reject();
	}
}
