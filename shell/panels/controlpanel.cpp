/*
 * zacos9-controlpanel: the Mouse, Keyboard, Sound and Monitors control
 * panels, one per run:
 *
 *   zacos9-controlpanel mouse | keyboard | sound | monitors
 *
 * Each writes ~/.config/zacos9/desktop.conf; zacos9-wm applies the
 * mouse, keyboard and screen settings as they change (compositor/src/
 * prefs.c). System volume goes through PulseAudio/PipeWire (pactl).
 * TODO: the layouts are ours, built from HIG controls and spacing.
 */
#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QProcess>
#include <QTimer>
#include <QWidget>

#include "panelkit.h"
#include "settings.h"
#include "soundclient.h"

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
	MousePanel() : ControlPanel("Mouse", 230) {
		const int g1 = 26, g2 = g1 + SLIDER_GROUP_H + 12 + 16;
		m_groups = { { g1, g1 + SLIDER_GROUP_H, "Mouse Tracking" },
			{ g2, g2 + 1 + PL_GROUP_MARGIN_TOP + 52 + PL_GROUP_MARGIN, "Double-Click Speed" } };
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
	PanelSlider m_tracking;
	PanelRadios m_speed;
	QRect m_test;
	QTimer m_flash;
	bool m_lit = false;
};

/* ---- Keyboard ------------------------------------------------------------------ */

class KeyboardPanel : public ControlPanel {
public:
	KeyboardPanel() : ControlPanel("Keyboard", 244) {
		const int g1 = 26, g2 = g1 + SLIDER_GROUP_H + 12 + 16;
		const int g3 = g2 + SLIDER_GROUP_H + 12 + 16;
		m_groups = { { g1, g1 + SLIDER_GROUP_H, "Key Repeat Rate" },
			{ g2, g2 + SLIDER_GROUP_H, "Delay Until Repeat" } };
		placeSlider(m_rate, g1, 7, settingInt("key-repeat", 5), "Off", "Fast");
		m_rate.changed = [](int v) { setInt("key-repeat", v); };
		placeSlider(m_delay, g2, 6, settingInt("key-delay", 3), "Off", "Short");
		m_delay.changed = [](int v) { setInt("key-delay", v); };
		m_field = QRect(M + 2, g3 - 6, W - 2 * M - 4, PL_EDIT_H);
		m_caret.callOnTimeout([this] { m_caretOn = !m_caretOn; update(); });
		m_caret.start(QApplication::cursorFlashTime() / 2);
	}

protected:
	void paintExtra(pl_canvas *c) override {
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
		if (m_caretOn) {
			pl_vline(c, caret, r.top() + 4, r.bottom() - 4, C_BLACK);
		}
	}

	void keyExtra(QKeyEvent *e) override {
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
