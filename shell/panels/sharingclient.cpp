#include "sharingclient.h"
#include "process.h"
#include "programs.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <unistd.h>

QString sharingConfFile(const QString &name) {
	const QString env = qEnvironmentVariable("ZACOS9_SHARING_CONF");
	return (env.isEmpty() ? QString("/etc/zacos9") : env) + '/' + name;
}

/* `kind` is "sharing" or "accounts": zacos9-KIND-helper (rootHelperPath()),
 * from its source folder of that name in a build tree. */
static void helperCommand(QProcess *p, const QString &kind, const QStringList &args) {
	if (geteuid() == 0) {
		p->start(rootHelperPath(kind, kind), args);
	} else {
		p->start("pkexec", QStringList{ rootHelperPath(kind, kind) } + args);
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
	/* Servers take a moment to start, and pkexec may be asking for a password. */
	return runHelper(kind, kind, kind == "sharing" ? QString("file sharing") : kind, args, ok, err,
		{ input, true });
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
