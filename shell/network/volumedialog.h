#pragma once

#include <QDialog>

#include "afpclient.h"
#include "panelkit.h"

/* The volumes on a server, once logged in (AFP only — see
 * smbclient.h for why SMB has no equivalent step here). */
class VolumeDialog : public QDialog {
public:
	/* Empty if Cancel was chosen. */
	static QString ask(QWidget *parent, const QString &serverName,
		const std::vector<AfpVolume> &volumes);

protected:
	void showEvent(QShowEvent *) override;
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	VolumeDialog(const std::vector<AfpVolume> &volumes);

	std::vector<AfpVolume> m_volumes;
	PanelList m_list;
	PanelButton m_mount, m_cancel;
	PanelHost m_host{ this };
};
