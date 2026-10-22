#pragma once

#include <QProcess>
#include <QStringList>
#include <QTimer>
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
 * Pages: Introduction, Name, Password, Computer Name, Time Zone, Internet,
 * Conclusion. Internet opens the TCP/IP control panel (zacos9-tcpip) and
 * says that's where the connection is set from now on.
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
	enum Page { Intro, Name, Password, Computer, TimeZone, Internet, Conclusion, PageCount };
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

	PanelButton m_openTcpip;
	QProcess *m_tcpip = nullptr;
	QProcess *m_nmcli = nullptr;
	QTimer m_netTimer;
	QString m_netStatus; /* "Connected through Wi-Fi (“Home”)." and the like */

	PanelButton m_back, m_next, m_goAhead;
	PanelHost m_host{ this };
	QProcess *m_helper = nullptr;

	void showPage(Page p);
	/* Why the current page can't be left yet ("" if it can; an empty
	 * field gives no message, only a disabled arrow). */
	QString problem(bool *blocked) const;
	void updateButtons();
	void goAhead();
	void openTcpip();
	void checkNetwork();
	QString hostName() const;
	QString zoneId() const;

	void paintPage(pl_canvas *c);
	void paintArrow(pl_canvas *c, const PanelButton &b, bool right) const;
};
