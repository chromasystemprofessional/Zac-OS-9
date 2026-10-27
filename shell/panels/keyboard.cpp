/* The Keyboard control panel (zacos9-controlpanel keyboard). */
#include "controlpanel.h"

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
		/* Hint Bubbles. */
		m_rate.hint = "Sets how fast a key repeats while you hold it down. Off: keys never repeat.";
		m_delay.hint = "Sets how long you hold a key down before it starts to repeat.";
		m_option.hint = "Switches keyboard layouts with Command-Option-Space as well as Command-Space.";
		m_hintRects.push_back({ &m_list.frame, "The keyboard layouts you can type with. Check the ones you want in the keyboard menu." });
		m_hintRects.push_back({ &m_fkeys.rect, "Opens Function Keys, where F-keys can open programs, documents and folders." });
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

PANEL("keyboard", "Keyboard", false, KeyboardPanel);
