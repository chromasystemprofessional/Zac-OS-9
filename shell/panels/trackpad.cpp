/* The Trackpad control panel (zacos9-controlpanel trackpad). */
#include "controlpanel.h"

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
		/* Hint Bubbles. */
		m_tracking.hint = "Sets how fast the pointer moves when you move your finger on the trackpad.";
		m_speed.hint = "Sets how quickly you must tap twice for a double-click.";
		m_clicking.hint = "Lets you tap the trackpad to click.";
		m_dragging.hint = "Lets you tap twice and slide your finger to drag.";
		m_dragLock.hint = "Keeps a drag going when you lift your finger, until you tap again.";
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

PANEL("trackpad", "Trackpad", false, TrackpadPanel);
