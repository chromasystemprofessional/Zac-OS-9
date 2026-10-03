#pragma once

#include <QWidget>
#include <vector>

#include "discovery.h"
#include "panelkit.h"

/*
 * platinum-netbrowser: the Network Browser, from the Apple menu. Lists
 * what Bonjour and our own WS-Discovery probe find; "Connect to
 * Server…" for anything that doesn't announce itself. Not pixel-
 * measured (see docs/network.md).
 */
class NetBrowserWindow : public QWidget {
public:
	NetBrowserWindow();

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void mouseDoubleClickEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	void scan();
	void connectToServer();
	void connectTo(const QString &address, DiscoveredServer::Kind kind);

	std::vector<DiscoveredServer> m_found;
	PanelList m_list;
	PanelButton m_connect, m_scan;
	PanelHost m_host{ this };
	QString m_status;
};
