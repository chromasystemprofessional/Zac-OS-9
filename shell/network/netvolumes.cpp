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
		v.share = v.name = QFileInfo(v.path).fileName();
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

QString afpMountPointFor(const QString &volumeName) {
	QString base = volumeName;
	base.replace('/', '-');
	if (base.isEmpty()) {
		base = "Untitled";
	}
	QDir().mkpath(mountsDir());
	const std::vector<NetVolume> live = netVolumes();
	auto taken = [&](const QString &path) {
		for (const NetVolume &v : live) {
			if (v.path == path) {
				return true;
			}
		}
		return false;
	};
	QString candidate = mountsDir() + "/" + base;
	for (int i = 2; taken(candidate) || QFileInfo::exists(candidate); i++) {
		candidate = mountsDir() + "/" + base + QStringLiteral(" (%1)").arg(i);
	}
	return candidate;
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
