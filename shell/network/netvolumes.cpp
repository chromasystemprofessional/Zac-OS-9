#include "netvolumes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDebug>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QUrl>

#include <cerrno>
#include <sys/stat.h>

#include "smbclient.h"

static QString mountsDir() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
		"/zacos9/mounts";
}

static std::vector<NetVolume> afpMounts() {
	std::vector<NetVolume> out;
	QFile mounts("/proc/self/mountinfo");
	if (!mounts.open(QIODevice::ReadOnly)) {
		qWarning() << "Could not read network mount table:" << mounts.errorString();
		return out;
	}
	for (const QByteArray &line : mounts.readAll().split('\n')) {
		const QList<QByteArray> fields = line.split(' ');
		const int separator = fields.indexOf("-");
		if (separator < 6 || separator + 1 >= fields.size() ||
				fields.at(separator + 1) != "fuse.afp") {
			continue;
		}
		NetVolume v;
		v.kind = NetVolume::AFP;
		v.path = netVolumeMountInfoPath(fields.at(4));
		v.name = QFileInfo(v.path).fileName();
		/* The source, "afp://SERVER/VOLUME", names what was mounted. */
		const QUrl source(separator + 2 < fields.size() ?
			netVolumeMountInfoPath(fields.at(separator + 2)) : QString());
		v.server = source.host();
		v.share = source.path().mid(1);
		if (v.share.isEmpty()) {
			v.share = v.name;
		}
		out.push_back(v);
	}
	return out;
}

QString netVolumeMountInfoPath(QByteArray path) {
	path.replace("\\040", " ");
	path.replace("\\011", "\t");
	path.replace("\\012", "\n");
	path.replace("\\134", "\\");
	return QFile::decodeName(path);
}

static std::vector<NetVolume> smbMounts() {
	std::vector<NetVolume> out;
	const QString gvfs = qEnvironmentVariable("XDG_RUNTIME_DIR") + "/gvfs";
	/* "smb-share:server=HOST,share=SHARE" or "...,share=SHARE,user=NAME". */
	static const QRegularExpression name("^smb-share:server=([^,]+),share=([^,]+)");
	for (const QFileInfo &entry : QDir(gvfs).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		auto m = name.match(entry.fileName());
		if (!m.hasMatch()) {
			continue;
		}
		NetVolume v;
		v.kind = NetVolume::SMB;
		v.server = m.captured(1);
		v.share = m.captured(2);
		v.name = v.share;
		v.path = entry.absoluteFilePath();
		out.push_back(v);
	}
	return out;
}

std::vector<NetVolume> netVolumes() {
	std::vector<NetVolume> out = afpMounts();
	const std::vector<NetVolume> smb = smbMounts();
	out.insert(out.end(), smb.begin(), smb.end());
	return out;
}

static QString pathSafe(QString name, const QString &fallback) {
	name.replace('/', '-');
	name = name.trimmed();
	if (name.isEmpty() || name == "." || name == "..") {
		name = fallback;
	}
	return name;
}

QString afpVolumeDisplayName(const QString &volume, const QString &user) {
	for (const QString &suffix : { QStringLiteral("'s home"), QStringLiteral("\u2019s home") }) {
		if (!user.isEmpty() && volume.compare(user + suffix, Qt::CaseInsensitive) == 0) {
			return volume.left(user.size());
		}
	}
	return volume;
}

QString afpMountPointIn(const QString &dir, const QString &server, const QString &name) {
	return dir + "/" + pathSafe(server, "Server") + "/" + pathSafe(name, "Untitled");
}

/* Runs a short command to the end without blocking the GUI (never
 * QProcess::waitForFinished; see CLAUDE.md). */
static bool runBriefly(const QString &program, const QStringList &args) {
	QProcess p;
	p.start(program, args);
	QElapsedTimer timer;
	timer.start();
	while (p.state() != QProcess::NotRunning && timer.elapsed() < 5000) {
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 10);
		QThread::msleep(1);
	}
	if (p.state() != QProcess::NotRunning) {
		p.kill();
		return false;
	}
	return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

/* Empty folders in DIR (and one level down) that nothing is mounted on.
 * rmdir removes only an empty folder, and fails on a mount point. */
static void removeLeftovers(const QString &dir, const std::vector<NetVolume> &live) {
	for (const QFileInfo &entry : QDir(dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		const QString path = entry.absoluteFilePath();
		bool mounted = false;
		for (const NetVolume &v : live) {
			mounted |= v.path == path || v.path.startsWith(path + "/");
		}
		if (mounted) {
			continue;
		}
		for (const QFileInfo &inner : QDir(path).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
			QDir().rmdir(inner.absoluteFilePath());
		}
		QDir().rmdir(path);
	}
}

QString afpMountPointFor(const QString &server, const QString &volume, const QString &user,
		bool *mounted) {
	*mounted = false;
	const QString path = afpMountPointIn(mountsDir(), server, afpVolumeDisplayName(volume, user));
	for (const NetVolume &v : afpMounts()) {
		if (v.path != path) {
			continue;
		}
		struct stat st;
		if (stat(QFile::encodeName(path).constData(), &st) == 0) {
			*mounted = true; /* already there: nothing to do */
			return path;
		}
		/* Its server went away ("Transport endpoint is not connected"). */
		if (errno == ENOTCONN || errno == EIO) {
			runBriefly("fusermount3", { "-u", "-z", path });
		}
	}
	removeLeftovers(mountsDir(), afpMounts());
	return path;
}

bool netVolumeEject(const NetVolume &v, QString *error) {
	if (v.kind == NetVolume::AFP) {
		QProcess p;
		p.start("fusermount3", { "-u", v.path });
		QElapsedTimer timer;
		timer.start();
		while (p.state() != QProcess::NotRunning && timer.elapsed() < 10000) {
			QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 10);
			QThread::msleep(1);
		}
		const bool timedOut = p.state() != QProcess::NotRunning;
		if (timedOut) {
			p.kill();
		}
		if (timedOut || p.error() == QProcess::FailedToStart || p.exitStatus() != QProcess::NormalExit ||
				p.exitCode() != 0) {
			*error = QString::fromUtf8(p.readAllStandardError()).trimmed();
			if (error->isEmpty()) {
				*error = timedOut ? QStringLiteral("Disconnecting the server disk took too long.") :
					p.error() == QProcess::FailedToStart ? p.errorString() :
					QStringLiteral("The volume couldn’t be put away; something may still be using it.");
			}
			return false;
		}
		QDir().rmdir(v.path);
		return true;
	}
	if (!smbUnmount(v.server, v.share)) {
		*error = "The volume couldn’t be put away; something may still be using it.";
		return false;
	}
	return true;
}
