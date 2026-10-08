#include "alias.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QDirIterator>
#include <sys/stat.h>
#include <unistd.h>

namespace {

struct Identity {
	quint64 dev = 0, ino = 0;
	QString name, parent;
	bool valid = false;
};

constexpr int kMaxHops = 32;
constexpr int kMaxVisited = 4000;
constexpr int kMaxDepth = 3;

QString recordPath(const QString &aliasPath) {
	const QFileInfo info(aliasPath);
	return info.absolutePath() + "/.alias/" + info.fileName();
}

bool statOf(const QString &path, struct stat *st, bool follow) {
	return (follow ? stat(QFile::encodeName(path).constData(), st)
		: lstat(QFile::encodeName(path).constData(), st)) == 0;
}

Identity identityOf(const QString &path) {
	Identity id;
	struct stat st;
	const QString intended = QFileInfo(path).canonicalFilePath();
	if (!intended.isEmpty() && statOf(intended, &st, true)) {
		id.dev = st.st_dev;
		id.ino = st.st_ino;
		id.name = QFileInfo(intended).fileName();
		id.parent = QFileInfo(intended).absolutePath();
		id.valid = true;
	}
	return id;
}

Identity readRecord(const QString &aliasPath) {
	Identity id;
	QFile f(recordPath(aliasPath));
	if (!f.open(QIODevice::ReadOnly) || f.size() > 8192) {
		return id;
	}
	for (const QByteArray &line : f.readAll().split('\n')) {
		const int eq = line.indexOf('=');
		const QByteArray key = line.left(eq), value = line.mid(eq + 1);
		if (key == "dev") {
			id.dev = value.toULongLong();
		} else if (key == "ino") {
			id.ino = value.toULongLong();
		} else if (key == "name") {
			id.name = QString::fromUtf8(value);
		} else if (key == "parent") {
			id.parent = QString::fromUtf8(value);
		}
	}
	id.valid = id.ino != 0;
	return id;
}

bool writeRecord(const QString &aliasPath, const Identity &id) {
	if (!QDir().mkpath(QFileInfo(recordPath(aliasPath)).absolutePath())) {
		return false;
	}
	QSaveFile f(recordPath(aliasPath));
	if (!f.open(QIODevice::WriteOnly)) {
		return false;
	}
	const QByteArray data = "dev=" + QByteArray::number(id.dev) + "\nino=" +
		QByteArray::number(id.ino) + "\nname=" + id.name.toUtf8() + "\nparent=" +
		id.parent.toUtf8() + "\n";
	return f.write(data) == data.size() && f.commit();
}

bool sameIdentity(const QString &path, const Identity &id) {
	struct stat st;
	return statOf(path, &st, true) && quint64(st.st_dev) == id.dev && quint64(st.st_ino) == id.ino;
}

/* Follows the link chain by hand: Loop on a repeat or too many hops,
 * Missing at a dangling end, else Ok with the final item. */
AliasState chase(const QString &aliasPath, QString *final) {
	QSet<QString> seen;
	QString current = aliasPath;
	for (int hop = 0; hop < kMaxHops; hop++) {
		const QFileInfo info(current);
		if (!info.isSymLink()) {
			*final = current;
			return info.exists() ? AliasState::Ok : AliasState::Missing;
		}
		if (seen.contains(current)) {
			return AliasState::Loop;
		}
		seen << current;
		QString next = info.symLinkTarget();
		if (next.isEmpty()) {
			return AliasState::Missing;
		}
		current = next;
	}
	return AliasState::Loop;
}

/* Bounded breadth-first look for the recorded inode: a few nearby folders,
 * same disk only, no symlinks followed, at most kMaxVisited entries. */
QString search(const Identity &id, const QString &aliasPath) {
	QStringList roots = { id.parent, QFileInfo(aliasPath).absolutePath(),
		QFileInfo(id.parent).absolutePath(),
		QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/Desktop",
		QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/Trash/files" };
	QSet<QString> queued;
	int visited = 0;
	struct Level { QString dir; int depth; };
	QList<Level> queue;
	for (const QString &r : roots) {
		struct stat st;
		if (!r.isEmpty() && !queued.contains(r) && statOf(r, &st, false) &&
				S_ISDIR(st.st_mode) && quint64(st.st_dev) == id.dev) {
			queued << r;
			queue.append({ r, 0 });
		}
	}
	for (int i = 0; i < queue.size() && visited < kMaxVisited; i++) {
		const Level level = queue.at(i);
		QDirIterator entries(level.dir, QDir::AllEntries | QDir::NoDotAndDotDot |
			QDir::System | QDir::Hidden);
		while (entries.hasNext() && visited < kMaxVisited) {
			const QString path = entries.next();
			++visited;
			struct stat st;
			if (!statOf(path, &st, false)) {
				continue;
			}
			if (quint64(st.st_dev) == id.dev && quint64(st.st_ino) == id.ino) {
				return path;
			}
			if (S_ISDIR(st.st_mode) && quint64(st.st_dev) == id.dev && level.depth < kMaxDepth &&
					QFileInfo(path).fileName() != ".alias" && !queued.contains(path)) {
				queued << path;
				queue.append({ path, level.depth + 1 });
			}
		}
	}
	return {};
}

bool repoint(const QString &aliasPath, const QString &target) {
	const QFileInfo info(aliasPath);
	const QString temp = info.absolutePath() + "/." + info.fileName() + ".new" +
		QString::number(getpid());
	QFile::remove(temp);
	if (symlink(QFile::encodeName(target).constData(), QFile::encodeName(temp).constData()) != 0) {
		return false;
	}
	if (rename(QFile::encodeName(temp).constData(), QFile::encodeName(aliasPath).constData()) != 0) {
		QFile::remove(temp);
		return false;
	}
	return true;
}

} // namespace

bool aliasRecord(const QString &aliasPath, const QString &target) {
	const Identity id = identityOf(target);
	return id.valid && writeRecord(aliasPath, id);
}

AliasResolution aliasResolve(const QString &aliasPath, bool reconnect) {
	AliasResolution out;
	if (!QFileInfo(aliasPath).isSymLink()) {
		return out;
	}
	QString final;
	const AliasState chained = chase(aliasPath, &final);
	if (chained == AliasState::Loop) {
		out.state = AliasState::Loop;
		return out;
	}
	const Identity id = readRecord(aliasPath);
	if (chained == AliasState::Ok) {
		if (!id.valid) {
			/* A plain symbolic link: adopt it so it can reconnect later. */
			if (!aliasRecord(aliasPath, final)) {
				out.error = "The alias identity record could not be saved.";
			}
		} else if (!sameIdentity(final, id)) {
			const QString moved = reconnect ? search(id, aliasPath) : QString();
			if (!moved.isEmpty()) {
				if (!aliasRecord(aliasPath, moved)) {
					out.state = AliasState::Missing;
					out.error = "The original was found, but its alias identity record could not be updated.";
					return out;
				}
				if (!repoint(aliasPath, moved)) {
					out.state = AliasState::Missing;
					out.error = "The original was found, but the alias link could not be repaired.";
					return out;
				}
				out.state = AliasState::Reconnected;
				out.target = moved;
				return out;
			}
			out.state = AliasState::Changed;
			out.target = final;
			return out;
		}
		out.state = AliasState::Ok;
		out.target = final;
		return out;
	}
	out.state = AliasState::Missing;
	if (id.valid && reconnect) {
		const QString found = search(id, aliasPath);
		if (!found.isEmpty()) {
			if (!aliasRecord(aliasPath, found)) {
				out.error = "The original was found, but its alias identity record could not be updated.";
			} else if (!repoint(aliasPath, found)) {
				out.error = "The original was found, but the alias link could not be repaired.";
			} else {
				out.state = AliasState::Reconnected;
				out.target = found;
			}
		}
	}
	return out;
}

bool aliasReconnect(const QString &aliasPath, const QString &newTarget, QString *error) {
	auto fail = [&](const QString &why) {
		if (error) {
			*error = why;
		}
		return false;
	};
	const QFileInfo target(newTarget);
	if (!QFileInfo(aliasPath).isSymLink()) {
		return fail("This is not an alias.");
	}
	if (!target.exists()) {
		return fail("The chosen item does not exist.");
	}
	const QString absolute = target.absoluteFilePath();
	QString final;
	if (absolute == QFileInfo(aliasPath).absoluteFilePath() ||
			(target.isSymLink() && chase(absolute, &final) != AliasState::Ok) ||
			(target.isSymLink() && final == QFileInfo(aliasPath).absoluteFilePath())) {
		return fail("An alias cannot point to itself.");
	}
	const QString oldTarget = QFileInfo(aliasPath).symLinkTarget();
	if (!repoint(aliasPath, absolute)) {
		return fail("The alias could not be updated. Check folder permissions.");
	}
	if (!aliasRecord(aliasPath, absolute)) {
		repoint(aliasPath, oldTarget);
		return fail("The alias record could not be saved. The previous target was restored.");
	}
	return true;
}

bool aliasMoveRecord(const QString &from, const QString &to, bool copy) {
	const QString src = recordPath(from);
	if (!QFileInfo(src).isFile()) {
		return true;
	}
	const QString dest = recordPath(to);
	if (src == dest) {
		return true;
	}
	if (!QDir().mkpath(QFileInfo(dest).absolutePath())) {
		return false;
	}
	QFile input(src);
	if (!input.open(QIODevice::ReadOnly) || input.size() > 8192) {
		return false;
	}
	const QByteArray contents = input.readAll();
	if (input.error() != QFileDevice::NoError) {
		return false;
	}
	QSaveFile output(dest);
	if (!output.open(QIODevice::WriteOnly) || output.write(contents) != contents.size() ||
			!output.commit()) {
		return false;
	}
	return copy || QFile::remove(src);
}

bool aliasRemoveRecord(const QString &aliasPath) {
	const QString path = recordPath(aliasPath);
	return !QFileInfo::exists(path) || QFile::remove(path);
}

QString aliasProblemText(AliasState state) {
	switch (state) {
	case AliasState::Loop:
		return "The alias points to itself through a chain of aliases.";
	case AliasState::Changed:
		return "The original has been replaced by a different item.";
	case AliasState::Missing:
		return "The original could not be found. It may have been deleted or its disk disconnected.";
	default:
		return {};
	}
}
