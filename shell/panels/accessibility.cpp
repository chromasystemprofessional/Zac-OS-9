/* The Accessibility control panel (zacos9-controlpanel accessibility). */
#include "controlpanel.h"

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
		/* Hint Bubbles. */
		m_sticky.hint = "Sticky Keys: press Shift, Command, Option or Control and then a key, rather than both at once.";
		m_slow.hint = "Slow Keys: a key types only after you hold it down for a moment, so brief presses are ignored.";
		m_slowDelay.hint = "Sets how long you hold a key down before it types.";
		m_mouse.hint = "Mouse Keys: move the pointer with the numeric keypad; 5 clicks.";
		m_mouseDelay.hint = "Sets how long you hold a keypad key before the pointer starts to move.";
		m_mouseSpeed.hint = "Sets how fast the pointer moves with Mouse Keys.";
		m_zoom.hint = "Zoom: magnifies the screen around the pointer.";
		m_zoomFactor.hint = "Sets how much Zoom magnifies.";
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

PANEL("accessibility", "Accessibility", false, AccessibilityPanel);
