#pragma once

#include <QDialog>

#include "afpclient.h"
#include "panelkit.h"

/* The volumes on a server, once logged in (AFP only — see
 * smbclient.h for why SMB has no equivalent step here). */
class VolumeDialog : public QDialog {
public:
	struct Result {
		QString volume; /* empty if Cancel was chosen */
		bool mountAtStartup = false;
	};

	/* `server` is the address (for the saved startup mount), while
	 * `serverName` is the name shown to the person. `user` is empty for
	 * Guest; `cleartext` is saved with the mount so a later login can
	 * repeat the warning-free connection. */
	static Result ask(QWidget *parent, const QString &serverName,
		const std::vector<AfpVolume> &volumes, const QString &server,
		const QString &user, bool guest, bool cleartext);

protected:
	void showEvent(QShowEvent *) override;
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void wheelEvent(QWheelEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	VolumeDialog(const std::vector<AfpVolume> &volumes, const QString &server,
		const QString &user, bool guest, bool cleartext);
	void updateStartup();

	std::vector<AfpVolume> m_volumes;
	QString m_server, m_user;
	bool m_guest = true, m_cleartext = false;
	PanelList m_list;
	PanelCheckbox m_startup;
	PanelButton m_mount, m_cancel;
	PanelHost m_host{ this };
};
