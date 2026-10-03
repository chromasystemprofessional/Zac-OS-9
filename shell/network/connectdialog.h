#pragma once

#include <QDialog>

#include "discovery.h"
#include "panelkit.h"

/*
 * "Connect to Server": a server address, typed. Not pixel-measured —
 * there was no Mac OS 9 screenshot of this left from an earlier
 * session to build it from (see docs/network.md); laid out from the
 * HIG controls the rest of the control panels already use.
 */
class ConnectDialog : public QDialog {
public:
	/* True if Connect was chosen; fills `address` and `kind`. An
	 * address typed as "afp://host/..." or "smb://host/..." sets
	 * `kind` from the scheme and strips it; otherwise `kind` comes from
	 * the radio buttons. */
	static bool ask(QWidget *parent, QString *address, DiscoveredServer::Kind *kind);

protected:
	void showEvent(QShowEvent *) override;
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	ConnectDialog();

	PanelEdit m_address;
	PanelRadios m_kind;
	PanelButton m_connect, m_cancel;
	PanelHost m_host{ this };
};
