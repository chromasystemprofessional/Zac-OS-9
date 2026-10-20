#include "sharingclient.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <unistd.h>

QString sharingConfFile(const QString &name) {
	const QString env = qEnvironmentVariable("ZACOS9_SHARING_CONF");
	return (env.isEmpty() ? QString("/etc/zacos9") : env) + '/' + name;
}

/* `kind` is "sharing", "accounts" or "memory": zacos9-KIND-helper, from
 * its source folder of that name in a build tree; ZACOS9_SHARING_HELPER,
 * ZACOS9_ACCOUNTS_HELPER or ZACOS9_MEMORY_HELPER stand in for tests. */
static QString helperPath(const QString &kind = "sharing") {
	const QString env = qEnvironmentVariable(("ZACOS9_" + kind.toUpper() + "_HELPER").toUtf8().constData());
	if (!env.isEmpty()) {
		return env;
	}
	const QString name = "zacos9-" + kind + "-helper";
	/* Installed beside us (prefix/libexec/zacos9), or, running from a
	 * build tree (build/shell/...), the one in the sources. */
	const QString dir = QCoreApplication::applicationDirPath();
	for (const QString &candidate : { dir + "/../libexec/zacos9/" + name,
			dir + "/../../" + kind + "/" + name }) {
		if (QFileInfo(candidate).isExecutable()) {
			return QFileInfo(candidate).canonicalFilePath();
		}
	}
	return "/usr/libexec/zacos9/" + name;
}

static void helperCommand(QProcess *p, const QString &kind, const QStringList &args) {
	if (geteuid() == 0) {
		p->start(helperPath(kind), args);
	} else {
		p->start("pkexec", QStringList{ helperPath(kind) } + args);
	}
}

void sharingHelperCommand(QProcess *p, const QStringList &args) {
	helperCommand(p, "sharing", args);
}

QString runSharingHelper(const QStringList &args, const QByteArray &input, bool *ok, QString *err) {
	return runPrivilegedHelper("sharing", args, input, ok, err);
}

QString runPrivilegedHelper(const QString &kind, const QStringList &args, const QByteArray &input,
		bool *ok, QString *err) {
	QProcess p;
	bool done = false;
	QObject::connect(&p, &QProcess::finished, [&done] { done = true; });
	QObject::connect(&p, &QProcess::errorOccurred, [&done](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			done = true;
		}
	});
	const QString what = kind == "sharing" ? QString("file sharing") : kind;
	helperCommand(&p, kind, args);
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
				? QString("The %1 helper stopped with status %2.").arg(what).arg(p.exitCode())
				: QString("The %1 helper couldn’t be run (%2).").arg(what, p.errorString());
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
