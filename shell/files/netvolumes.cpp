#include "netvolumes.h"
#include "process.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDebug>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

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
		if (separator + 2 >= fields.size() ||
				!afpMountSource(netVolumeMountInfoPath(fields.at(separator + 2)), &v.server, &v.share)) {
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

bool afpMountSource(const QString &source, QString *server, QString *volume) {
	static const QString scheme = QStringLiteral("afp://");
	const int slash = source.indexOf('/', scheme.size());
	if (!source.startsWith(scheme, Qt::CaseInsensitive) || slash <= scheme.size() ||
			slash + 1 >= source.size()) {
		return false;
	}
	*server = source.mid(scheme.size(), slash - scheme.size());
	*volume = source.mid(slash + 1);
	return true;
}

bool afpMountIs(const NetVolume &v, const QString &server, const QString &volume) {
	/* What zacos9-afp would have named this volume's mount (afpfs.c). */
	QByteArray source = QFile::encodeName("afp://" + server + "/" + volume);
	source.replace(',', '_');
	QString wantServer, wantVolume;
	return v.kind == NetVolume::AFP &&
		afpMountSource(QFile::decodeName(source.left(255)), &wantServer, &wantVolume) &&
		v.server.compare(wantServer, Qt::CaseInsensitive) == 0 && v.share == wantVolume;
}

QString afpMountPointIn(const QString &dir, const QString &server, const QString &volume,
		const QString &name) {
	const QString host = server.toLower(); /* host names are case-insensitive */
	const QByteArray id = QCryptographicHash::hash(host.toUtf8() + '\0' + volume.toUtf8(),
		QCryptographicHash::Sha256).toHex().left(16);
	return dir + "/" + pathSafe(host, "Server") + "/" + QString::fromLatin1(id) + "/" +
		pathSafe(name, "Untitled");
}

/* Where 0.1.48 to 0.1.50 mounted a volume: DIR/SERVER/NAME. */
static QString legacyMountPoint(const QString &dir, const QString &server, const QString &name) {
	return dir + "/" + pathSafe(server, "Server") + "/" + pathSafe(name, "Untitled");
}

int afpMountAmong(const std::vector<NetVolume> &live, const QString &path, const QString &legacy,
		const QString &server, const QString &volume, bool *taken) {
	*taken = false;
	int found = -1;
	for (size_t i = 0; i < live.size(); i++) {
		const NetVolume &v = live[i];
		if (v.kind != NetVolume::AFP || (v.path != path && v.path != legacy)) {
			continue;
		}
		if (!afpMountIs(v, server, volume)) {
			*taken |= v.path == path;
		} else if (found < 0 || v.path == path) {
			found = int(i);
		}
	}
	return found;
}

/* Runs a short command to the end without blocking the GUI (never
 * QProcess::waitForFinished; see CLAUDE.md). */
static bool runBriefly(const QString &program, const QStringList &args) {
	return runProcess(program, args, { {}, 5000 }).ok();
}

/* Empty folders under DIR, down to where volumes mount (SERVER/ID/NAME),
 * that nothing is mounted on or in. rmdir removes only an empty folder, and
 * fails on a mount point; nothing else mounted here is looked inside. */
static void removeLeftovers(const QString &dir, const std::vector<NetVolume> &live, int depth = 3) {
	struct stat top;
	if (depth == 0 || stat(QFile::encodeName(dir).constData(), &top) != 0) {
		return;
	}
	for (const QFileInfo &entry : QDir(dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot |
			QDir::NoSymLinks | QDir::Hidden)) {
		const QString path = entry.absoluteFilePath();
		bool mounted = false;
		for (const NetVolume &v : live) {
			mounted |= v.path == path || v.path.startsWith(path + "/");
		}
		struct stat st;
		if (mounted || lstat(QFile::encodeName(path).constData(), &st) != 0 ||
				!S_ISDIR(st.st_mode) || st.st_dev != top.st_dev) {
			continue;
		}
		removeLeftovers(path, live, depth - 1);
		QDir().rmdir(path);
	}
}

QString afpMountPointFor(const QString &server, const QString &volume, const QString &user,
		bool *mounted) {
	*mounted = false;
	const QString name = afpVolumeDisplayName(volume, user);
	const QString path = afpMountPointIn(mountsDir(), server, volume, name);
	const std::vector<NetVolume> live = afpMounts();
	bool taken = false;
	const int found = afpMountAmong(live, path, legacyMountPoint(mountsDir(), server, name),
		server, volume, &taken);
	if (taken) {
		qWarning() << "A different volume is mounted at" << path;
		return QString();
	}
	if (found >= 0) {
		const QString at = live[found].path;
		struct stat st;
		if (stat(QFile::encodeName(at).constData(), &st) == 0) {
			*mounted = true; /* already there: nothing to do */
			return at;
		}
		/* Its server went away ("Transport endpoint is not connected"). */
		if (errno == ENOTCONN || errno == EIO) {
			runBriefly("fusermount3", { "-u", "-z", at });
		}
	}
	removeLeftovers(mountsDir(), afpMounts());
	return path;
}

bool netVolumeEject(const NetVolume &v, QString *error) {
	if (v.kind == NetVolume::AFP) {
		QProcess p;
		p.start("fusermount3", { "-u", v.path });
		const bool timedOut = !waitForProcess(&p, 10000);
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
