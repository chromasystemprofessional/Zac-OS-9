#pragma once

#include <QProcess>
#include <QString>
#include <QWidget>
#include <memory>
#include <vector>

#include "panelkit.h"

/*
 * ZacOS 9 Installer: a Mac OS 9-style single-window installer that asks
 * the user to pick a destination disk and then installs.
 *
 * Screens (shown in order):
 *   Welcome  — logo, description, Continue button
 *   Select   — list of available disks, Install button
 *   Install  — progress bar and status while zacos9-install runs
 *   Done     — completion message, Restart button
 */

struct DiskEntry {
	QString device;   /* /dev/sda, /dev/nvme0n1, … */
	QString size;     /* human-readable, e.g. "500G" */
	QString model;    /* drive model, may be empty */
	QString label() const;
};

class InstallerWindow : public QWidget {
public:
	InstallerWindow();

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	enum class Screen { Welcome, Select, Installing, Done };
	Screen m_screen = Screen::Welcome;

	/* Welcome screen */
	PanelButton m_continue;

	/* Select screen */
	std::vector<DiskEntry> m_disks;
	PanelList m_diskList;
	PanelButton m_install;

	/* Installing screen */
	QProcess *m_installProcess = nullptr;
	double m_progress = 0.0;
	QString m_status;
	int m_step = 0;

	/* Done screen */
	PanelButton m_restart;

	void showWelcome();
	void showSelect();
	void showInstalling(const QString &device);
	void showDone();

	void enumerateDisks();
	void paintWelcome(pl_canvas *c);
	void paintSelect(pl_canvas *c);
	void paintInstalling(pl_canvas *c);
	void paintDone(pl_canvas *c);
	void paintLogo(pl_canvas *c, int cx, int cy, int size);
	void paintButtons(pl_canvas *c);
};
