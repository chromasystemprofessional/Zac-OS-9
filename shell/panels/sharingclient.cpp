#include "sharingclient.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <unistd.h>

QString sharingConfFile(const QString &name) {
	const QString env = qEnvironmentVariable("ZACOS9_SHARING_CONF");
	return (env.isEmpty() ? QString("/etc/zacos9") : env) + '/' + name;
}

static QString helperPath() {
	const QString env = qEnvironmentVariable("ZACOS9_SHARING_HELPER");
	if (!env.isEmpty()) {
		return env;
	}
	/* Installed beside us (prefix/libexec/zacos9), or, running from a
	 * build tree (build/shell/...), the one in the sources. */
	const QString dir = QCoreApplication::applicationDirPath();
	for (const QString &candidate : { dir + "/../libexec/zacos9/zacos9-sharing-helper",
			dir + "/../../sharing/zacos9-sharing-helper" }) {
		if (QFileInfo(candidate).isExecutable()) {
			return QFileInfo(candidate).canonicalFilePath();
		}
	}
	return "/usr/libexec/zacos9/zacos9-sharing-helper";
}

void sharingHelperCommand(QProcess *p, const QStringList &args) {
	if (geteuid() == 0) {
		p->start(helperPath(), args);
	} else {
		p->start("pkexec", QStringList{ helperPath() } + args);
	}
}

QString runSharingHelper(const QStringList &args, const QByteArray &input, bool *ok, QString *err) {
	QProcess p;
	bool done = false;
	QObject::connect(&p, &QProcess::finished, [&done] { done = true; });
	QObject::connect(&p, &QProcess::errorOccurred, [&done](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			done = true;
		}
	});
	sharingHelperCommand(&p, args);
	p.write(input);
	p.closeWriteChannel();
	/* Wait, keeping the windows drawn (servers take a moment to start, and
	 * pkexec may be asking for a password). */
	while (!done) {
		QApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents, 100);
	}
	const bool started = p.error() != QProcess::FailedToStart;
	*ok = started && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
	if (err) {
		*err = QString::fromUtf8(p.readAllStandardError()).trimmed().section('\n', -1);
		if (!*ok && err->isEmpty()) {
			*err = started && p.exitStatus() == QProcess::NormalExit
				? QString("The file sharing helper stopped with status %1.").arg(p.exitCode())
				: QString("The file sharing helper couldn’t be run (%1).").arg(p.errorString());
		}
	}
	return QString::fromUtf8(p.readAllStandardOutput());
}

QList<SharedFolder> sharedFolders() {
	QList<SharedFolder> list;
	QFile f(sharingConfFile("shared-folders"));
	if (!f.open(QIODevice::ReadOnly)) {
		return list;
	}
	for (const QByteArray &line : f.readAll().split('\n')) {
		const int tab = line.indexOf('\t');
		if (tab > 0) {
			list.append({ QString::fromUtf8(line.left(tab)), QString::fromUtf8(line.mid(tab + 1)) });
		}
	}
	return list;
}

QString sharingSetting(const QString &key) {
	QFile f(sharingConfFile("sharing.conf"));
	if (!f.open(QIODevice::ReadOnly)) {
		return QString();
	}
	const QByteArray prefix = key.toUtf8() + '=';
	for (const QByteArray &line : f.readAll().split('\n')) {
		if (line.startsWith(prefix)) {
			return QString::fromUtf8(line.mid(prefix.size()));
		}
	}
	return QString();
}

bool sharingOn() {
	return sharingSetting("afp") == "yes" || sharingSetting("smb") == "yes";
}
