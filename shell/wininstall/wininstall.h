#pragma once

#include <QProcess>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include "panelkit.h"

/*
 * Windows Installer: runs a Windows program's installer (.exe or .msi)
 * through Wine. Drag the installer onto the window (from the Finder or
 * a browser), or open one from the Finder. While Wine runs it, the
 * installer's own windows guide the way; afterwards this window names
 * the programs it added, which the Finder shows in Applications (Wine
 * files their Start-menu shortcuts as desktop entries).
 */
class WinInstallWindow : public QWidget {
public:
	explicit WinInstallWindow(const QString &file = QString());

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;
	void dragEnterEvent(QDragEnterEvent *) override;
	void dragLeaveEvent(QDragLeaveEvent *) override;
	void dropEvent(QDropEvent *) override;
	void closeEvent(QCloseEvent *) override;

private:
	enum class State { Empty, Ready, Preparing, Installing, Done };
	State m_state = State::Empty;
	QString m_file;
	QString m_message;      /* a problem, shown in red */
	QStringList m_added;    /* names of the programs the installer added */
	QSet<QString> m_before; /* Wine's desktop entries before installing */
	QSet<QString> m_beforeDirs; /* program folders in the prefix before installing */
	bool m_dropHover = false;

	PanelButton m_install, m_openOnly, m_another, m_done;
	PanelHost m_host{ this };
	QProcess *m_wine = nullptr;
	QTimer m_sweep;
	double m_sweepPos = 0;

	void setFile(const QString &path);
	void setState(State s);
	void install();
	void runInstaller();
	void finished(int exitCode);
	void openWithoutInstalling();
};
