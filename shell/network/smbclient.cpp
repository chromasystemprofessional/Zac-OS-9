#include "smbclient.h"

#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

/* server/share, percent-escaped the way a URL needs (a share name with
 * a space in it is common enough on real networks to get right). */
static QString smbUri(const QString &server, const QString &share) {
	return "smb://" + QUrl::toPercentEncoding(server) + "/" + QUrl::toPercentEncoding(share);
}

/* gio mount's own exit code is 0 whether or not the mount actually
 * worked (tested against a server refusing the connection); the
 * message on stderr is the only signal it gives, so that is what
 * both callers here go by. */
static bool runGio(const QStringList &args, const QByteArray &stdin_, QString *error) {
	QProcess p;
	p.start("gio", args);
	if (!stdin_.isEmpty()) {
		p.write(stdin_);
	}
	p.closeWriteChannel();
	QElapsedTimer timer;
	timer.start();
	while (p.state() != QProcess::NotRunning && timer.elapsed() < 20000) {
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 10);
		QThread::msleep(1);
	}
	if (p.state() != QProcess::NotRunning) {
		p.kill();
		*error = "That took too long.";
		return false;
	}
	if (p.error() == QProcess::FailedToStart || p.exitStatus() != QProcess::NormalExit) {
		*error = p.errorString();
		return false;
	}
	const QString err = QString::fromUtf8(p.readAllStandardError()).trimmed();
	if (!err.isEmpty()) {
		/* "gio: smb://host/share/: Failed to mount Windows share: ..." */
		const int colon = err.indexOf(": ", err.indexOf("://"));
		*error = colon > 0 ? err.mid(colon + 2) : err;
		return false;
	}
	if (p.exitCode() != 0) {
		*error = "The network operation failed (exit code " + QString::number(p.exitCode()) + ").";
		return false;
	}
	return true;
}

bool smbMount(const QString &server, const QString &share, const QString &user,
		const QString &password, QString *error) {
	const QString uri = smbUri(server, share);
	if (user.isEmpty()) {
		return runGio({ "mount", "-a", uri }, QByteArray(), error);
	}
	/* gio mount's own prompt order: user, domain, password — one line
	 * each; an empty domain line takes its default (the workgroup the
	 * server itself is in, which is what almost every network wants). */
	const QByteArray answers = user.toUtf8() + "\n\n" + password.toUtf8() + "\n";
	return runGio({ "mount", uri }, answers, error);
}

bool smbUnmount(const QString &server, const QString &share) {
	QString error;
	return runGio({ "mount", "-u", smbUri(server, share) }, QByteArray(), &error);
}
