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
 *   Select   — list of available disks, Install button; a disk that
 *              already has ZacOS 9 offers Update (keeps its accounts,
 *              files and settings) beside Erase & Install
 *   Install  — progress bar and status while zacos9-install runs
 *   Done     — completion message, Restart button
 */

struct DiskEntry {
	QString device;   /* /dev/sda or /dev/sda2 */
	QString size;     /* human-readable, e.g. "500G" (not shown) */
	QString model;    /* the drive's make and model: its name in the list */
	bool isDisk = true; /* the list holds whole disks only */
	/* A ZacOS 9 already installed on it (zacos9-install --detect). */
	QString zacosPart, zacosVersion, zacosName;
	int zacosCmp = 0; /* its version against this installer's: -1, 0, 1 */
	QString label() const;
};

class InstallerWindow : public QWidget {
public:
	InstallerWindow();

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void wheelEvent(QWheelEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	enum class Screen { Welcome, Select, Installing, Done };
	Screen m_screen = Screen::Welcome;

	/* Welcome screen */
	PanelButton m_continue;

	/* Select screen */
	std::vector<DiskEntry> m_disks;
	PanelList m_diskList;
	PanelButton m_install;  /* Install, or Update / Reinstall for a ZacOS 9 disk */
	PanelButton m_erase;    /* Erase & Install, beside Update */
	QString m_liveVersion;  /* this installer's ZacOS 9 */
	PanelEdit m_diskName; /* what the installed disk is called */
	PanelHost m_host{ this }; /* the name field's caret, typing, Tab */

	/* Installing screen */
	QProcess *m_installProcess = nullptr;
	double m_progress = 0.0;
	QString m_status;
	QString m_lastOutput; /* last non-protocol line from the helper, shown on failure */
	int m_step = 0;
	bool m_updating = false; /* updating an installed system, not installing */

	/* Done screen */
	PanelButton m_restart;

	void showWelcome();
	void showSelect();
	void showInstalling(const QStringList &args, bool updating);
	void updateSelectButtons();
	const DiskEntry *selectedDisk() const;
	void showDone();

	void enumerateDisks();
	void paintWelcome(pl_canvas *c);
	void paintSelect(pl_canvas *c);
	void paintInstalling(pl_canvas *c);
	void paintDone(pl_canvas *c);
	void paintLogo(pl_canvas *c, int cx, int cy, int size);
	void paintButtons(pl_canvas *c);
};
