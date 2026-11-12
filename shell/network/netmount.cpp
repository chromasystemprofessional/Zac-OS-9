/*
 * zacos9-netmount: mounts the network disks saved to mount at startup.
 *
 * The Network Browser writes the list and the login autostart entry; this
 * helper runs once at login, looks up a registered user's password in the
 * keychain, and mounts what is not already mounted. Failures go to
 * $XDG_STATE_HOME/zacos9/network-mounts.log: there is no window to show
 * them in at login, and one unreachable server must not hold up the others.
 */
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

#include <cstring>

#include "afpclient.h"
#include "netvolumes.h"
#include "smbclient.h"
#include "startupmounts.h"

static QString logFile() {
	QString base = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation);
	if (base.isEmpty()) {
		base = QDir::homePath() + "/.local/state";
	}
	return base + "/zacos9/network-mounts.log";
}

static void log(const QString &message) {
	const QString path = logFile();
	if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
		return;
	}
	QFile file(path);
	if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
		file.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8() + " " +
			message.toUtf8() + "\n");
	}
}

static bool isMounted(const StartupMount &mount) {
	for (const NetVolume &volume : netVolumes()) {
		if (mount.kind == StartupMount::AFP && volume.kind == NetVolume::AFP &&
				afpMountIs(volume, mount.server, mount.volume)) {
			return true;
		}
		if (mount.kind == StartupMount::SMB && volume.kind == NetVolume::SMB &&
				volume.server.compare(mount.server, Qt::CaseInsensitive) == 0 &&
				volume.share == mount.share) {
			return true;
		}
	}
	return false;
}

static void mountOne(const StartupMount &mount) {
	const QString name = mount.kind == StartupMount::AFP ? mount.volume : mount.share;
	if (isMounted(mount)) {
		return;
	}
	QString password;
	if (!mount.guest) {
		password = startupMountPassword(mount);
		if (password.isEmpty()) {
			log(QString("“%1” on “%2”: no password in the keychain").arg(name, mount.server));
			return;
		}
	}
	QString error;
	if (mount.kind == StartupMount::AFP) {
		bool already = false;
		const QString mountPoint = afpMountPointFor(mount.server, mount.volume, mount.user, &already);
		if (mountPoint.isEmpty()) {
			log(QString("“%1” on “%2”: another disk is mounted in its place").arg(name, mount.server));
			return;
		}
		if (!already && !afpMount(mount.server, mount.volume, mountPoint, mount.user, password,
				mount.cleartext, &error)) {
			log(QString("“%1” on “%2”: %3").arg(name, mount.server, error));
		}
		return;
	}
	if (!smbMount(mount.server, mount.share, mount.user, password, &error)) {
		log(QString("“%1” on “%2”: %3").arg(name, mount.server, error));
	}
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	bool all = false;
	QString wanted;
	for (int i = 1; i < argc; i++) {
		if (std::strcmp(argv[i], "--mount-all") == 0) {
			all = true;
		} else if (std::strcmp(argv[i], "--mount-id") == 0 && i + 1 < argc) {
			wanted = QString::fromUtf8(argv[++i]);
		}
	}
	if (wanted.isEmpty() && !all) {
		QTextStream(stderr) << "usage: zacos9-netmount --mount-id ID | --mount-all\n";
		return 2;
	}
	if (!wanted.isEmpty()) {
		for (const StartupMount &mount : startupMounts()) {
			if (startupMountId(mount) == wanted) {
				mountOne(mount);
				return 0;
			}
		}
		log(QString("no saved network disk has the id %1").arg(wanted));
		return 1;
	}
	for (const StartupMount &mount : startupMounts()) {
		mountOne(mount);
	}
	return 0;
}
