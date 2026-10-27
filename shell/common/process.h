#pragma once

/*
 * Running another program and waiting for it without freezing the window.
 * QProcess::waitForFinished() can deadlock in a Qt program on Wayland,
 * even after the program has exited: zacos9-afp once completed its whole
 * exchange with a server (Netatalk's own log showed the login and a clean
 * logout) and exited, and waitForFinished() never returned. So ZacOS
 * never calls it (CLAUDE.md):
 * these wait by running Qt's event loop instead, drawing windows and
 * answering the compositor, but not taking clicks or keys meanwhile.
 */

#include <QByteArray>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <functional>

/* Waits for `process`, already started, to finish: true if it did (or
 * couldn't start: process->error() says FailedToStart), false if
 * `timeoutMs` ran out first (-1: no limit), when it's killed and given a
 * second to go. Its exit status, code and output are the caller's to read. */
bool waitForProcess(QProcess *process, int timeoutMs = -1);

/* `process`, finished, ran and exited 0. */
bool processSucceeded(const QProcess &process);

/* Waits until `ready()` is true or `timeoutMs` runs out; ready()'s last answer. */
bool waitUntil(const std::function<bool()> &ready, int timeoutMs);

/* What a program run with runProcess() did. */
struct ProcessResult {
	bool started = false;  /* false: it couldn't be run (error says why) */
	bool timedOut = false; /* it was killed for taking too long */
	bool crashed = false;
	int exitCode = -1;
	QByteArray out, err;
	QString error;         /* QProcess's reason it couldn't run */

	/* Ran, finished in time and exited 0. */
	bool ok() const { return started && !timedOut && !crashed && exitCode == 0; }
};

struct ProcessOptions {
	QByteArray input;     /* written to its stdin, which is then closed */
	int timeoutMs = -1;   /* -1: no limit */
};

/* Runs `program` with `args` to the end (or the timeout), as waitForProcess() waits. */
ProcessResult runProcess(const QString &program, const QStringList &args,
	const ProcessOptions &options = {});
