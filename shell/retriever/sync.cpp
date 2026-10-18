#include "sync.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <memory>

static QString confPath() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/zacos9/retriever.conf";
}

QList<FolderPair> loadPairs() {
	QList<FolderPair> pairs;
	QFile f(confPath());
	if (!f.open(QIODevice::ReadOnly)) {
		return pairs;
	}
	for (const QByteArray &line : f.readAll().split('\n')) {
		const QList<QByteArray> parts = line.split('\t');
		if (parts.size() == 2 && !parts[0].isEmpty() && !parts[1].isEmpty()) {
			pairs.append({ QString::fromUtf8(parts[0]), QString::fromUtf8(parts[1]) });
		}
	}
	return pairs;
}

bool savePairs(const QList<FolderPair> &pairs) {
	QDir().mkpath(QFileInfo(confPath()).path());
	QSaveFile f(confPath());
	if (!f.open(QIODevice::WriteOnly)) {
		return false;
	}
	for (const FolderPair &p : pairs) {
		f.write(p.left.toUtf8() + '\t' + p.right.toUtf8() + '\n');
	}
	return f.commit();
}

/* One way: what's newer in `from` replaces what's in `to` (--update),
 * keeping times so the other way sees them as alike. */
static void copyNewer(const QString &from, const QString &to, std::function<void(const QString &)> done) {
	const QString env = qEnvironmentVariable("ZACOS9_RSYNC");
	const QString rsync = env.isEmpty() ? QStandardPaths::findExecutable("rsync") : env;
	if (rsync.isEmpty()) {
		done("rsync isn't installed.");
		return;
	}
	auto *p = new QProcess;
	QObject::connect(p, &QProcess::finished, [p, done](int code, QProcess::ExitStatus status) {
		const QString err = QString::fromUtf8(p->readAllStandardError()).trimmed().section('\n', -1);
		p->deleteLater();
		done(status == QProcess::NormalExit && code == 0 ? QString()
			: err.isEmpty() ? QString("rsync stopped with status %1.").arg(code) : err);
	});
	QObject::connect(p, &QProcess::errorOccurred, [p, done](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			p->deleteLater();
			done("rsync couldn't be run.");
		}
	});
	/* Trailing slashes: the folders' contents, not the folders. */
	p->start(rsync, { "--recursive", "--times", "--update", "--links", "--perms",
		QDir(from).absolutePath() + '/', QDir(to).absolutePath() + '/' });
}

void syncPair(const FolderPair &pair, std::function<void(const QString &)> done) {
	for (const QString &dir : { pair.left, pair.right }) {
		if (!QFileInfo(dir).isDir()) {
			done(QString("“%1” can't be found.").arg(dir));
			return;
		}
	}
	copyNewer(pair.left, pair.right, [pair, done](const QString &error) {
		if (!error.isEmpty()) {
			done(error);
			return;
		}
		copyNewer(pair.right, pair.left, done);
	});
}
