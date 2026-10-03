#pragma once

#include <QDialog>

#include "panelkit.h"

/*
 * "Connect to the file server as:" — Mac OS 9's own login dialog, from
 * memory and the HIG controls (no screenshot survived this session to
 * measure it from; see docs/network.md). Guest / Registered User, a
 * name and password; for an SMB server, also which share to mount
 * (AFP's volume list is its own dialog, see volumedialog.h — SMB has
 * no equivalent one here, see smbclient.h for why).
 */
class LoginDialog : public QDialog {
public:
	struct Result {
		bool accepted = false;
		bool guest = true;
		QString user, password, share;
	};

	/* `error`, non-empty, shows as a line under the fields: a previous
	 * attempt's "wrong password" or similar, so the dialog can be shown
	 * again without a separate alert interrupting the flow twice. */
	static Result ask(QWidget *parent, const QString &serverName, bool guestAllowed,
		bool needsShare, const QString &error = QString());

protected:
	void showEvent(QShowEvent *) override;
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	LoginDialog(const QString &serverName, bool guestAllowed, bool needsShare,
		const QString &error);
	void updateEnabled();

	QString m_error;
	bool m_needsShare = false;
	PanelRadios m_who;
	PanelEdit m_name, m_password, m_share;
	PanelButton m_connect, m_cancel;
	PanelHost m_host{ this };
};
