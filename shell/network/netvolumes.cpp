#include "netvolumes.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include "smbclient.h"

static QString mountsDir() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
		"/zacos9/mounts";
}

static std::vector<NetVolume> afpMounts() {
	std::vector<NetVolume> out;
	QProcess p;
	p.start("findmnt", { "-t", "fuse.afp", "-n", "-o", "TARGET" });
	if (!p.waitForFinished(3000)) {
		return out;
	}
	for (const QString &path : QString::fromUtf8(p.readAllStandardOutput())
			.split('\n', Qt::SkipEmptyParts)) {
		NetVolume v;
		v.kind = NetVolume::AFP;
		v.path = path.trimmed();
		v.share = v.name = QFileInfo(v.path).fileName();
		out.push_back(v);
	}
	return out;
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
		if (!p.waitForFinished(10000) || p.exitStatus() != QProcess::NormalExit ||
				p.exitCode() != 0) {
			*error = QString::fromUtf8(p.readAllStandardError()).trimmed();
			if (error->isEmpty()) {
				*error = "The volume couldn’t be put away; something may still be using it.";
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
