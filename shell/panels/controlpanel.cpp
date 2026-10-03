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
static bool pactl(const QStringList &args, QString *out = nullptr) {
	QProcess p;
	p.start("pactl", args);
	if (!p.waitForFinished(3000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
		return false;
	}
	if (out) {
		*out = QString::fromUtf8(p.readAllStandardOutput());
	}
	return true;
}

class SoundPanel : public ControlPanel {
public:
	SoundPanel() : ControlPanel("Sound", 236) {
		const int g1 = 26, g2 = g1 + SLIDER_GROUP_H + 12 + 16 + 20;
		m_groups = { { g1, g1 + SLIDER_GROUP_H + 20, "Volume" },
			{ g2, g2 + SLIDER_GROUP_H, "Alert Volume" } };

		/* Volume in eight steps, 0..7, as on the Mac. */
		QString out;
		m_available = pactl({ "get-sink-volume", "@DEFAULT_SINK@" }, &out);
		int percent = 100;
		const int at = out.indexOf('%');
		if (at > 0) {
			int start = at;
			while (start > 0 && out[start - 1].isDigit()) {
				start--;
			}
			percent = out.mid(start, at - start).toInt();
		}
		placeSlider(m_volume, g1, 8, (percent * 7 + 50) / 100, "Quiet", "Loud");
		m_volume.enabled = m_available;
		m_volume.changed = [](int v) {
			pactl({ "set-sink-volume", "@DEFAULT_SINK@", QString("%1%").arg(v * 100 / 7) });
		};
		m_mute = PanelCheckbox("Mute", QPoint(M + IN, g1 + SLIDER_GROUP_H + 2));
		QString muted;
		pactl({ "get-sink-mute", "@DEFAULT_SINK@" }, &muted);
		m_mute.on = muted.contains("yes");
		m_mute.enabled = m_available;
		m_mute.toggled = [](bool on) {
			pactl({ "set-sink-mute", "@DEFAULT_SINK@", on ? "1" : "0" });
		};
		m_checks.push_back(&m_mute);

		placeSlider(m_alert, g2, 8, settingInt("alert-volume", 7), "Off", "Loud");
		m_alert.changed = [](int v) {
			setInt("alert-volume", v);
		};
		if (!m_available) {
			m_notes = { "The volume can't be set: no sound server (pactl) here." };
		}
	}

protected:
	void mouseReleaseEvent(QMouseEvent *e) override {
		/* Let go of the alert volume and you hear it. */
		const bool alert = m_alert.dragging;
		ControlPanel::mouseReleaseEvent(e);
		if (alert) {
			pl_beep();
		}
	}

private:
	bool m_available = false;
	PanelSlider m_volume, m_alert;
	PanelCheckbox m_mute;
};

/* ---- Monitors ----------------------------------------------------------------------- */

class MonitorsPanel : public ControlPanel {
public:
	MonitorsPanel() : ControlPanel("Monitors", 236) {
		readOutputs();
		const int g1 = 26;
		const int listH = 7 * PL_LIST_ROW_H + 1;
		m_groups = { { g1, g1 + 1 + PL_GROUP_MARGIN_TOP + listH + PL_GROUP_MARGIN, "Resolution" } };
		m_list.frame = QRect(QPoint(M + IN, g1 + 1 + PL_GROUP_MARGIN_TOP),
			QPoint(M + IN + 170, g1 + 1 + PL_GROUP_MARGIN_TOP + listH));
		QStringList names;
		int current = 0;
		for (int i = 0; i < m_modes.size(); i++) {
			names << QString("%1 × %2").arg(m_modes[i].width()).arg(m_modes[i].height());
			if (m_modes[i] == m_size) {
				current = i;
			}
		}
		m_list.setItems(names);
		m_list.select(current, false);
		m_list.picked = [this](int i) {
			const QSize s = m_modes.value(i);
			pl_setting_set("resolution",
				QString("%1x%2").arg(s.width()).arg(s.height()).toUtf8().constData());
		};

		/* Pixel size: whole-number scales only, so the pixel art stays sharp. */
		const int rx = M + IN + 190, ry = g1 + 1 + PL_GROUP_MARGIN_TOP + 18;
		m_scale.add("Normal", QPoint(rx, ry));
		m_scale.add("Large (2×)", QPoint(rx, ry + 20));
		m_scale.add("Larger (3×)", QPoint(rx, ry + 40));
		m_scale.selected = std::clamp(m_currentScale, 1, 3) - 1;
		m_scale.changed = [](int i) { setInt("scale", i + 1); };
		m_radios.push_back(&m_scale);
		m_notes = { "Changes take effect at once." };
		if (m_nested) {
			m_notes.insert(m_notes.begin(),
				"In a window (nested), the resolution is the window's size.");
		}
	}

protected:
	void paintExtra(pl_canvas *c) override {
		m_list.paint(c, true);
		Text pixels("Pixel size:", 200, PL_FONT_SYSTEM);
		pl_text(c, pixels.t, M + IN + 190, m_groups[0].top + 1 + PL_GROUP_MARGIN_TOP + 9, C_BLACK);
	}

	bool pressExtra(QPoint p, bool) override {
		return m_list.press(p);
	}

	void keyExtra(QKeyEvent *e) override {
		if (m_list.key(e->key(), e->text())) {
			update();
		}
	}

private:
	/* zacos9-wm publishes the screens and their modes. */
	void readOutputs() {
		QFile f(qEnvironmentVariable("XDG_RUNTIME_DIR") + "/zacos9-outputs-" +
			qEnvironmentVariable("WAYLAND_DISPLAY"));
		if (f.open(QIODevice::ReadOnly)) {
			bool first = true;
			for (const QByteArray &line : f.readAll().split('\n')) {
				const QList<QByteArray> w = line.split(' ');
				if (w.value(0) == "output" && first) {
					const QList<QByteArray> wh = w.value(2).split('x');
					m_size = QSize(wh.value(0).toInt(), wh.value(1).toInt());
					m_currentScale = w.value(4).toInt();
					m_nested = w.value(6) == "1";
				} else if (w.value(0) == "output") {
					first = false; /* the first screen's modes only */
				} else if (w.value(0) == "mode" && first) {
					const QList<QByteArray> wh = w.value(1).split('@').value(0).split('x');
					const QSize s(wh.value(0).toInt(), wh.value(1).toInt());
					if (!m_modes.contains(s)) {
						m_modes << s;
					}
				}
			}
		}
		if (m_modes.isEmpty()) {
			/* A window has no modes of its own: offer the usual sizes,
			 * the Mac's own 832 x 624 and 1152 x 870 among them. */
			m_modes = { { 640, 480 }, { 800, 600 }, { 832, 624 }, { 1024, 768 },
				{ 1152, 870 }, { 1280, 720 }, { 1280, 800 }, { 1280, 1024 }, { 1440, 900 },
				{ 1600, 900 }, { 1920, 1080 } };
		}
		if (!m_size.isEmpty() && !m_modes.contains(m_size)) {
			m_modes.prepend(m_size);
		}
		std::sort(m_modes.begin(), m_modes.end(), [](const QSize &a, const QSize &b) {
			return a.width() != b.width() ? a.width() < b.width() : a.height() < b.height();
		});
	}

	QList<QSize> m_modes;
	QSize m_size;
	int m_currentScale = 1;
	bool m_nested = false;
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
