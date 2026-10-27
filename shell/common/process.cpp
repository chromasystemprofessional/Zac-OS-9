#include "process.h"
#include "programs.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <unistd.h>

/* One turn of the event loop: windows drawn, the compositor answered, no
 * clicks or keys taken; back at once if something happens, else in 100 ms. */
static void turn() {
	QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents, 100);
}

bool waitUntil(const std::function<bool()> &ready, int timeoutMs) {
	QElapsedTimer clock;
	clock.start();
	while (!ready() && (timeoutMs < 0 || clock.elapsed() < timeoutMs)) {
		turn();
	}
	return ready();
}

bool waitForProcess(QProcess *process, int timeoutMs) {
	const auto done = [process] {
		return process->state() == QProcess::NotRunning;
	};
	if (waitUntil(done, timeoutMs)) {
		return true;
	}
	process->kill();
	waitUntil(done, 1000);
	return false;
}

bool processSucceeded(const QProcess &process) {
	return process.error() != QProcess::FailedToStart && process.state() == QProcess::NotRunning &&
		process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

ProcessResult runProcess(const QString &program, const QStringList &args, const ProcessOptions &options) {
	ProcessResult result;
	QProcess process;
	process.start(program, args);
	if (!options.input.isEmpty()) {
		process.write(options.input);
	}
	process.closeWriteChannel();
	result.timedOut = !waitForProcess(&process, options.timeoutMs);
	result.started = process.error() != QProcess::FailedToStart;
	if (!result.started) {
		result.error = process.errorString();
		return result;
	}
	result.crashed = !result.timedOut && process.exitStatus() == QProcess::CrashExit;
	result.exitCode = result.timedOut || result.crashed ? -1 : process.exitCode();
	result.out = process.readAllStandardOutput();
	result.err = process.readAllStandardError();
	return result;
}

QString runHelper(const QString &name, const QString &sourceDir, const QString &what,
		const QStringList &args, bool *ok, QString *err, const HelperOptions &options) {
	QString program = rootHelperPath(name, sourceDir);
	QStringList arguments = args;
	if (options.asRoot && geteuid() != 0) {
		arguments.prepend(program);
		program = "pkexec";
	}
	const ProcessResult r = runProcess(program, arguments, { options.input, -1 });
	*ok = r.ok();
	if (err) {
		*err = QString::fromUtf8(r.err).trimmed().section('\n', -1);
		if (!*ok && err->isEmpty()) {
			*err = r.started && !r.crashed
				? QString("The %1 helper stopped with status %2.").arg(what).arg(r.exitCode)
				: QString("The %1 helper couldn’t be run (%2).").arg(what, r.started ? QString("it crashed") : r.error);
		}
	}
	return QString::fromUtf8(r.out);
}
