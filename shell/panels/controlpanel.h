#pragma once

/*
 * What zacos9-controlpanel's panels share: a fixed-width window of group
 * boxes holding sliders, radio buttons and checkboxes (ControlPanel), and
 * desktop.conf's settings. Each panel is a file of its own (mouse.cpp,
 * sound.cpp...) that registers itself at its end with PANEL()
 * (panelregistry.h).
 */
#include <QApplication>
#include <QCollator>
#include <QDate>
#include <QLocale>
#include <QSaveFile>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QWheelEvent>
#include <QWidget>
#include <clocale>
#include <numeric>

#include "panelkit.h"
#include "sharingclient.h"
#include "alert.h"
#include "savers.h"
#include "settings.h"
#include "soundclient.h"
#include "memory.h"
#include "startupdisk.h"
#include "users.h"
#include "platinumshell.h"
#include "defaultapps.h"
#include "functionkeys.h"
#include "xkblayouts.h"

#include "panelregistry.h"

static constexpr uint32_t FACE = GRAY(0xD);
static constexpr int W = 360;
static constexpr int M = 12; /* window margin to group boxes */
static constexpr int IN = 1 + PL_GROUP_MARGIN; /* group box line to its items */

inline int settingInt(const char *key, int fallback) {
	char v[32];
	return pl_setting(key, v, sizeof(v)) ? atoi(v) : fallback;
}

inline void setInt(const char *key, int value) {
	pl_setting_set(key, QByteArray::number(value).constData());
}

inline bool settingOn(const char *key, bool fallback) {
	char v[16];
	if (!pl_setting(key, v, sizeof(v))) {
		return fallback;
	}
	return strcmp(v, "true") == 0 || strcmp(v, "1") == 0;
}

inline void setOn(const char *key, bool on) {
	pl_setting_set(key, on ? "true" : "false");
}

/* A group box: its top line, its title, and its bottom line. */
struct Group {
	int top, bottom;
	const char *title;
};

/* A panel: groups of sliders, radio buttons and checkboxes, and an
 * optional extra painter and mouse handler for a custom area. */
class ControlPanel : public QWidget {
public:
	ControlPanel(const char *title, int height) {
		setWindowTitle(title);
		setFixedSize(W, height);
	}

protected:
	std::vector<Group> m_groups;
	std::vector<PanelSlider *> m_sliders;
	std::vector<PanelRadios *> m_radios;
	std::vector<PanelCheckbox *> m_checks;
	PanelSlider *m_focus = nullptr;
	std::vector<QString> m_notes; /* views-font lines at the bottom */

	virtual void paintExtra(pl_canvas *) {}
	virtual bool pressExtra(QPoint, bool /* doubleClick */) { return false; }
	virtual void keyExtra(QKeyEvent *) {}
	virtual void moveExtra(QPoint) {}
	virtual void releaseExtra(QPoint) {}
	/* Hint Bubbles for lists, pop-ups and buttons, by their rectangles. */
	std::vector<std::pair<const QRect *, QString>> m_hintRects;
	/* Hint Bubbles for controls the panel draws itself. */
	virtual QString hintExtra(QPoint) const { return {}; }

	bool event(QEvent *e) override {
		return panelHintEvent(this, e, [this](QPoint p) {
			for (const PanelSlider *s : m_sliders) {
				if (s->hitRect().contains(p) && !s->hint.isEmpty()) {
					return s->hint;
				}
			}
			for (const PanelRadios *r : m_radios) {
				if (r->hit(p) >= 0 && !r->hint.isEmpty()) {
					return r->hint;
				}
			}
			for (const PanelCheckbox *k : m_checks) {
				if (k->hitRect().contains(p) && !k->hint.isEmpty()) {
					return k->hint;
				}
			}
			for (const auto &[r, text] : m_hintRects) {
				if (r->contains(p)) {
					return text;
				}
			}
			return hintExtra(p);
		}) || QWidget::event(e);
	}

	void paintEvent(QPaintEvent *) override {
		const int h = height();
		Pixels px(W, h);
		pl_canvas *c = &px.c;
		pl_fill(c, 0, 0, W - 1, h - 1, FACE);
		for (const Group &g : m_groups) {
			panelGroup(c, M, g.top, W - M - 2, g.bottom, g.title, FACE);
		}
		for (PanelSlider *s : m_sliders) {
			s->paint(c);
		}
		for (PanelRadios *r : m_radios) {
			r->paint(c);
		}
		for (PanelCheckbox *k : m_checks) {
			k->paint(c);
		}
		paintExtra(c);
		int y = h - 8 - 12 * (static_cast<int>(m_notes.size()) - 1);
		for (const QString &note : m_notes) {
			Text t(note, W - 2 * M, PL_FONT_VIEWS);
			pl_text(c, t.t, M, y, GRAY(0x5));
			y += 12;
		}
		QPainter p(this);
		px.blit(p);
	}

	void mousePressEvent(QMouseEvent *e) override {
		const QPoint pos = e->position().toPoint();
		for (PanelSlider *s : m_sliders) {
			if (s->press(pos)) {
				m_focus = s;
				update();
				return;
			}
		}
		for (PanelRadios *r : m_radios) {
			if (r->press(pos)) {
				update();
				return;
			}
		}
		for (PanelCheckbox *k : m_checks) {
			if (k->press(pos)) {
				update();
				return;
			}
		}
		if (pressExtra(pos, false)) {
			update();
		}
	}

	void mouseDoubleClickEvent(QMouseEvent *e) override {
		if (pressExtra(e->position().toPoint(), true)) {
			update();
		} else {
			mousePressEvent(e);
		}
	}

	void mouseMoveEvent(QMouseEvent *e) override {
		const QPoint pos = e->position().toPoint();
		bool changed = false;
		for (PanelSlider *s : m_sliders) {
			changed |= s->move(pos);
		}
		for (PanelRadios *r : m_radios) {
			changed |= r->move(pos);
		}
		for (PanelCheckbox *k : m_checks) {
			changed |= k->move(pos);
		}
		moveExtra(pos);
		if (changed) {
			update();
		}
	}

	void mouseReleaseEvent(QMouseEvent *e) override {
		const QPoint pos = e->position().toPoint();
		for (PanelSlider *s : m_sliders) {
			s->release(pos);
		}
		for (PanelRadios *r : m_radios) {
			r->release(pos);
		}
		for (PanelCheckbox *k : m_checks) {
			k->release(pos);
		}
		releaseExtra(pos);
		update();
	}

	void keyPressEvent(QKeyEvent *e) override {
		if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
			close();
			return;
		}
		if (m_focus && m_focus->key(e->key())) {
			update();
			return;
		}
		keyExtra(e);
	}

	/* A slider in a group starting at `top`, its labels under its ends. */
	void placeSlider(PanelSlider &s, int top, int steps, int value, const QString &left,
			const QString &right) {
		s.pos = QPoint(M + IN, top + 1 + PL_GROUP_MARGIN_TOP);
		s.width = W - 2 * M - 2 * IN - 2;
		s.steps = steps;
		s.value = std::clamp(value, 0, steps - 1);
		s.setLabels(left, right);
		m_sliders.push_back(&s);
	}
};

/* A group's height for `rows` rows of radio buttons or checkboxes. */
static constexpr int ROWS(int rows) {
	return 1 + PL_GROUP_MARGIN_TOP + 12 + PL_GROUP_MARGIN + (rows - 1) * 20;
}

/* A group holding one slider and its labels. */
static constexpr int SLIDER_GROUP_H = 1 + PL_GROUP_MARGIN_TOP + PL_SLIDER_H + 14 + PL_GROUP_MARGIN;
