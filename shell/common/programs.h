#pragma once

/*
 * The desktop's other programs and helpers, for the Qt programs: where they
 * are installed beside this one, or in the build tree it runs from
 * (lib/locate.h has the rules).
 */

#include <QByteArray>
#include <QCoreApplication>
#include <QFileInfo>
#include <QString>
#include <climits>

#include "locate.h"

/* Another of the desktop's programs, to run: its path, or `name` alone,
 * for $PATH, when neither beside this program nor in its build tree. */
inline QString siblingProgram(const QString &name) {
	char path[PATH_MAX];
	return pl_sibling_program(name.toUtf8().constData(), path, sizeof(path))
		? QString::fromUtf8(path) : name;
}

/* `relative` under the source tree that was built, when this program runs
 * from its build tree; "" when it's installed. */
inline QString sourceTreePath(const QString &relative) {
	char path[PATH_MAX];
	return pl_source_path(relative.toUtf8().constData(), path, sizeof(path))
		? QString::fromUtf8(path) : QString();
}

/* `relative` under the build tree this program runs from; "" when it's
 * installed. */
inline QString buildTreePath(const QString &relative) {
	char path[PATH_MAX];
	return pl_build_path(relative.toUtf8().constData(), path, sizeof(path))
		? QString::fromUtf8(path) : QString();
}

/* A root helper, zacos9-NAME-helper: $ZACOS9_NAME_HELPER for a test's
 * stand-in, else installed beside this program's prefix (libexec/zacos9),
 * else, from a build tree, the source tree's system/SOURCEDIR/zacos9-NAME-helper,
 * else /usr/libexec/zacos9's. A path pkexec can run. */
inline QString rootHelperPath(const QString &name, const QString &sourceDir) {
	const QString env = qEnvironmentVariable(("ZACOS9_" + name.toUpper() + "_HELPER").toUtf8().constData());
	if (!env.isEmpty()) {
		return env;
	}
	const QString file = "zacos9-" + name + "-helper";
	const QString dir = QCoreApplication::applicationDirPath();
	for (const QString &candidate : { dir + "/../libexec/zacos9/" + file,
			sourceTreePath("system/" + sourceDir + "/" + file) }) {
		if (!candidate.isEmpty() && QFileInfo(candidate).isExecutable()) {
			return QFileInfo(candidate).canonicalFilePath();
		}
	}
	return "/usr/libexec/zacos9/" + file;
}
