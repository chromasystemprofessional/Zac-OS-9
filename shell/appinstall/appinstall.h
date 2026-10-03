#pragma once

#include <QFile>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QWidget>
#include <functional>

#include "panelkit.h"

/*
 * App Installer: what dropping install files onto the Applications
 * folder does (the Finder starts it with their paths). A small window
 * after Mac OS 9's copy progress window - the file's icon, "Items
 * remaining to be installed", what it is doing, a progress bar and Stop -
 * working through the files one at a time:
 *
 *   .deb        confirmed first (it runs setup programs as root), then
 *               installed with apt through zacos9-appstore-helper
 *   .AppImage   copied into ~/.local/share/zacos9/apps, made runnable,
 *               given a desktop entry from its own name and icon
 *   .tar.*      unpacked there; given a desktop entry if one inside says
 *               how to start it, or there is one clear program to start
 *
 * Each finished one shows up in Applications through its desktop entry.
 */
class AppInstallWindow : public QWidget {
public:
	explicit AppInstallWindow(const QStringList &files);
	/* Files that aren't installers are set aside (reported at the end);
	 * false if none are left to install. */
	bool hasWork() const { return !m_queue.isEmpty(); }
	/* Says what couldn't be installed, if anything. */
	void reportFailures();
	/* Begins on the first file, once the window is up. */
	void start() { QTimer::singleShot(0, this, [this] { next(); }); }

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;
	void closeEvent(QCloseEvent *) override;

private:
	QStringList m_queue;
	QString m_file;          /* the one being installed */
	QString m_name;          /* how it is named in the window */
	QString m_doing;         /* "Installing: Name", "Copying: ...", apt's own words */
	double m_progress = -1;  /* 0..1, or < 0 for "working" (a sweep) */
	double m_sweepPos = 0;
	bool m_stoppable = true;
	QStringList m_failures;  /* "Name: why", reported at the end */

	PanelButton m_stop;
	PanelHost m_host{ this };
	QTimer m_sweep;
	QProcess *m_proc = nullptr;
	QString m_procErr;       /* the last line it printed to stderr */
	/* An AppImage copy in steps, so the window stays live. */
	QFile *m_src = nullptr, *m_dst = nullptr;
	QTimer m_copyStep;
	QString m_target;        /* where the program is going */

	void next();
	void fail(const QString &why);
	void succeed();
	void stop();
	/* Runs a program; `done` gets its exit code. stdout lines go to `line`. */
	void run(const QString &program, const QStringList &args,
		std::function<void(int)> done, std::function<void(const QString &)> line = {});

	void installDeb();
	void installAppImage();
	void appImageCopied();
	void installArchive();
	void archiveUnpacked();
};
