#pragma once

/*
 * What the File Sharing control panel and the Finder's Get Info share:
 * running platinum-sharing-helper (as root, through pkexec) and reading the
 * settings it writes, which everyone may read.
 */

#include <QByteArray>
#include <QList>
#include <QProcess>
#include <QString>
#include <QStringList>

/* The helper, as root: through pkexec, or directly if we are root.
 * PLATINUM_SHARING_HELPER points at another one, for tests. */
void sharingHelperCommand(QProcess *p, const QStringList &args);

/* Runs the helper to the end, with `input` on its stdin, keeping the
 * windows drawn meanwhile. Its stdout; *ok says whether it worked and
 * *err gets its last line of complaint. */
QString runSharingHelper(const QStringList &args, const QByteArray &input, bool *ok,
	QString *err = nullptr);

/* A file the helper writes: /etc/platinum/NAME (or under
 * $PLATINUM_SHARING_CONF, for tests). */
QString sharingConfFile(const QString &name);

/* A folder shared from Get Info: its name on the network and its path. */
struct SharedFolder {
	QString name, path;
};
QList<SharedFolder> sharedFolders();

/* /etc/platinum/sharing.conf: "afp", "smb", "guests", ... -> "yes"/"no". */
QString sharingSetting(const QString &key);
/* Is file sharing on, for Macs or for Windows? */
bool sharingOn();
