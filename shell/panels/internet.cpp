/* The Internet control panel (zacos9-controlpanel internet). */
#include "controlpanel.h"

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
		m_webSharing = PanelCheckbox("Share my Web Pages folder (port 8080)",
			QPoint(M + IN, y + 48));
		m_webSharing.enabled = false;
		m_checks.push_back(&m_webSharing);
		m_webSharing.toggled = [this](bool on) { setWebSharing(on); };
		connect(&m_webProcess, &QProcess::finished, this,
			[this](int code, QProcess::ExitStatus status) {
				finishWebSharing(status == QProcess::NormalExit && code == 0,
					QString::fromLocal8Bit(m_webProcess.readAllStandardOutput()).trimmed(),
					QString::fromLocal8Bit(m_webProcess.readAllStandardError()).trimmed());
			});
		connect(&m_webProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
			if (error == QProcess::FailedToStart) {
				finishWebSharing(false, {}, m_webProcess.errorString());
			}
		});
		m_notes = { "Programs that ask for the system's browser, mail program", "or downloads folder use these." };
		m_webPending = true;
		m_webProcess.start("zacos9-web-sharing", { "status" });
		/* Hint Bubbles. */
		m_hintRects.push_back({ &m_browser.rect, "Chooses the program that opens web pages." });
		m_hintRects.push_back({ &m_mail.rect, "Chooses the program that writes e-mail." });
		m_hintRects.push_back({ &m_select.rect, "Chooses the folder where downloaded files go." });
		m_webSharing.hint = "Lets other computers read the pages in your Web Pages folder with a web browser.";
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
	void setWebSharing(bool on) {
		if (m_webPending) {
			return;
		}
		m_webWanted = on;
		m_webCommandStatus = false;
		m_webPending = true;
		m_webSharing.enabled = false;
		m_webProcess.start("zacos9-web-sharing", { on ? "start" : "stop" });
		update();
	}

	void finishWebSharing(bool ok, const QString &out, const QString &error) {
		if (!m_webPending) {
			return;
		}
		m_webPending = false;
		if (ok) {
			if (m_webCommandStatus) {
				m_webOn = out == "on";
			} else {
				m_webOn = m_webWanted;
			}
			m_webSharing.on = m_webOn;
		} else {
			m_webSharing.on = m_webOn;
			Alert::ask(error.isEmpty() ? "Web Sharing couldn't be changed." : error, "OK",
				QString());
		}
		m_webCommandStatus = false;
		m_webSharing.enabled = true;
		update();
	}

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
	static constexpr int WEB_H = 1 + PL_GROUP_MARGIN_TOP + ROW + 18 + PL_BUTTON_H + 48 +
		PL_CHECKBOX_SIZE + PL_GROUP_MARGIN;
	static constexpr int G2 = G1 + WEB_H + 28;
	static constexpr int MAIL_H = 1 + PL_GROUP_MARGIN_TOP + PL_POPUP_H + PL_GROUP_MARGIN - 4;
	static constexpr int PANEL_H = G2 + MAIL_H + 12 + 2 * 12 + 8;
	PanelPopup m_browser, m_mail;
	PanelButton m_select;
	PanelCheckbox m_webSharing;
	QProcess m_webProcess;
	bool m_webOn = false, m_webWanted = false, m_webPending = false;
	bool m_webCommandStatus = true;
};

PANEL("internet", "Internet", false, InternetPanel);
