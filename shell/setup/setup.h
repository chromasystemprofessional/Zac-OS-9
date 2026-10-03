#pragma once

#include <QProcess>
#include <QStringList>
#include <QWidget>

#include "panelkit.h"

/*
 * ZacOS Setup Assistant: the first thing a freshly installed computer
 * shows, after Mac OS 9's Mac OS Setup Assistant — one window, a page
 * per question, arrows at the bottom right to go back and forth, and
 * "Go Ahead" on the last page. It runs in the first-run account
 * zacos9-install makes; zacos9-setup-helper (through pkexec) creates
 * the real account and ends that session.
 *
 * Pages: Introduction, Name, Password, Computer Name, Time Zone, Conclusion.
 */
class SetupWindow : public QWidget {
public:
	SetupWindow();

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void wheelEvent(QWheelEvent *) override;
	void keyPressEvent(QKeyEvent *) override;
	void closeEvent(QCloseEvent *) override;

private:
	enum Page { Intro, Name, Password, Computer, TimeZone, Conclusion, PageCount };
	enum class State { Asking, Working, Finished };

	Page m_page = Intro;
	State m_state = State::Asking;
	QString m_error; /* the helper's complaint, on the Conclusion page */

	PanelEdit m_fullName, m_shortName;
	bool m_shortNameTyped = false;
	PanelEdit m_password, m_verify;
	PanelCheckbox m_autoLogin;
	PanelEdit m_computerName;
	PanelList m_zones;
	QStringList m_zoneIds;

	PanelButton m_back, m_next, m_goAhead;
	PanelHost m_host{ this };
	QProcess *m_helper = nullptr;

	void showPage(Page p);
	/* Why the current page can't be left yet ("" if it can; an empty
	 * field gives no message, only a disabled arrow). */
	QString problem(bool *blocked) const;
	void updateButtons();
	void goAhead();
	QString hostName() const;
	QString zoneId() const;

	void paintPage(pl_canvas *c);
	void paintArrow(pl_canvas *c, const PanelButton &b, bool right) const;
};
