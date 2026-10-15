/*
 * zacos9-controlpanel: the Mouse, Trackpad, Keyboard, Sound, Monitors, Energy
 * Saver, Screen Saver, Zac Menu Options, General Controls, Internet and
 * Accessibility control panels, one per run:
 *
 *   zacos9-controlpanel mouse | trackpad | keyboard | sound | monitors |
 *     energysaver | screensaver | zacmenu | general | internet | accessibility |
 *     startupdisk | users
 *
 * Each writes ~/.config/zacos9/desktop.conf; zacos9-wm applies the
 * mouse, keyboard, screen and sleep settings as they change (compositor/
 * src/prefs.c, idle.c). System volume goes through PulseAudio/PipeWire (pactl).
 * TODO: the layouts are ours, built from HIG controls and spacing.
 */
#include <QApplication>
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

#include "panelkit.h"
#include "savers.h"
#include "settings.h"
#include "soundclient.h"
#include "startupdisk.h"
#include "users.h"
#include "platinumshell.h"
#include "defaultapps.h"
#include "functionkeys.h"
#include "xkblayouts.h"

static constexpr uint32_t FACE = GRAY(0xD);
static constexpr int W = 360;
static constexpr int M = 12; /* window margin to group boxes */
static constexpr int IN = 1 + PL_GROUP_MARGIN; /* group box line to its items */

static int settingInt(const char *key, int fallback) {
	char v[32];
	return pl_setting(key, v, sizeof(v)) ? atoi(v) : fallback;
}

static void setInt(const char *key, int value) {
	pl_setting_set(key, QByteArray::number(value).constData());
}

static bool settingOn(const char *key, bool fallback) {
	char v[16];
	if (!pl_setting(key, v, sizeof(v))) {
		return fallback;
	}
	return strcmp(v, "true") == 0 || strcmp(v, "1") == 0;
}

static void setOn(const char *key, bool on) {
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

/* A group holding one slider and its labels. */
static constexpr int SLIDER_GROUP_H = 1 + PL_GROUP_MARGIN_TOP + PL_SLIDER_H + 14 + PL_GROUP_MARGIN;

/* ---- Mouse ------------------------------------------------------------------- */

class MousePanel : public ControlPanel {
public:
	MousePanel() : ControlPanel("Mouse", 230 + OPTIONS_H + 12 + 16) {
		const int g1 = 26, g2 = g1 + SLIDER_GROUP_H + 12 + 16;
		const int g3 = g2 + 1 + PL_GROUP_MARGIN_TOP + 52 + PL_GROUP_MARGIN + 12 + 16;
		m_groups = { { g1, g1 + SLIDER_GROUP_H, "Mouse Tracking" },
			{ g2, g2 + 1 + PL_GROUP_MARGIN_TOP + 52 + PL_GROUP_MARGIN, "Double-Click Speed" },
			{ g3, g3 + OPTIONS_H, "Options" } };

		/* The PowerBooks' options, for a pointer that's hard to follow:
		 * zacos9-wm draws both (compositor/src/tracks.c, input.c). */
		m_tracks = PanelCheckbox("Mouse Tracks", QPoint(M + IN, g3 + 1 + PL_GROUP_MARGIN_TOP));
		m_tracks.on = settingOn("mouse-tracks", false);
		m_tracks.toggled = [](bool on) { setOn("mouse-tracks", on); };
		m_ibeam = PanelCheckbox("Thick I-Beam", QPoint(M + IN, g3 + 1 + PL_GROUP_MARGIN_TOP + 20));
		m_ibeam.on = settingOn("thick-ibeam", false);
		m_ibeam.toggled = [](bool on) { setOn("thick-ibeam", on); };
		m_checks = { &m_tracks, &m_ibeam };
		placeSlider(m_tracking, g1, 7, settingInt("mouse-speed", 3), "Very Slow", "Fast");
		m_tracking.changed = [](int v) { setInt("mouse-speed", v); };

		/* Slow, medium and fast double-clicks (Mac OS's default is 533 ms). */
		const int y = g2 + 1 + PL_GROUP_MARGIN_TOP;
		m_speed.add("Slow", QPoint(M + IN, y));
		m_speed.add("Medium", QPoint(M + IN, y + 20));
		m_speed.add("Fast", QPoint(M + IN, y + 40));
		const int ms = settingInt("double-click", 533);
		m_speed.selected = ms > 650 ? 0 : ms < 450 ? 2 : 1;
		m_speed.changed = [](int i) {
			static const int times[] = { 800, 533, 333 };
			if (i == 1) {
				pl_setting_set("double-click", nullptr);
			} else {
				setInt("double-click", times[i]);
			}
		};
		m_radios.push_back(&m_speed);
		m_test = QRect(W / 2 + 10, y - 2, W / 2 - M - IN - 12, 52);
		m_flash.setSingleShot(true);
		m_flash.callOnTimeout([this] { m_lit = false; update(); });
	}

protected:
	void paintExtra(pl_canvas *c) override {
		/* A spot to try the speed: it lights up on a double-click. */
		const QRect r = m_test;
		pl_edit_frame_paint(c, r.left(), r.top(), r.right(), r.bottom());
		if (m_lit) {
			pl_fill(c, r.left() + 1, r.top() + 1, r.right() - 1, r.bottom() - 1,
				pl_highlight_current());
		}
		for (int i = 0; i < 2; i++) {
			Text t(i == 0 ? "Double-click" : "here to try it", r.width() - 8, PL_FONT_VIEWS);
			pl_text(c, t.t, r.center().x() - t.inkWidth() / 2, r.top() + 22 + 13 * i, C_BLACK);
		}
	}

	bool pressExtra(QPoint p, bool doubleClick) override {
		if (!doubleClick || !m_test.contains(p)) {
			return false;
		}
		m_lit = true;
		m_flash.start(400);
		return true;
	}

private:
	static constexpr int OPTIONS_H = 1 + PL_GROUP_MARGIN_TOP + 32 + PL_GROUP_MARGIN;
	PanelSlider m_tracking;
	PanelRadios m_speed;
	PanelCheckbox m_tracks, m_ibeam;
	QRect m_test;
	QTimer m_flash;
	bool m_lit = false;
};

/* ---- Keyboard ------------------------------------------------------------------ */

/* Mac OS 9's Keyboard panel: the layouts in use, ticked in a list of all
 * of them (zacos9-wm switches between them with ⌘-Space, and the menu bar
 * shows the keyboard menu), then key repeat. */
class KeyboardPanel : public ControlPanel {
public:
	KeyboardPanel() : ControlPanel("Keyboard", PANEL_H) {
		m_groups = { { G0, G0 + LAYOUTS_H, "Keyboard Layouts" },
			{ G1, G1 + SLIDER_GROUP_H, "Key Repeat Rate" },
			{ G2, G2 + SLIDER_GROUP_H, "Delay Until Repeat" } };

		/* The ones in use first, in their order, then the rest by name. */
		m_inUse = xkbLayoutsInUse();
		QList<XkbLayout> all = xkbLayouts();
		for (const QString &code : m_inUse) {
			const auto found = std::find_if(all.begin(), all.end(),
				[&](const XkbLayout &l) { return l.code == code; });
			m_layouts << (found != all.end() ? *found : XkbLayout{ code, code });
		}
		for (const XkbLayout &l : all) {
			if (!m_inUse.contains(l.code)) {
				m_layouts << l;
			}
		}
		QStringList names;
		for (const XkbLayout &l : m_layouts) {
			names << l.name;
		}
		m_list.frame = QRect(M + IN, G0 + 1 + PL_GROUP_MARGIN_TOP, W - 2 * M - 2 * IN - 2,
			LIST_ROWS * PL_LIST_ROW_H + 2);
		m_list.setItems(names, {}, PL_CHECKBOX_SIZE + 6);
		m_list.state.selected = -1;

		char v[16];
		m_option = PanelCheckbox("Switch with Command-Option-Space",
			QPoint(M + IN, m_list.frame.bottom() + 8));
		m_option.on = pl_setting("keyboard-switch", v, sizeof(v)) && strcmp(v, "option") == 0;
		m_option.toggled = [](bool on) { pl_setting_set("keyboard-switch", on ? "option" : nullptr); };
		m_checks.push_back(&m_option);

		placeSlider(m_rate, G1, 7, settingInt("key-repeat", 5), "Off", "Fast");
		m_rate.changed = [](int v) { setInt("key-repeat", v); };
		placeSlider(m_delay, G2, 6, settingInt("key-delay", 3), "Off", "Short");
		m_delay.changed = [](int v) { setInt("key-delay", v); };
		m_field = QRect(M + 2, G3 - 6, W - 2 * M - 4, PL_EDIT_H);
		m_caret.callOnTimeout([this] { m_caretOn = !m_caretOn; update(); });
		m_caret.start(pl_caret_blink_ms() / 2);
		m_fkeys = PanelButton("Function Keys…", QRect(W - M - 2 - 120, G3 - 6 + PL_EDIT_H + 12, 120, PL_BUTTON_H));
		m_fkeys.clicked = [this] { showFunctionKeys(this); };
	}

protected:
	void paintExtra(pl_canvas *c) override {
		m_list.paint(c, m_listFocus);
		const int visible = pl_list_visible_rows(m_list.frame.top(), m_list.frame.bottom());
		for (int r = 0; r < visible && m_list.state.top + r < m_layouts.size(); r++) {
			const int i = m_list.state.top + r;
			pl_checkbox_paint(c, m_list.frame.left() + 3,
				m_list.frame.top() + 1 + r * PL_LIST_ROW_H + 2,
				m_inUse.contains(m_layouts[i].code) ? PL_CHECK_ON : PL_CHECK_OFF,
				false, true, nullptr);
		}

		/* A field to try the settings in. */
		Text label("Try them here:", 200, PL_FONT_SYSTEM);
		pl_text(c, label.t, M + 2, m_field.top() - 8, C_BLACK);
		const QRect r = m_field;
		pl_edit_frame_paint(c, r.left(), r.top(), r.right(), r.bottom());
		Text t(m_text.right(60), r.width() - 10, PL_FONT_VIEWS);
		int caret = r.left() + 4;
		if (t.t && t.t->ink_l >= 0) {
			pl_text(c, t.t, r.left() + 4 + t.t->ink_l - 1, r.top() + 15, C_BLACK);
			caret += t.t->advance;
		}
		if (m_caretOn && !m_listFocus) {
			pl_vline(c, caret, r.top() + 4, r.bottom() - 4, C_BLACK);
		}
		m_fkeys.paint(c);
	}

	bool pressExtra(QPoint pos, bool) override {
		m_focus = nullptr;
		if (m_fkeys.press(pos)) {
			return true;
		}
		m_listFocus = m_list.frame.contains(pos);
		if (!m_listFocus) {
			return m_field.contains(pos);
		}
		const int before = m_list.state.selected;
		m_list.state.selected = -1;
		m_list.press(pos);
		const int row = m_list.state.selected;
		if (row >= 0) {
			toggle(row);
		} else {
			m_list.state.selected = before; /* the scroll bar */
		}
		return true;
	}

	void moveExtra(QPoint pos) override {
		if (m_list.move(pos) || m_fkeys.move(pos)) {
			update();
		}
	}

	void releaseExtra(QPoint pos) override {
		m_list.release();
		m_fkeys.release(pos);
	}

	void wheelEvent(QWheelEvent *e) override {
		if (m_list.wheel(e->position().toPoint(), e->angleDelta().y())) {
			update();
		}
	}

	void keyExtra(QKeyEvent *e) override {
		if (m_listFocus) {
			if (e->key() == Qt::Key_Space && m_list.state.selected >= 0) {
				toggle(m_list.state.selected);
			} else {
				m_list.key(e->key(), e->text());
			}
			update();
			return;
		}
		if (e->key() == Qt::Key_Backspace) {
			m_text.chop(1);
		} else if (!e->text().isEmpty() && e->text().at(0).isPrint()) {
			m_text += e->text();
		} else {
			return;
		}
		m_caretOn = true;
		update();
	}

private:
	/* Ticks or unticks a layout: one at least, four at most (xkb's limit). */
	void toggle(int row) {
		const QString code = m_layouts[row].code;
		if (m_inUse.contains(code) ? m_inUse.size() == 1 : m_inUse.size() >= 4) {
			pl_beep();
			return;
		}
		if (!m_inUse.removeOne(code)) {
			m_inUse << code;
		}
		pl_setting_set("keyboard-layouts", m_inUse.join(',').toUtf8().constData());
	}

	static constexpr int LIST_ROWS = 8;
	static constexpr int G0 = 26;
	static constexpr int LAYOUTS_H = 1 + PL_GROUP_MARGIN_TOP + LIST_ROWS * PL_LIST_ROW_H + 2 + 8 +
		PL_CHECKBOX_SIZE + PL_GROUP_MARGIN;
	static constexpr int G1 = G0 + LAYOUTS_H + 12 + 16, G2 = G1 + SLIDER_GROUP_H + 12 + 16;
	static constexpr int G3 = G2 + SLIDER_GROUP_H + 12 + 16;
	static constexpr int PANEL_H = G3 - 6 + PL_EDIT_H + 12 + PL_BUTTON_H + 12;
	PanelButton m_fkeys;
	QList<XkbLayout> m_layouts;
	QStringList m_inUse;
	PanelList m_list;
	PanelCheckbox m_option;
	bool m_listFocus = false;
	PanelSlider m_rate, m_delay;
	QRect m_field;
	QString m_text;
	QTimer m_caret;
	bool m_caretOn = true;
};

/* ---- Sound ---------------------------------------------------------------------- */

/* The system volume through PulseAudio or PipeWire's pulse server. */
class SoundPanel : public ControlPanel {
public:
	SoundPanel() : ControlPanel("Sound", 336), m_sound(this) {
		const int g0 = 26, g1 = 102, g2 = g1 + SLIDER_GROUP_H + 12 + 16 + 20;
		m_groups = { { g0, 78, "Sound Output" },
			{ g1, g1 + SLIDER_GROUP_H + 20, "Volume" },
			{ g2, g2 + SLIDER_GROUP_H, "Alert Volume" } };
		m_output.rect = QRect(M + IN, g0 + 1 + PL_GROUP_MARGIN_TOP,
			W - 2 * M - 2 * IN - 2, PL_POPUP_H);
		m_output.items = { PopupItem("Reading sound outputs...", false) };
		m_output.enabled = false;
		m_output.chosen = [this](int row) {
			if (row < 0 || row >= m_sound.outputs.size()) {
				return;
			}
			m_error.clear();
			m_sound.selectOutput(m_sound.outputs[row]);
		};

		/* Volume in eight steps, 0..7, as on the Mac. */
		placeSlider(m_volume, g1, 8, 0, "Quiet", "Loud");
		m_volume.enabled = false;
		m_volume.changed = [this](int v) {
			m_error.clear();
			m_volumeFeedbackReady = false;
			m_sound.setVolume(m_currentSink, v * 100 / 7);
		};
		m_sound.volumeApplied = [this] {
			if (m_volume.dragging) {
				m_volumeFeedbackReady = true;
			} else {
				pl_beep();
			}
		};
		m_mute = PanelCheckbox("Mute", QPoint(M + IN, g1 + SLIDER_GROUP_H + 2));
		m_mute.enabled = false;
		m_mute.toggled = [this](bool on) {
			m_error.clear();
			m_sound.setMuted(m_currentSink, on);
		};
		m_checks.push_back(&m_mute);

		placeSlider(m_alert, g2, 8, settingInt("alert-volume", 7), "Off", "Loud");
		m_alert.changed = [](int v) {
			setInt("alert-volume", v);
		};
		m_sound.changed = [this] { syncOutputs(); };
		m_sound.failed = [this](const QString &error) {
			m_error = error;
			syncOutputs();
		};
		m_refresh.callOnTimeout([this] { m_sound.refresh(); });
		m_refresh.start(2000);
		m_sound.refresh();
	}

protected:
	void paintExtra(pl_canvas *c) override {
		m_output.paint(c);
	}

	bool pressExtra(QPoint p, bool) override {
		m_focus = nullptr;
		return m_output.press(this, p);
	}

	void mouseReleaseEvent(QMouseEvent *e) override {
		const bool alert = m_alert.dragging;
		const bool volume = m_volume.dragging && m_volumeFeedbackReady;
		ControlPanel::mouseReleaseEvent(e);
		m_volumeFeedbackReady = false;
		if (alert || volume) {
			pl_beep();
		}
	}

private:
	void syncOutputs() {
		m_output.items.clear();
		int selected = -1;
		for (int i = 0; i < m_sound.outputs.size(); i++) {
			const SoundOutput &output = m_sound.outputs[i];
			m_output.items.emplace_back(output.label);
			if (output.current) {
				selected = i;
			}
		}
		if (m_output.items.empty()) {
			m_output.items.emplace_back("No sound outputs available", false);
		} else if (selected < 0) {
			m_output.items.emplace_back("Choose a sound output", false);
		}
		m_output.selected = selected < 0 ? static_cast<int>(m_output.items.size()) - 1 : selected;
		m_output.enabled = !m_sound.switching && !m_sound.outputs.isEmpty();
		m_volume.enabled = m_mute.enabled = !m_sound.switching && selected >= 0;
		m_currentSink.clear();
		if (selected >= 0) {
			const SoundOutput &output = m_sound.outputs[selected];
			m_currentSink = output.sink;
			if (!m_volume.dragging) {
				m_volume.value = (output.volume * 7 + 50) / 100;
			}
			if (!m_mute.down) {
				m_mute.on = output.muted;
			}
		}
		const QString note = !m_error.isEmpty() ? m_error
			: m_sound.switching ? "Changing sound output..."
			: m_sound.outputs.isEmpty() ? "No sound outputs are available."
			: "Choose built-in speakers, a monitor or another output.";
		m_notes.clear();
		const QStringList lines = panelWrap(note, W - 2 * M, PL_FONT_VIEWS);
		for (const QString &line : lines.mid(0, 3)) {
			m_notes.push_back(line);
		}
		update();
	}

	SoundClient m_sound;
	QTimer m_refresh;
	PanelPopup m_output;
	QString m_currentSink, m_error;
	bool m_volumeFeedbackReady = false;
	PanelSlider m_volume, m_alert;
	PanelCheckbox m_mute;
};

/* ---- Monitors ----------------------------------------------------------------------- */

/* One screen, as zacos9-wm publishes it. Positions and sizes of the screens in the
 * layout are in logical pixels (the screen's pixels divided by the pixel size). */
struct Screen {
	QString name;
	QSize px;
	int scale = 1;
	bool nested = false;
	QPoint pos;
	bool main = false;
	QList<QSize> modes;

	QSize logical() const { return QSize(px.width() / std::max(scale, 1), px.height() / std::max(scale, 1)); }
	QRect rect() const { return QRect(pos, logical()); }
};

class MonitorsPanel : public ControlPanel {
public:
	MonitorsPanel() : ControlPanel("Monitors", 236) {
		readOutputs();
		build();
	}

protected:
	void paintExtra(pl_canvas *c) override {
		m_list.paint(c, true);
		Text pixels("Pixel size:", 200, PL_FONT_SYSTEM);
		pl_text(c, pixels.t, M + IN + 190, m_resTop + 1 + PL_GROUP_MARGIN_TOP + 9, C_BLACK);
		if (multiple()) {
			paintArrangement(c);
		}
	}

	bool pressExtra(QPoint p, bool) override {
		if (multiple() && m_area.contains(p)) {
			return pressArrangement(p);
		}
		return m_list.press(p);
	}

	void moveExtra(QPoint p) override {
		if (m_drag == Drag::None) {
			return;
		}
		m_dragNow = p;
		if (m_drag == Drag::Display) {
			m_dragPos = wantFor(p - m_dragGrab);
			m_dragSnap = snapFor(m_dragPos);
		}
		update();
	}

	void releaseExtra(QPoint p) override {
		if (m_drag == Drag::None) {
			return;
		}
		m_dragNow = p;
		const Drag mode = m_drag;
		m_drag = Drag::None;
		if (mode == Drag::Bar) {
			const int to = displayAt(p);
			if (to >= 0 && !m_displays[to].main) {
				for (Screen &d : m_displays) {
					d.main = false;
				}
				m_displays[to].main = true;
				saveArrangement();
			}
		} else {
			dropDisplay();
		}
		update();
	}

	void keyExtra(QKeyEvent *e) override {
		if (m_list.key(e->key(), e->text())) {
			update();
		}
	}

private:
	enum class Drag { None, Display, Bar };

	bool multiple() const { return m_displays.size() > 1; }
	const Screen *selected() const { return m_displays.isEmpty() ? nullptr : &m_displays[m_sel]; }

	void build() {
		const int g0 = 26;
		int resTop = g0;
		const int listH = 7 * PL_LIST_ROW_H + 1;
		int height;
		m_groups.clear();
		m_checks.clear();
		if (multiple()) {
			m_area = QRect(M + IN, g0 + 1 + PL_GROUP_MARGIN_TOP, W - 2 * M - 2 * IN - 2, 92);
			const int arrBottom = m_area.bottom() + 8 + 14 + PL_GROUP_MARGIN;
			m_groups.push_back({ g0, arrBottom, "Arrangement" });
			m_mirror = std::make_unique<PanelCheckbox>("Mirror Displays",
				QPoint(M + IN, m_area.bottom() + 8));
			m_mirror->on = m_mirrored;
			m_mirror->toggled = [this](bool on) {
				pl_setting_set("mirror", on ? "1" : "0");
				refreshSoon();
			};
			m_checks.push_back(m_mirror.get());
			resTop = arrBottom + 12;
		}
		m_resTop = resTop;
		const int resBottom = resTop + 1 + PL_GROUP_MARGIN_TOP + listH + PL_GROUP_MARGIN;
		m_title = multiple() ? QByteArray("Resolution of Display ") + QByteArray::number(m_sel + 1)
			: QByteArray("Resolution");
		m_groups.push_back({ resTop, resBottom, m_title.constData() });
		m_list.frame = QRect(QPoint(M + IN, resTop + 1 + PL_GROUP_MARGIN_TOP),
			QPoint(M + IN + 170, resTop + 1 + PL_GROUP_MARGIN_TOP + listH));
		fillModes();
		m_list.picked = [this](int i) {
			const Screen *d = selected();
			const QSize s = d ? d->modes.value(i) : m_modes.value(i);
			const QByteArray value = QString("%1x%2").arg(s.width()).arg(s.height()).toUtf8();
			pl_setting_set(d ? ("display." + d->name.toUtf8() + ".resolution").constData() : "resolution",
				value.constData());
			refreshSoon();
		};

		/* Pixel size: whole-number scales only, so the pixel art stays sharp. */
		m_scale = PanelRadios();
		const int rx = M + IN + 190, ry = resTop + 1 + PL_GROUP_MARGIN_TOP + 18;
		m_scale.add("Normal", QPoint(rx, ry));
		m_scale.add("Large (2×)", QPoint(rx, ry + 20));
		m_scale.add("Larger (3×)", QPoint(rx, ry + 40));
		m_scale.selected = std::clamp(m_currentScale, 1, 3) - 1;
		m_scale.changed = [this](int i) {
			setInt("scale", i + 1);
			refreshSoon();
		};
		m_radios.clear();
		m_radios.push_back(&m_scale);
		m_notes = { "Changes take effect at once." };
		if (m_nested) {
			m_notes.insert(m_notes.begin(),
				"In a window (nested), the resolution is the window's size.");
		}
		height = resBottom + 14 + 12 * static_cast<int>(m_notes.size());
		setFixedSize(W, height);
	}

	/* The selected screen's resolutions. */
	void fillModes() {
		const Screen *d = selected();
		const QList<QSize> &modes = d ? d->modes : m_modes;
		const QSize size = d ? d->px : m_size;
		QStringList names;
		int current = 0;
		for (int i = 0; i < modes.size(); i++) {
			names << QString("%1 × %2").arg(modes[i].width()).arg(modes[i].height());
			if (modes[i] == size) {
				current = i;
			}
		}
		m_list.setItems(names);
		m_list.select(current, false);
	}

	/* ---- the arrangement -------------------------------------------------------- */

	/* The layout's extent, with half a screen of room round it to drag into. */
	QRect world() const {
		QRect all;
		int maxW = 0, maxH = 0;
		for (const Screen &d : m_displays) {
			all = all.isNull() ? d.rect() : all.united(d.rect());
			maxW = std::max(maxW, d.logical().width());
			maxH = std::max(maxH, d.logical().height());
		}
		return all.adjusted(-maxW / 2, -maxH / 2, maxW / 2, maxH / 2);
	}

	double factor() const {
		const QRect w = world();
		return std::min(double(m_area.width()) / std::max(w.width(), 1),
			double(m_area.height()) / std::max(w.height(), 1));
	}

	QPoint toPanel(QPoint wp) const {
		const QRect w = world();
		const double f = factor();
		const QPointF centre = QPointF(m_area.center()) - QPointF(w.width() * f / 2, w.height() * f / 2);
		return QPoint(int(centre.x() + (wp.x() - w.x()) * f), int(centre.y() + (wp.y() - w.y()) * f));
	}

	QRect miniRect(const Screen &d, QPoint pos) const {
		const double f = factor();
		const QPoint tl = toPanel(pos);
		return QRect(tl, QSize(std::max(int(d.logical().width() * f), 12),
			std::max(int(d.logical().height() * f), 10)));
	}

	QRect miniRect(int i) const {
		const Screen &d = m_displays[i];
		QPoint pos = d.pos;
		if (m_drag == Drag::Display && i == m_sel) {
			pos = m_dragPos;
		}
		return miniRect(d, pos);
	}

	/* The menu bar's strip along the top of the main screen. */
	QRect barRect(int i) const {
		const QRect r = miniRect(i);
		return QRect(r.left() + 1, r.top() + 1, r.width() - 2, 6);
	}

	int displayAt(QPoint p) const {
		for (int i = m_displays.size() - 1; i >= 0; i--) {
			if (miniRect(i).contains(p)) {
				return i;
			}
		}
		return -1;
	}

	void paintArrangement(pl_canvas *c) {
		pl_fill(c, m_area.left(), m_area.top(), m_area.right(), m_area.bottom(), GRAY(0xC));
		pl_outline(c, m_area.left() - 1, m_area.top() - 1, m_area.right() + 1, m_area.bottom() + 1,
			GRAY(0x8));
		const pl_accent accent = pl_accent_current();
		for (int pass = 0; pass < 2; pass++) {
			for (int i = 0; i < m_displays.size(); i++) {
				if ((i == m_sel) != (pass == 1)) {
					continue;
				}
				const QRect r = miniRect(i);
				pl_fill(c, r.left() + 2, r.top() + 2, r.right() + 2, r.bottom() + 2, GRAY(0x7));
				pl_fill(c, r.left(), r.top(), r.right(), r.bottom(), C_WHITE);
				pl_outline(c, r.left(), r.top(), r.right(), r.bottom(), C_BLACK);
				if (i == m_sel) {
					pl_outline(c, r.left() + 1, r.top() + 1, r.right() - 1, r.bottom() - 1, accent.dark);
					pl_outline(c, r.left() + 2, r.top() + 2, r.right() - 2, r.bottom() - 2, accent.dark);
				}
				const bool moving = m_drag == Drag::Bar && m_displays[i].main;
				if (m_displays[i].main && !moving) {
					paintBar(c, barRect(i));
				}
				Text number(QString::number(i + 1), 40, PL_FONT_SYSTEM);
				if (number.t && number.t->ink_l >= 0) {
					const int w = number.t->ink_r - number.t->ink_l + 1;
					pl_text(c, number.t, r.center().x() - w / 2 - number.t->ink_l, r.center().y() + 5,
						C_BLACK);
				}
			}
		}
		if (m_drag == Drag::Display) {
			/* Where it will land when let go. */
			const QRect r = miniRect(m_displays[m_sel], m_dragSnap);
			pl_outline(c, r.left(), r.top(), r.right(), r.bottom(), GRAY(0x5));
			pl_outline(c, r.left() + 1, r.top() + 1, r.right() - 1, r.bottom() - 1, GRAY(0x8));
		}
		if (m_drag == Drag::Bar) {
			paintBar(c, QRect(m_dragNow.x() - 14, m_dragNow.y() - 2, 28, 4));
		}
	}

	static void paintBar(pl_canvas *c, QRect r) {
		pl_fill(c, r.left(), r.top(), r.right(), r.bottom(), GRAY(0xD));
		pl_hline(c, r.left(), r.right(), r.bottom(), C_BLACK);
		pl_hline(c, r.left(), r.right(), r.top(), C_BLACK);
	}

	bool pressArrangement(QPoint p) {
		const int hit = displayAt(p);
		if (hit < 0) {
			return false;
		}
		/* The menu bar is picked up by its strip; a screen by anywhere else. */
		if (m_displays[hit].main && barRect(hit).adjusted(0, -1, 0, 2).contains(p)) {
			m_drag = Drag::Bar;
		} else {
			/* Where it is now, not where the last drag left m_dragPos: the offset of
			 * the grab inside it decides where it lands. */
			m_dragGrab = p - miniRect(m_displays[hit], m_displays[hit].pos).topLeft();
			m_dragPos = m_dragSnap = m_displays[hit].pos;
			m_drag = Drag::Display;
		}
		if (hit != m_sel) {
			m_sel = hit;
			build();
		}
		m_dragNow = p;
		return true;
	}

	/* Where a dragged screen's top left is in the layout, for where it is in the panel. */
	QPoint wantFor(QPoint panelTopLeft) const {
		const double f = factor();
		const QRect w = world();
		const QPoint origin = toPanel(QPoint(w.x(), w.y()));
		return QPoint(int((panelTopLeft.x() - origin.x()) / f) + w.x(),
			int((panelTopLeft.y() - origin.y()) / f) + w.y());
	}

	/* Where the dragged screen lands: touching another screen, like Mac OS's
	 * arrangement, at the nearest such place to where it is. */
	QPoint snapFor(QPoint want) const {
		const Screen &d = m_displays[m_sel];
		const QSize sz = d.logical();
		QPoint best = d.pos;
		long bestDist = -1;
		auto consider = [&](QPoint cand) {
			const QRect me(cand, sz);
			for (int i = 0; i < m_displays.size(); i++) {
				if (i != m_sel && me.intersects(m_displays[i].rect())) {
					return;
				}
			}
			const long dx = cand.x() - want.x(), dy = cand.y() - want.y();
			const long dist = dx * dx + dy * dy;
			if (bestDist < 0 || dist < bestDist) {
				bestDist = dist;
				best = cand;
			}
		};
		for (int i = 0; i < m_displays.size(); i++) {
			if (i == m_sel) {
				continue;
			}
			const QRect o = m_displays[i].rect();
			/* Slide along the touching edge, kept overlapping it, and snap to
			 * the edges' alignments when close. */
			const int xs[] = { want.x(), o.x(), o.right() + 1 - sz.width(),
				o.center().x() - sz.width() / 2 };
			const int ys[] = { want.y(), o.y(), o.bottom() + 1 - sz.height(),
				o.center().y() - sz.height() / 2 };
			for (int y : ys) {
				const int yy = std::clamp(y, o.y() - sz.height() + 40, o.bottom() + 1 - 40);
				consider(QPoint(o.right() + 1, yy));
				consider(QPoint(o.x() - sz.width(), yy));
			}
			for (int x : xs) {
				const int xx = std::clamp(x, o.x() - sz.width() + 40, o.right() + 1 - 40);
				consider(QPoint(xx, o.bottom() + 1));
				consider(QPoint(xx, o.y() - sz.height()));
			}
		}
		return best;
	}

	void dropDisplay() {
		m_displays[m_sel].pos = snapFor(wantFor(m_dragNow - m_dragGrab));
		saveArrangement();
	}

	void saveArrangement() {
		for (Screen &d : m_displays) {
			setInt(("display." + d.name.toUtf8() + ".x").constData(), d.pos.x());
			setInt(("display." + d.name.toUtf8() + ".y").constData(), d.pos.y());
			if (d.main) {
				pl_setting_set("main-display", d.name.toUtf8().constData());
			}
		}
		refreshSoon();
	}

	/* zacos9-wm applies a change and republishes the screens; look again when it has. */
	void refreshSoon() {
		QTimer::singleShot(700, this, [this] {
			if (m_drag != Drag::None) {
				return;
			}
			const QString keep = selected() ? selected()->name : QString();
			readOutputs();
			m_sel = 0;
			for (int i = 0; i < m_displays.size(); i++) {
				if (m_displays[i].name == keep) {
					m_sel = i;
				}
			}
			build();
			update();
		});
	}

	/* zacos9-wm publishes the screens and their modes. */
	void readOutputs() {
		m_displays.clear();
		m_modes.clear();
		QFile f(qEnvironmentVariable("XDG_RUNTIME_DIR") + "/zacos9-outputs-" +
			qEnvironmentVariable("WAYLAND_DISPLAY"));
		if (f.open(QIODevice::ReadOnly)) {
			for (const QByteArray &line : f.readAll().split('\n')) {
				const QList<QByteArray> w = line.split(' ');
				if (w.value(0) == "mirror") {
					m_mirrored = w.value(1) == "1";
				} else if (w.value(0) == "output") {
					Screen d;
					d.name = QString::fromUtf8(w.value(1));
					const QList<QByteArray> wh = w.value(2).split('x');
					d.px = QSize(wh.value(0).toInt(), wh.value(1).toInt());
					d.scale = w.value(4).toInt();
					d.nested = w.value(6) == "1";
					d.pos = QPoint(w.value(8).toInt(), w.value(10).toInt());
					d.main = w.value(12) == "1";
					m_displays << d;
				} else if (w.value(0) == "mode" && !m_displays.isEmpty()) {
					const QList<QByteArray> wh = w.value(1).split('@').value(0).split('x');
					const QSize s(wh.value(0).toInt(), wh.value(1).toInt());
					if (!m_displays.last().modes.contains(s)) {
						m_displays.last().modes << s;
					}
				}
			}
		}
		if (!m_displays.isEmpty()) {
			m_size = m_displays.first().px;
			m_currentScale = m_displays.first().scale;
			m_nested = m_displays.first().nested;
		}
		for (Screen &d : m_displays) {
			finishModes(d.modes, d.px);
		}
		/* No screens published (not under zacos9-wm): one window-sized one. */
		finishModes(m_modes, m_size);
		if (m_sel >= m_displays.size()) {
			m_sel = 0;
		}
	}

	void finishModes(QList<QSize> &modes, const QSize &current) {
		if (modes.isEmpty()) {
			/* A window has no modes of its own: offer the usual sizes,
			 * the Mac's own 832 x 624 and 1152 x 870 among them. */
			modes = { { 640, 480 }, { 800, 600 }, { 832, 624 }, { 1024, 768 },
				{ 1152, 870 }, { 1280, 720 }, { 1280, 800 }, { 1280, 1024 }, { 1440, 900 },
				{ 1600, 900 }, { 1920, 1080 } };
		}
		if (!current.isEmpty() && !modes.contains(current)) {
			modes.prepend(current);
		}
		std::sort(modes.begin(), modes.end(), [](const QSize &a, const QSize &b) {
			return a.width() != b.width() ? a.width() < b.width() : a.height() < b.height();
		});
	}

	QList<Screen> m_displays;
	QList<QSize> m_modes; /* with no screens published */
	QSize m_size;
	int m_currentScale = 1;
	bool m_nested = false;
	bool m_mirrored = false;
	int m_sel = 0;
	int m_resTop = 0;
	QByteArray m_title;
	QRect m_area;
	Drag m_drag = Drag::None;
	QPoint m_dragNow, m_dragGrab, m_dragPos, m_dragSnap;
	std::unique_ptr<PanelCheckbox> m_mirror;
	PanelList m_list;
	PanelRadios m_scale;
};

/* ---- Energy Saver ------------------------------------------------------------------ */

/* The slider's stops, in minutes; 0 is Never. */
static const int SLEEP_MINUTES[] = { 1, 2, 3, 5, 10, 15, 20, 30, 45, 60, 90, 120, 0 };
static constexpr int SLEEP_STEPS = sizeof(SLEEP_MINUTES) / sizeof(SLEEP_MINUTES[0]);

static int sleepStep(int minutes) {
	if (minutes <= 0) {
		return SLEEP_STEPS - 1;
	}
	int best = 0;
	for (int i = 0; i < SLEEP_STEPS - 1; i++) {
		if (std::abs(SLEEP_MINUTES[i] - minutes) < std::abs(SLEEP_MINUTES[best] - minutes)) {
			best = i;
		}
	}
	return best;
}

static QString sleepText(int step) {
	const int m = SLEEP_MINUTES[step];
	return m == 0 ? QStringLiteral("Never") : m == 1 ? QStringLiteral("1 minute") :
		m < 60 || m % 60 ? QStringLiteral("%1 minutes").arg(m) :
		m == 60 ? QStringLiteral("1 hour") : QStringLiteral("%1 hours").arg(m / 60);
}

/* When the display and the system sleep; zacos9-wm applies them
 * (compositor/src/idle.c). */
class EnergySaverPanel : public ControlPanel {
public:
	EnergySaverPanel() : ControlPanel("Nap Time", 202) {
		const int g1 = 26, g2 = g1 + SLIDER_GROUP_H + 12;
		m_groups = { { g1, g1 + SLIDER_GROUP_H, "Put the display to sleep when inactive for" },
			{ g2, g2 + SLIDER_GROUP_H, "Put the system to sleep when inactive for" } };
		placeSlider(m_display, g1, SLEEP_STEPS, sleepStep(settingInt("display-sleep", 10)),
			"1 min", "Never");
		m_display.changed = [](int v) { setInt("display-sleep", SLEEP_MINUTES[v]); };
		placeSlider(m_system, g2, SLEEP_STEPS, sleepStep(settingInt("system-sleep", 0)),
			"1 min", "Never");
		m_system.changed = [](int v) { setInt("system-sleep", SLEEP_MINUTES[v]); };
		m_notes = { "A window filling a screen keeps that screen awake,",
			"and the system too. Any key or the mouse wakes them." };
	}

protected:
	void paintExtra(pl_canvas *c) override {
		/* The chosen time, between the slider's end labels (at an end,
		 * that end's label says it already). */
		for (const PanelSlider *s : { &m_display, &m_system }) {
			if (s->value == 0 || s->value == SLEEP_STEPS - 1) {
				continue;
			}
			Text t(sleepText(s->value), 120, PL_FONT_VIEWS);
			pl_text(c, t.t, s->pos.x() + (s->width - t.inkWidth()) / 2,
				s->pos.y() + PL_SLIDER_H + 11, C_BLACK);
		}
	}

private:
	PanelSlider m_display, m_system;
};

/* ---- Screen Saver ------------------------------------------------------------------ */

/* "Start after" stops, in minutes. */
static const int SAVER_MINUTES[] = { 1, 2, 3, 5, 10, 15, 20, 30, 45, 60 };
static constexpr int SAVER_STEPS = sizeof(SAVER_MINUTES) / sizeof(SAVER_MINUTES[0]);

/* Which screen saver, with a live preview, and when it starts; zacos9-wm
 * starts zacos9-screensaver then (compositor/src/idle.c, lib/savers.c). */
class ScreenSaverPanel : public ControlPanel {
public:
	ScreenSaverPanel() : ControlPanel("Screen Saver", 278) {
		const int g1 = 26, rows = pl_saver_count() + 1;
		const int g1h = 1 + PL_GROUP_MARGIN_TOP + 20 * rows - 8 + PL_GROUP_MARGIN;
		const int g2 = g1 + g1h + 12;
		m_groups = { { g1, g1 + g1h, "Screen Saver" },
			{ g2, g2 + SLIDER_GROUP_H, "Start the screen saver when inactive for" } };
		const int y = g1 + 1 + PL_GROUP_MARGIN_TOP;
		m_which.add("None", QPoint(M + IN, y));
		for (int i = 0; i < pl_saver_count(); i++) {
			m_which.add(QString::fromUtf8(pl_saver_name(i)), QPoint(M + IN, y + 20 * (i + 1)));
		}
		char id[64];
		const bool set = pl_setting("screensaver", id, sizeof(id));
		m_which.selected = !set ? 1 : strcmp(id, "none") == 0 ? 0 : pl_saver_find(id) + 1;
		m_which.changed = [this](int i) {
			pl_setting_set("screensaver", i == 0 ? "none" : pl_saver_id(i - 1));
			m_after.enabled = i != 0;
			restartPreview();
		};
		m_radios.push_back(&m_which);
		m_preview = QRect(W - M - IN - 2 - 160, y - 2, 160, 100);

		int minutes = settingInt("screensaver-after", 5), step = 0;
		for (int i = 0; i < SAVER_STEPS; i++) {
			if (std::abs(SAVER_MINUTES[i] - minutes) < std::abs(SAVER_MINUTES[step] - minutes)) {
				step = i;
			}
		}
		placeSlider(m_after, g2, SAVER_STEPS, step, "1 min", "1 hour");
		m_after.changed = [](int v) { setInt("screensaver-after", SAVER_MINUTES[v]); };
		m_after.enabled = m_which.selected != 0;
		/* Locks once every screen shows it or sleeps, and before the system
		 * sleeps (compositor/src/lock.c, shell/lock/). */
		m_password = std::make_unique<PanelCheckbox>(QStringLiteral("Ask for my password to wake"),
			QPoint(M + 2, g2 + SLIDER_GROUP_H + 12));
		char v[8];
		m_password->on = pl_setting("require-password", v, sizeof(v)) && strcmp(v, "1") == 0;
		m_password->toggled = [](bool on) { pl_setting_set("require-password", on ? "1" : nullptr); };
		m_checks.push_back(m_password.get());
		m_notes = { "It shows until the display sleeps (Nap Time).",
			"A window filling a screen keeps it away from that one." };
		restartPreview();
		m_clock.start();
		m_tick.callOnTimeout([this] { update(m_preview); });
		m_tick.start(40);
	}

	~ScreenSaverPanel() override {
		pl_saver_free(m_saver);
	}

protected:
	void paintExtra(pl_canvas *c) override {
		const QRect r = m_preview;
		pl_outline(c, r.left() - 1, r.top() - 1, r.right() + 1, r.bottom() + 1, C_BLACK);
		pl_canvas inner = { c->px + r.top() * c->stride + r.left(), c->stride, 0, 0, r.width(), r.height() };
		if (m_saver) {
			pl_saver_draw(m_saver, &inner, m_clock.elapsed() / 1000.0);
		} else {
			pl_fill(&inner, 0, 0, r.width() - 1, r.height() - 1, GRAY(0x8));
		}
		if (m_after.value > 0 && m_after.value < SAVER_STEPS - 1) {
			const int m = SAVER_MINUTES[m_after.value];
			Text t(m == 1 ? QStringLiteral("1 minute") : QStringLiteral("%1 minutes").arg(m), 120, PL_FONT_VIEWS);
			pl_text(c, t.t, m_after.pos.x() + (m_after.width - t.inkWidth()) / 2,
				m_after.pos.y() + PL_SLIDER_H + 11, m_after.enabled ? C_BLACK : GRAY(0x8));
		}
	}

private:
	void restartPreview() {
		pl_saver_free(m_saver);
		m_saver = m_which.selected > 0 ?
			pl_saver_new(m_which.selected - 1, m_preview.width(), m_preview.height(), 9) : nullptr;
		m_clock.restart();
	}

	PanelRadios m_which;
	PanelSlider m_after;
	std::unique_ptr<PanelCheckbox> m_password;
	QRect m_preview;
	pl_saver *m_saver = nullptr;
	QElapsedTimer m_clock;
	QTimer m_tick;
};

/* ---- Zac Menu Options --------------------------------------------------------------- */

/* How many recent items the Zac menu lists (Mac OS 9's Apple Menu Options);
 * the menu bar reads them as it opens the menu (shell/menubar/recent.c). */
static const int RECENT_COUNTS[] = { 0, 5, 10, 15, 20, 30, 40 };

class ZacMenuOptionsPanel : public ControlPanel {
public:
	ZacMenuOptionsPanel() : ControlPanel("Zac Menu Options", PANEL_H) {
		m_groups = { { G, G + GROUP_H, "Remember recently used items" } };
		const char *const keys[] = { "recent-documents", "recent-applications", "recent-servers" };
		const char *const labels[] = { "Documents:", "Applications:", "Servers:" };
		for (int i = 0; i < 3; i++) {
			PanelPopup &p = m_counts[i];
			p.rect = QRect(M + IN + LABEL_W, G + 1 + PL_GROUP_MARGIN_TOP + i * ROW, 72, PL_POPUP_H);
			p.label = labels[i];
			const int value = settingInt(keys[i], 10);
			p.selected = 2;
			for (int k = 0; k < static_cast<int>(std::size(RECENT_COUNTS)); k++) {
				p.items.emplace_back(RECENT_COUNTS[k] ? QString::number(RECENT_COUNTS[k]) : QString("None"));
				if (RECENT_COUNTS[k] == value) {
					p.selected = k;
				}
			}
			const char *key = keys[i];
			p.chosen = [this, i, key](int row) {
				m_counts[i].selected = row;
				setInt(key, RECENT_COUNTS[row]);
				update();
			};
		}
		m_notes = { "Each shows as a submenu of the Zac menu;", "None leaves it out." };
	}

protected:
	void paintExtra(pl_canvas *c) override {
		for (const PanelPopup &p : m_counts) {
			p.paint(c);
		}
	}
	bool pressExtra(QPoint pos, bool) override {
		for (PanelPopup &p : m_counts) {
			if (p.press(this, pos)) {
				return true;
			}
		}
		return false;
	}

private:
	static constexpr int G = 26, ROW = PL_POPUP_H + 8, LABEL_W = 96;
	static constexpr int GROUP_H = 1 + PL_GROUP_MARGIN_TOP + 3 * ROW - 8 + PL_GROUP_MARGIN;
	static constexpr int PANEL_H = G + GROUP_H + 12 + 2 * 12 + 8;
	PanelPopup m_counts[3];
};

/* ---- Internet ------------------------------------------------------------------------ */

/* Mac OS 9's Internet control panel, the parts a Linux system has a place
 * for: the default web browser and e-mail program (mimeapps.list) and
 * where downloads go (user-dirs.dirs). Programs that ask the system for a
 * browser or mail program, or for the downloads folder, follow them. */
class InternetPanel : public ControlPanel {
public:
	InternetPanel() : ControlPanel("Internet", PANEL_H) {
		m_groups = { { G1, G1 + WEB_H, "Web" }, { G2, G2 + MAIL_H, "E-mail" } };
		setUp(m_browser, "Web Browser:", G1 + 1 + PL_GROUP_MARGIN_TOP, AppRole::WebBrowser);
		setUp(m_mail, "E-mail Program:", G2 + 1 + PL_GROUP_MARGIN_TOP, AppRole::Mail);
		const int y = G1 + 1 + PL_GROUP_MARGIN_TOP + ROW;
		m_select = PanelButton("Select…", QRect(W - M - IN - 2 - 76, y + 18, 76, PL_BUTTON_H));
		m_select.clicked = [this] {
			const QString folder = QFileDialog::getExistingDirectory(this, "Download Files To",
				downloadFolder());
			if (!folder.isEmpty() && setDownloadFolder(folder)) {
				update();
			}
		};
		m_notes = { "Programs that ask for the system's browser, mail program", "or downloads folder use these." };
	}

protected:
	void paintExtra(pl_canvas *c) override {
		m_browser.paint(c);
		m_mail.paint(c);
		const int y = G1 + 1 + PL_GROUP_MARGIN_TOP + ROW;
		Text label("Download Files To:", 200, PL_FONT_SYSTEM);
		pl_text(c, label.t, M + IN, y + 12, C_BLACK);
		QString path = downloadFolder();
		if (path.startsWith(QDir::homePath() + "/")) {
			path = "Home: " + path.mid(QDir::homePath().size() + 1).replace('/', ": ");
		}
		Text where(path, m_select.rect.left() - M - IN - 8, PL_FONT_VIEWS);
		pl_text(c, where.t, M + IN, y + 34, C_BLACK);
		m_select.paint(c);
	}

	bool pressExtra(QPoint pos, bool) override {
		return m_browser.press(this, pos) || m_mail.press(this, pos) || m_select.press(pos);
	}
	void moveExtra(QPoint pos) override {
		if (m_select.move(pos)) {
			update();
		}
	}
	void releaseExtra(QPoint pos) override {
		m_select.release(pos);
	}

private:
	void setUp(PanelPopup &popup, const char *label, int y, AppRole role) {
		popup.rect = QRect(M + IN + LABEL_W, y - 4, W - 2 * M - 2 * IN - LABEL_W - 2, PL_POPUP_H);
		popup.label = label;
		const QList<DefaultApp> apps = defaultAppChoices(role);
		const QString current = defaultApp(role);
		popup.items.clear();
		popup.selected = -1;
		for (int i = 0; i < apps.size(); i++) {
			popup.items.emplace_back(apps[i].name);
			if (apps[i].id == current) {
				popup.selected = i;
			}
		}
		if (apps.isEmpty()) {
			popup.items.emplace_back("None installed", false);
			popup.enabled = false;
			popup.selected = 0;
		} else if (popup.selected < 0) {
			popup.items.emplace_back("Choose one", false);
			popup.selected = static_cast<int>(popup.items.size()) - 1;
		}
		popup.chosen = [this, &popup, apps, role](int row) {
			if (row >= 0 && row < apps.size() && setDefaultApp(role, apps[row].id)) {
				popup.selected = row;
				update();
			}
		};
	}

	static constexpr int G1 = 26, ROW = PL_POPUP_H + 8, LABEL_W = 120;
	static constexpr int WEB_H = 1 + PL_GROUP_MARGIN_TOP + ROW + 18 + PL_BUTTON_H + PL_GROUP_MARGIN;
	static constexpr int G2 = G1 + WEB_H + 28;
	static constexpr int MAIL_H = 1 + PL_GROUP_MARGIN_TOP + PL_POPUP_H + PL_GROUP_MARGIN - 4;
	static constexpr int PANEL_H = G2 + MAIL_H + 12 + 2 * 12 + 8;
	PanelPopup m_browser, m_mail;
	PanelButton m_select;
};

/* ---- General Controls ---------------------------------------------------------------- */

/* Mac OS 9's General Controls (Missing Manual ch. 12): whether the desktop
 * shows while another program is in front (zacos9-wm hides the Sniffer's
 * windows, the Sniffer its desktop icons), how often a chosen menu item
 * blinks (the menu bar), the insertion point's blink rate (Qt programs
 * through the ZacOS 9 style, GTK ones through GSettings), and where Open
 * and Save start (the Sniffer's file-chooser portal). */
static const int CARET_MS[] = { 1600, 1066, 534 };

/* A group's height for `rows` rows of radio buttons or checkboxes. */
static constexpr int ROWS(int rows) {
	return 1 + PL_GROUP_MARGIN_TOP + 12 + PL_GROUP_MARGIN + (rows - 1) * 20;
}

class GeneralControlsPanel : public ControlPanel {
public:
	GeneralControlsPanel() : ControlPanel("General Controls", PANEL_H) {
		m_groups = { { G1, G1 + ROWS(2), "Desktop" }, { G2, G2 + ROWS(1), "Menu Blinking" },
			{ G3, G3 + ROWS(1), "Insertion Point Blinking" }, { G4, G4 + ROWS(4), "Documents" } };
		char v[16];
		m_desktop = PanelCheckbox("Show Desktop when in background", QPoint(X, G1 + TOP));
		m_desktop.on = !(pl_setting("show-desktop", v, sizeof(v)) && strcmp(v, "false") == 0);
		m_desktop.toggled = [](bool on) { pl_setting_set("show-desktop", on ? nullptr : "false"); };
		m_checks.push_back(&m_desktop);
		/* Mac OS 9's "Show Launcher at system startup": the Sniffer opens
		 * the Toy Box as it starts. */
		m_toyBox = PanelCheckbox("Show Toy Box at system startup", QPoint(X, G1 + TOP + 20));
		m_toyBox.on = pl_setting("toy-box-at-startup", v, sizeof(v)) && strcmp(v, "true") == 0;
		m_toyBox.toggled = [](bool on) { pl_setting_set("toy-box-at-startup", on ? "true" : nullptr); };
		m_checks.push_back(&m_toyBox);

		const char *const blinks[] = { "Off", "1", "2", "3" };
		for (int i = 0; i < 4; i++) {
			m_menu.add(blinks[i], QPoint(X + 64 * i, G2 + TOP));
		}
		m_menu.selected = pl_menu_blinks();
		m_menu.changed = [](int n) {
			if (n == 1) {
				pl_setting_set("menu-blink", nullptr);
			} else {
				setInt("menu-blink", n);
			}
		};
		m_radios.push_back(&m_menu);

		const char *const rates[] = { "Slow", "Medium", "Fast" };
		for (int i = 0; i < 3; i++) {
			m_caretRate.add(rates[i], QPoint(X + 88 * i, G3 + TOP));
		}
		const int ms = pl_caret_blink_ms();
		m_caretRate.selected = ms > 1300 ? 0 : ms < 800 ? 2 : 1;
		m_caretRate.changed = [this](int i) {
			if (i == 1) {
				pl_setting_set("caret-blink", nullptr);
			} else {
				setInt("caret-blink", CARET_MS[i]);
			}
			applyCaret(CARET_MS[i]);
		};
		m_radios.push_back(&m_caretRate);
		m_caret.callOnTimeout([this] { m_caretOn = !m_caretOn; update(); });
		m_caret.start(ms / 2);

		const char *const places[] = { "Folder that is set by the application",
			"Last folder used in the application", "Documents folder" };
		const char *const values[] = { "application", "last", "documents" };
		for (int i = 0; i < 3; i++) {
			m_documents.add(places[i], QPoint(X, G4 + TOP + 18 + 20 * i));
		}
		m_documents.selected = 0;
		if (pl_setting("documents-folder", v, sizeof(v))) {
			for (int i = 1; i < 3; i++) {
				m_documents.selected = strcmp(v, values[i]) == 0 ? i : m_documents.selected;
			}
		}
		m_documents.changed = [values](int i) {
			if (i == 2) {
				/* As on the Mac, choosing it makes the folder. */
				QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
			}
			pl_setting_set("documents-folder", i == 0 ? nullptr : values[i]);
		};
		m_radios.push_back(&m_documents);
		m_notes = { "With the desktop hidden, Command-Tab or the Application",
			"menu brings the Sniffer back." };
	}

protected:
	void paintExtra(pl_canvas *c) override {
		/* The insertion point, blinking at the chosen rate. */
		const QRect r(W - M - IN - 40, G3 + TOP - 5, 36, PL_EDIT_H);
		pl_edit_frame_paint(c, r.left(), r.top(), r.right(), r.bottom());
		if (m_caretOn) {
			pl_vline(c, r.left() + 6, r.top() + 4, r.bottom() - 4, C_BLACK);
		}
		Text label("When opening or saving, take me to:", W - 2 * M, PL_FONT_SYSTEM);
		pl_text(c, label.t, X, G4 + TOP + 9, C_BLACK);
	}

private:
	void applyCaret(int ms) {
		QApplication::setCursorFlashTime(ms);
		m_caret.start(ms / 2);
		m_caretOn = true;
		/* GTK programs follow GSettings as it changes; the schema may be
		 * missing, and then there is nothing to tell. */
		QProcess::startDetached("gsettings", { "set", "org.gnome.desktop.interface",
			"cursor-blink-time", QString::number(ms) });
	}

	static constexpr int X = M + IN, TOP = 1 + PL_GROUP_MARGIN_TOP;
	static constexpr int G1 = 26, G2 = G1 + ROWS(2) + 28, G3 = G2 + ROWS(1) + 28;
	static constexpr int G4 = G3 + ROWS(1) + 28;
	static constexpr int PANEL_H = G4 + ROWS(4) + 12 + 2 * 12 + 8;
	PanelCheckbox m_desktop, m_toyBox;
	PanelRadios m_menu, m_caretRate, m_documents;
	QTimer m_caret;
	bool m_caretOn = true;
};

/* ---- Trackpad ------------------------------------------------------------------------ */

/* Mac OS 9's Trackpad panel (the PowerBooks'): tracking and double-click
 * speed, shared with the Mouse panel, and what a tap on the pad does
 * (zacos9-wm sets libinput's tapping from it, compositor/src/input.c). */
class TrackpadPanel : public ControlPanel {
public:
	TrackpadPanel() : ControlPanel("Trackpad", PANEL_H) {
		m_groups = { { G1, G1 + SLIDER_GROUP_H, "Tracking Speed" },
			{ G2, G2 + ROWS(1), "Double-Click Speed" }, { G3, G3 + ROWS(3), "Use Trackpad for" } };
		placeSlider(m_tracking, G1, 7, settingInt("mouse-speed", 3), "Very Slow", "Fast");
		m_tracking.changed = [](int v) { setInt("mouse-speed", v); };

		const char *const speeds[] = { "Slow", "Medium", "Fast" };
		for (int i = 0; i < 3; i++) {
			m_speed.add(speeds[i], QPoint(X + 88 * i, G2 + TOP));
		}
		const int ms = settingInt("double-click", 533);
		m_speed.selected = ms > 650 ? 0 : ms < 450 ? 2 : 1;
		m_speed.changed = [](int i) {
			static const int times[] = { 800, 533, 333 };
			if (i == 1) {
				pl_setting_set("double-click", nullptr);
			} else {
				setInt("double-click", times[i]);
			}
		};
		m_radios.push_back(&m_speed);

		/* Unset, libinput's own defaults: no tapping, tap-dragging, no lock. */
		m_clicking = PanelCheckbox("Clicking", QPoint(X, G3 + TOP));
		m_clicking.on = settingOn("trackpad-tap", false);
		m_clicking.toggled = [this](bool on) {
			setOn("trackpad-tap", on);
			enableDragging();
		};
		m_dragging = PanelCheckbox("Dragging", QPoint(X + 20, G3 + TOP + 20));
		m_dragging.on = settingOn("trackpad-drag", true);
		m_dragging.toggled = [this](bool on) {
			setOn("trackpad-drag", on);
			enableDragging();
		};
		m_dragLock = PanelCheckbox("Drag Lock", QPoint(X + 40, G3 + TOP + 40));
		m_dragLock.on = settingOn("trackpad-drag-lock", false);
		m_dragLock.toggled = [](bool on) { setOn("trackpad-drag-lock", on); };
		m_checks = { &m_clicking, &m_dragging, &m_dragLock };
		enableDragging();
		m_notes = { "Clicking: tap the pad to click. Dragging: tap twice and keep",
			"your finger down to drag. Drag Lock: tap again to let go." };
	}

private:
	void enableDragging() {
		m_dragging.enabled = m_clicking.on;
		m_dragLock.enabled = m_clicking.on && m_dragging.on;
		update();
	}

	static constexpr int X = M + IN, TOP = 1 + PL_GROUP_MARGIN_TOP;
	static constexpr int G1 = 26, G2 = G1 + SLIDER_GROUP_H + 28, G3 = G2 + ROWS(1) + 28;
	static constexpr int PANEL_H = G3 + ROWS(3) + 12 + 2 * 12 + 8;
	PanelSlider m_tracking;
	PanelRadios m_speed;
	PanelCheckbox m_clicking, m_dragging, m_dragLock;
};

/* ---- Accessibility -------------------------------------------------------------------- */

/* Mac OS 9's Easy Access, with CloseView's zoom (Missing Manual pp.
 * 237-239): Sticky Keys, Slow Keys, Mouse Keys and Zoom, which zacos9-wm
 * does (compositor/src/access.c). The shortcuts turn them on and off from
 * the keyboard; the panel catches up when it comes forward. */
class AccessibilityPanel : public ControlPanel {
public:
	AccessibilityPanel() : ControlPanel("Accessibility", PANEL_H) {
		m_groups = { { G1, G1 + ROWS(1), "Sticky Keys" }, { G2, G2 + ROWS(2), "Slow Keys" },
			{ G3, G3 + ROWS(3), "Mouse Keys" }, { G4, G4 + ROWS(2), "Zoom" } };

		m_sticky = PanelCheckbox("On (or press Shift five times)", QPoint(X, G1 + TOP));
		m_sticky.toggled = [](bool on) { pl_setting_set("sticky-keys", on ? "true" : nullptr); };

		m_slow = PanelCheckbox("On", QPoint(X, G2 + TOP));
		m_slow.toggled = [](bool on) { pl_setting_set("slow-keys", on ? "true" : nullptr); };
		radioRow(m_slowDelay, "Delay:", G2 + TOP + 20, "slow-keys-delay", SLOW_DELAYS, 1);

		m_mouse = PanelCheckbox("On (or press ⌘-Shift-Clear)", QPoint(X, G3 + TOP));
		m_mouse.toggled = [](bool on) { pl_setting_set("mouse-keys", on ? "true" : nullptr); };
		radioRow(m_mouseDelay, "Delay:", G3 + TOP + 20, "mouse-keys-delay", MOUSE_DELAYS, 1);
		radioRow(m_mouseSpeed, "Speed:", G3 + TOP + 40, "mouse-keys-speed", MOUSE_SPEEDS, 1);

		m_zoom = PanelCheckbox("On (or press ⌘-Option-K)", QPoint(X, G4 + TOP));
		m_zoom.toggled = [](bool on) { pl_setting_set("zoom", on ? "true" : nullptr); };
		const char *const factors[] = { "2×", "4×", "8×", "16×" };
		for (int i = 0; i < 4; i++) {
			m_zoomFactor.add(factors[i], QPoint(X + LABEL_W + 60 * i, G4 + TOP + 20));
		}
		m_zoomFactor.changed = [](int i) { setInt("zoom-factor", 2 << i); };
		m_radios.push_back(&m_zoomFactor);

		m_checks = { &m_sticky, &m_slow, &m_mouse, &m_zoom };
		reload();
		m_notes = { "Mouse Keys: the keypad's 1-9 move the pointer, 5 clicks, 0 holds",
			"the button down and . lets it go. ⌘-Option-Up and Down zoom." };
	}

protected:
	void changeEvent(QEvent *e) override {
		if (e->type() == QEvent::ActivationChange && isActiveWindow()) {
			reload(); /* turned on or off from the keyboard meanwhile */
		}
		ControlPanel::changeEvent(e);
	}

	void paintExtra(pl_canvas *c) override {
		const std::pair<int, const char *> labels[] = { { G2 + TOP + 20, "Delay:" },
			{ G3 + TOP + 20, "Delay:" }, { G3 + TOP + 40, "Speed:" }, { G4 + TOP + 20, "Size:" } };
		for (const auto &[y, text] : labels) {
			Text t(text, LABEL_W, PL_FONT_SYSTEM);
			pl_text(c, t.t, X, y + 9, C_BLACK);
		}
	}

private:
	struct Choices {
		const char *names[3];
		int values[3];
	};
	static constexpr Choices SLOW_DELAYS = { { "Short", "Medium", "Long" }, { 300, 750, 1500 } };
	static constexpr Choices MOUSE_DELAYS = { { "Short", "Medium", "Long" }, { 200, 500, 1000 } };
	static constexpr Choices MOUSE_SPEEDS = { { "Slow", "Medium", "Fast" }, { 6, 12, 24 } };

	void radioRow(PanelRadios &r, const char *, int y, const char *key, const Choices &c, int def) {
		for (int i = 0; i < 3; i++) {
			r.add(c.names[i], QPoint(X + LABEL_W + 80 * i, y));
		}
		r.changed = [key, c, def](int i) {
			if (i == def) {
				pl_setting_set(key, nullptr);
			} else {
				setInt(key, c.values[i]);
			}
		};
		m_radios.push_back(&r);
		m_rows.push_back({ &r, key, c, def });
	}

	void reload() {
		m_sticky.on = settingOn("sticky-keys", false);
		m_slow.on = settingOn("slow-keys", false);
		m_mouse.on = settingOn("mouse-keys", false);
		m_zoom.on = settingOn("zoom", false);
		for (const Row &row : m_rows) {
			const int v = settingInt(row.key, row.choices.values[row.def]);
			int best = 0;
			for (int i = 1; i < 3; i++) {
				if (std::abs(row.choices.values[i] - v) < std::abs(row.choices.values[best] - v)) {
					best = i;
				}
			}
			row.radios->selected = best;
		}
		char v[16];
		const double factor = pl_setting("zoom-factor", v, sizeof(v)) ? atof(v) : 2.0;
		m_zoomFactor.selected = factor >= 12 ? 3 : factor >= 6 ? 2 : factor >= 3 ? 1 : 0;
		update();
	}

	struct Row {
		PanelRadios *radios;
		const char *key;
		Choices choices;
		int def;
	};
	static constexpr int X = M + IN, TOP = 1 + PL_GROUP_MARGIN_TOP, LABEL_W = 56;
	static constexpr int G1 = 26, G2 = G1 + ROWS(1) + 28, G3 = G2 + ROWS(2) + 28;
	static constexpr int G4 = G3 + ROWS(3) + 28;
	static constexpr int PANEL_H = G4 + ROWS(2) + 12 + 2 * 12 + 8;
	PanelCheckbox m_sticky, m_slow, m_mouse, m_zoom;
	PanelRadios m_slowDelay, m_mouseDelay, m_mouseSpeed, m_zoomFactor;
	std::vector<Row> m_rows;
};

/* ---- main --------------------------------------------------------------------------- */

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	const QString which = argc > 1 ? QString::fromUtf8(argv[1]) : QString("mouse");
	QApplication app(argc, argv);
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	/* Each panel is its own app to the menu bar (zacos9-mouse...). */
	QGuiApplication::setDesktopFileName("zacos9-" + which);

	std::unique_ptr<QWidget> panel;
	if (which == "keyboard") {
		QApplication::setApplicationName("Keyboard");
		panel = std::make_unique<KeyboardPanel>();
	} else if (which == "sound") {
		QApplication::setApplicationName("Sound");
		panel = std::make_unique<SoundPanel>();
	} else if (which == "screensaver") {
		QApplication::setApplicationName("Screen Saver");
		panel = std::make_unique<ScreenSaverPanel>();
	} else if (which == "energysaver") {
		QApplication::setApplicationName("Nap Time");
		panel = std::make_unique<EnergySaverPanel>();
	} else if (which == "internet") {
		QApplication::setApplicationName("Internet");
		panel = std::make_unique<InternetPanel>();
	} else if (which == "general") {
		QApplication::setApplicationName("General Controls");
		panel = std::make_unique<GeneralControlsPanel>();
	} else if (which == "zacmenu") {
		QApplication::setApplicationName("Zac Menu Options");
		panel = std::make_unique<ZacMenuOptionsPanel>();
	} else if (which == "startupdisk") {
		QApplication::setApplicationName("Startup Disk");
		platinumShellInit(); /* for its alerts */
		panel = std::make_unique<StartupDiskPanel>();
	} else if (which == "users") {
		QApplication::setApplicationName("Users");
		platinumShellInit(); /* for its alerts */
		panel = std::make_unique<UsersPanel>();
	} else if (which == "trackpad") {
		QApplication::setApplicationName("Trackpad");
		panel = std::make_unique<TrackpadPanel>();
	} else if (which == "accessibility") {
		QApplication::setApplicationName("Accessibility");
		panel = std::make_unique<AccessibilityPanel>();
	} else if (which == "monitors") {
		QApplication::setApplicationName("Monitors");
		panel = std::make_unique<MonitorsPanel>();
	} else {
		QApplication::setApplicationName("Mouse");
		panel = std::make_unique<MousePanel>();
	}
	panel->show();
	return app.exec();
}
