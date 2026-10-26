#include "inuse.h"

#include <QDir>
#include <QFile>
#include <QStringList>
#include <cstring>
#include <unistd.h>

/* Whether `path` is `mountPoint` or inside it ("(deleted)" suffix and all). */
static bool under(const QString &path, const QString &mountPoint) {
	return path == mountPoint || path.startsWith(mountPoint + '/');
}

static QString linkTarget(const QString &link) {
	char buf[4096];
	const ssize_t n = ::readlink(QFile::encodeName(link).constData(), buf, sizeof(buf) - 1);
	return n > 0 ? QFile::decodeName(QByteArray(buf, int(n))) : QString();
}

static bool usesVolume(const QString &dir, const QString &mountPoint) {
	for (const char *link : { "cwd", "root", "exe" }) {
		if (under(linkTarget(dir + '/' + link), mountPoint)) {
			return true;
		}
	}
	const QStringList fds = QDir(dir + "/fd").entryList(QDir::Files | QDir::System | QDir::NoDotAndDotDot);
	for (const QString &fd : fds) {
		if (under(linkTarget(dir + "/fd/" + fd), mountPoint)) {
			return true;
		}
	}
	QFile maps(dir + "/maps");
	if (maps.open(QIODevice::ReadOnly)) {
		while (!maps.atEnd()) {
			const QString line = QString::fromLocal8Bit(maps.readLine()).trimmed();
			/* address perms offset dev inode path */
			const QString path = line.section(' ', 5, -1, QString::SectionSkipEmpty);
			if (!path.isEmpty() && under(path, mountPoint)) {
				return true;
			}
		}
	}
	return false;
}

QList<Holder> programsUsing(const QString &mountPoint, const QString &proc) {
	QList<Holder> holders;
	if (mountPoint.isEmpty() || mountPoint == "/") {
		return holders;
	}
	QString mp = mountPoint;
	while (mp.size() > 1 && mp.endsWith('/')) {
		mp.chop(1);
	}
	const QStringList entries = QDir(proc).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
	for (const QString &entry : entries) {
		bool numeric = false;
		const int pid = entry.toInt(&numeric);
		if (!numeric || pid <= 0 || pid == ::getpid()) {
			continue;
		}
		const QString dir = proc + '/' + entry;
		if (!usesVolume(dir, mp)) {
			continue;
		}
		Holder h;
		h.pid = pid;
		QFile comm(dir + "/comm");
		if (comm.open(QIODevice::ReadOnly)) {
			h.name = QString::fromLocal8Bit(comm.readAll()).trimmed();
		}
		if (h.name.isEmpty()) {
			h.name = QString("process %1").arg(pid);
		}
		/* "0::/user.slice/user-1001.slice/user@1001.service/app.slice/lab.service" */
		QFile cgroup(dir + "/cgroup");
		if (cgroup.open(QIODevice::ReadOnly)) {
			const QString group = QString::fromLocal8Bit(cgroup.readLine()).trimmed().section("::", 1);
			if (group.contains("/user@") && group.endsWith(".service")) {
				h.unit = group.section('/', -1);
				h.name = h.unit.chopped(int(strlen(".service")));
			}
		}
		holders.append(h);
	}
	return holders;
}

QString holderNames(const QList<Holder> &holders) {
	QStringList names;
	for (const Holder &h : holders) {
		if (!names.contains(h.name)) {
			names << h.name;
		}
	}
	if (names.size() <= 1) {
		return names.join(QString());
	}
	const QString last = names.takeLast();
	return names.join(", ") + " and " + last;
}
