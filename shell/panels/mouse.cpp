/* The Mouse control panel (zacos9-controlpanel mouse). */
#include "controlpanel.h"

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
		/* Hint Bubbles. */
		m_tracking.hint = "Sets how fast the pointer moves when you move the mouse.";
		m_speed.hint = "Sets how quickly you must click twice for a double-click.";
		m_tracks.hint = "Leaves a trail behind the pointer, so it is easier to follow.";
		m_ibeam.hint = "Makes the text pointer (the I-beam) thicker, so it is easier to see.";
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

PANEL("mouse", "Mouse", false, MousePanel);
