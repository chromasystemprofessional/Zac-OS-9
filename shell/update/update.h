#pragma once

#include <QNetworkAccessManager>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <vector>

#include "panelkit.h"
#include "updatecheck.h"

class QNetworkReply;

/*
 * Software Update, from the Apple menu: one window that checks for a new
 * ZacOS 9 (the newest GitHub release's zacos9 .deb) and for Debian's own
 * updates (apt), lists what it found, and installs it all with one
 * button - the .deb through zacos9-appstore-helper install-deb, then
 * Debian's through its upgrade command. See docs/updates.md.
 */
class UpdateWindow : public QWidget {
public:
	UpdateWindow();
	/* Begins checking, once the window is up. */
	void start() { QTimer::singleShot(0, this, [this] { check(); }); }

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;
	void closeEvent(QCloseEvent *) override;

private:
	enum class State { Checking, Available, UpToDate, Working, Done, Failed };
	State m_state = State::Checking;

	QString m_headline;
	QStringList m_lines;     /* wrapped detail text */
	QString m_doing;         /* while working: what apt or the download is doing */
	double m_progress = -1;  /* 0..1, or < 0 for a sweep */
	double m_sweepPos = 0;

	/* What the check found. */
	QString m_current;       /* installed zacos9 version */
	Release m_release;       /* valid only if newer than m_current */
	QString m_releaseError;  /* why GitHub couldn't be asked, if it couldn't */
	std::vector<DebianUpdate> m_debian;
	int m_pendingChecks = 0;
	bool m_zacosUpdated = false;
	QString m_debPath;       /* the downloaded .deb */

	PanelButton m_primary, m_secondary;
	PanelHost m_host{ this };
	QTimer m_sweep;
	QNetworkAccessManager m_net;
	QNetworkReply *m_reply = nullptr;
	QProcess *m_proc = nullptr;
	QString m_procErr;

	void check();
	void checkGitHub();
	void checkDebian();
	void checkDone();
	void showResults();

	void install();
	void downloadDeb();
	void installDeb();
	void upgradeDebian();
	void finished();
	void failed(const QString &why);

	void setText(const QString &headline, const QString &detail);
	void setButtons(const QString &primary, const QString &secondary);
	/* Runs a program; `done` gets its exit code, stdout lines go to `line`. */
	void run(const QString &program, const QStringList &args, std::function<void(int)> done,
		std::function<void(const QString &)> line = {});
	/* apt's APT::Status-Fd progress, mapped into [from, to] of the bar. */
	std::function<void(const QString &)> aptProgress(double from, double to);
	void primaryClicked();
	void secondaryClicked();
	bool busy() const { return m_state == State::Working; }
};
