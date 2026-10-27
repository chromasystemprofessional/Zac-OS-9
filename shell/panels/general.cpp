/* The General Controls control panel (zacos9-controlpanel general). */
#include "controlpanel.h"

/* Mac OS 9's General Controls (Missing Manual ch. 12): whether the desktop
 * shows while another program is in front (zacos9-wm hides the Sniffer's
 * windows, the Sniffer its desktop icons), how often a chosen menu item
 * blinks (the menu bar), the insertion point's blink rate (Qt programs
 * through the ZacOS 9 style, GTK ones through GSettings), and where Open
 * and Save start (the Sniffer's file-chooser portal). */
static const int CARET_MS[] = { 1600, 1066, 534 };

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
		/* Hint Bubbles. */
		m_desktop.hint = "Shows the desktop's icons while another program is in front. Off: they hide, and a click on the desktop does nothing.";
		m_toyBox.hint = "Opens the Toy Box when you log in.";
		m_menu.hint = "Sets how many times a menu item blinks when you choose it.";
		m_caretRate.hint = "Sets how fast the insertion point blinks in text.";
		m_documents.hint = "Chooses the folder Open and Save dialogs start in.";
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

PANEL("general", "General Controls", false, GeneralControlsPanel);
