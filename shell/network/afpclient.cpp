#include "afpclient.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <sys/wait.h>
#include <unistd.h>

/* QProcess::waitForFinished() was tried first here and, in this Wayland
 * session, deadlocked: zacos9-afp completed its exchange with the
 * server in full (confirmed from Netatalk's own log: login, then a
 * clean "AFP logout" a moment later) and exited, but waitForFinished()
 * never returned. The rest of this project already avoids
 * waitForFinished() for exactly this reason (see runSharingHelper and
 * runAppstoreHelper); this file didn't, until it hit the same problem. */
/* True if the process finished on its own; false (and killed) if
 * `timeoutMs` ran out first. */
static bool waitForProcess(QProcess *p, int timeoutMs) {
	bool done = false;
	QObject::connect(p, &QProcess::finished, [&done] { done = true; });
	QObject::connect(p, &QProcess::errorOccurred, [&done](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			done = true;
		}
	});
	QElapsedTimer clock;
	clock.start();
	while (!done && clock.elapsed() < timeoutMs) {
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents |
			QEventLoop::WaitForMoreEvents, 100);
	}
	if (!done) {
		p->kill();
		p->waitForFinished(1000);
		return false;
	}
	return true;
}

static QString afpClientPath() {
	const QString env = qEnvironmentVariable("ZACOS9_AFP_CLIENT");
	if (!env.isEmpty()) {
		return env;
	}
	const QString dir = QCoreApplication::applicationDirPath();
	for (const QString &candidate : { dir + "/../libexec/zacos9/zacos9-afp",
			dir + "/../network/zacos9-afp" }) {
		if (QFileInfo(candidate).isExecutable()) {
			return QFileInfo(candidate).canonicalFilePath();
		}
	}
	return "zacos9-afp"; /* installed: next to the other binaries, on $PATH */
}

AfpServerInfo afpServerInfo(const QString &address) {
	AfpServerInfo info;
	QProcess p;
	p.start(afpClientPath(), { "info", address });
	if (!waitForProcess(&p, 8000)) {
		info.error = "The server didn’t answer.";
		return info;
	}
	const QString out = QString::fromUtf8(p.readAllStandardOutput());
	if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
		info.error = QString::fromUtf8(p.readAllStandardError()).trimmed();
		if (info.error.isEmpty()) {
			info.error = "The server could not be reached.";
		}
		return info;
	}
	bool hasSecureUam = false, hasCleartextUam = false;
	for (const QString &line : out.split('\n', Qt::SkipEmptyParts)) {
		const int tab = line.indexOf('\t');
		if (tab < 0) {
			continue;
		}
		const QString key = line.left(tab), value = line.mid(tab + 1);
		if (key == "name") {
			info.name = value;
		} else if (key == "machine") {
			info.machine = value;
		} else if (key == "uam") {
			info.uams << value;
			if (value.compare("Cleartxt Passwrd", Qt::CaseInsensitive) == 0) {
				hasCleartextUam = true;
			} else if (value.compare("No User Authent", Qt::CaseInsensitive) != 0) {
				hasSecureUam = true;
			}
		} else if (key == "guest") {
			info.guestAllowed = value == "yes";
		}
	}
	info.cleartextOnly = hasCleartextUam && !hasSecureUam;
	info.ok = !info.name.isEmpty();
	if (!info.ok) {
		info.error = "“" + address + "” isn’t a file server.";
	}
	return info;
}

std::vector<AfpVolume> afpListVolumes(const QString &address, const QString &user,
		const QString &password, bool cleartext, bool *ok, bool *wrongPassword, QString *error) {
	std::vector<AfpVolume> out;
	*ok = false;
	*wrongPassword = false;
	QStringList args = { "volumes" };
	if (!user.isEmpty()) {
		args << "--user" << user;
	}
	if (cleartext) {
		args << "--cleartext";
	}
	args << address;
	QProcess p;
	p.start(afpClientPath(), args);
	if (!user.isEmpty()) {
		p.write(password.toUtf8() + '\n');
	}
	p.closeWriteChannel();
	if (!waitForProcess(&p, 15000)) {
		*error = "The server didn’t answer.";
		return out;
	}
	if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
		*wrongPassword = p.exitCode() == 2;
		*error = QString::fromUtf8(p.readAllStandardError()).trimmed();
		if (error->isEmpty()) {
			*error = *wrongPassword ? "The name or password wasn’t correct."
				: "The server’s volumes could not be listed.";
		}
		return out;
	}
	for (const QString &line : QString::fromUtf8(p.readAllStandardOutput())
			.split('\n', Qt::SkipEmptyParts)) {
		AfpVolume v;
		const int tab = line.indexOf('\t');
		v.name = tab < 0 ? line : line.left(tab);
		v.hasPassword = tab >= 0;
		out.push_back(v);
	}
	*ok = true;
	return out;
}

/* Single-quotes `s` for /bin/sh, the usual way: close the quote, escape
 * the quote itself, reopen it. */
static QString shellQuote(const QString &s) {
	QString out = "'";
	for (const QChar &c : s) {
		out += c == '\'' ? QStringLiteral("'\\''") : QString(c);
	}
	out += "'";
	return out;
}

bool afpMount(const QString &address, const QString &volume, const QString &mountPoint,
		const QString &user, const QString &password, bool cleartext, QString *error) {
	if (!QDir().mkpath(mountPoint)) {
		*error = "The mount point couldn’t be created.";
		return false;
	}
	QString command = shellQuote(afpClientPath()) + " mount";
	if (!user.isEmpty()) {
		command += " --user " + shellQuote(user);
	}
	if (cleartext) {
		command += " --cleartext";
	}
	/* No "--" here: zacos9-afp's own parser doesn't recognize it as an
	 * end-of-options marker (tested live: it treats "--" as an unknown
	 * flag of its own and refuses with its usage message), it simply
	 * stops consuming "--something" flags once it reaches SERVER. */
	command += " " + shellQuote(address) + " " + shellQuote(volume) + " " + shellQuote(mountPoint);
	/* The directory is ours (we just made it, or it was already an
	 * empty mount point); once the volume is unmounted, afpfs_run
	 * returns and there is nothing left to clean up but the directory
	 * itself. A directory that failed to mount (so still has something
	 * in it, or was never ours) is left alone: rmdir only removes an
	 * empty one. */
	command += "; rmdir " + shellQuote(mountPoint) + " 2>/dev/null";

	int pipefd[2];
	if (pipe(pipefd) != 0) {
		*error = "Couldn’t start the mount.";
		return false;
	}
	const pid_t pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		*error = "Couldn’t start the mount.";
		return false;
	}
	if (pid == 0) {
		/* The middle process: detach fully (a new session, so closing
		 * the Network Browser or even the whole desktop session, as
		 * far as process groups go, doesn't signal this), then fork
		 * once more so the actual mount is reparented to init and this
		 * one can exit immediately — the parent only waits on this
		 * short-lived middle step, never on the mount itself. */
		setsid();
		dup2(pipefd[0], STDIN_FILENO);
		close(pipefd[0]);
		close(pipefd[1]);
		if (fork() == 0) {
			execl("/bin/sh", "sh", "-c", command.toUtf8().constData(), (char *)NULL);
			_exit(127);
		}
		_exit(0);
	}
	close(pipefd[0]);
	if (!user.isEmpty()) {
		const QByteArray pw = password.toUtf8() + '\n';
		if (write(pipefd[1], pw.constData(), static_cast<size_t>(pw.size())) < 0) {
			/* Nothing to recover: the child reads what it reads, and
			 * an empty password will simply be refused like any other
			 * wrong one — no different in kind from a network error. */
		}
	}
	close(pipefd[1]);
	int status = 0;
	waitpid(pid, &status, 0);
	return true;
}
