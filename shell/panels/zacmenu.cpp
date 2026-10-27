/* The Zac Menu Options control panel (zacos9-controlpanel zacmenu). */
#include "controlpanel.h"

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
		m_submenus = PanelCheckbox("Show recent items as submenus", QPoint(M + IN, G + GROUP_H + 6));
		m_submenus.on = settingInt("recent-submenus", 1) != 0;
		m_submenus.toggled = [this](bool enabled) {
			if (!pl_setting_set("recent-submenus", enabled ? "1" : "0")) {
				m_submenus.on = !enabled;
				m_notes = { "Could not save the recent-menu preference." };
				update();
				return;
			}
			m_notes = { "Turn this off to list recent items directly",
				"in the Zac menu, with their category." };
			update();
		};
		m_checks.push_back(&m_submenus);
		m_notes = { "Turn this off to list recent items directly", "in the Zac menu, with their category." };
		/* Hint Bubbles. */
		m_submenus.hint = "Shows recent items in submenus of the Zac menu, rather than all in the menu itself.";
		for (PanelPopup &u : m_counts) {
			m_hintRects.push_back({ &u.rect, "Sets how many recent items the Zac menu remembers." });
		}
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
	PanelCheckbox m_submenus;
};

PANEL("zacmenu", "Zac Menu Options", false, ZacMenuOptionsPanel);
