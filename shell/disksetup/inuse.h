#pragma once

/*
 * The programs using a volume: those with a file open, a working or root
 * directory, or a mapped file on it, as far as /proc tells (this user's
 * programs; others' can't be read). Disk Setup names them when a volume
 * can't be closed for First Aid, and offers to quit them. One run by a
 * user service (systemd --user) is named after the service, which is
 * stopped rather than killed, as it would start its programs again.
 */

#include <QList>
#include <QString>

struct Holder {
	int pid = 0;
	QString name; /* the program's name (comm): "php"; the service's: "chromasystem-lab" */
	QString unit; /* the user service it belongs to, if any: "chromasystem-lab.service" */
};

/* The programs using `mountPoint`, this one excepted. `proc` for tests. */
QList<Holder> programsUsing(const QString &mountPoint, const QString &proc = "/proc");

/* "php and bash", each name once: "php, bash and sh". */
QString holderNames(const QList<Holder> &holders);
