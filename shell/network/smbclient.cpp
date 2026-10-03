#include "smbclient.h"

#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

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
	if (!p.waitForFinished(20000)) {
		p.kill();
		*error = "That took too long.";
		return false;
	}
	const QString err = QString::fromUtf8(p.readAllStandardError()).trimmed();
	if (!err.isEmpty()) {
		/* "gio: smb://host/share/: Failed to mount Windows share: ..." */
		const int colon = err.indexOf(": ", err.indexOf("://"));
		*error = colon > 0 ? err.mid(colon + 2) : err;
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
