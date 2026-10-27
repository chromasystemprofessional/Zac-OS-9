/*
 * Running a program and waiting for it (shell/common/process.cpp): its
 * output, exit code and errors; stdin; a timeout killing it; a program
 * that can't be run; one that crashes; more output than a pipe holds;
 * and waiting for a condition.
 */
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <cstdio>

#include "process.h"

static int fails;

static void check(bool ok, const char *what) {
	std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);

	ProcessResult r = runProcess("sh", { "-c", "echo out; echo err >&2; exit 3" });
	check(r.started && !r.timedOut && !r.crashed && r.exitCode == 3 && !r.ok(), "the exit code, and not ok");
	check(r.out == "out\n" && r.err == "err\n", "stdout and stderr apart");
	check(runProcess("true", {}).ok(), "exit 0 is ok");

	r = runProcess("cat", {}, { "to stdin", -1 });
	check(r.ok() && r.out == "to stdin", "input goes to its stdin, which is then closed");

	QElapsedTimer clock;
	clock.start();
	r = runProcess("sleep", { "10" }, { {}, 300 });
	check(r.started && r.timedOut && !r.ok() && clock.elapsed() < 3000, "past the timeout it's killed");

	r = runProcess("/nonexistent/zacos9-nothing", {});
	check(!r.started && !r.error.isEmpty() && !r.ok(), "one that can't be run says why");

	r = runProcess("sh", { "-c", "kill -SEGV $$" });
	check(r.started && r.crashed && !r.ok(), "a crash isn't an exit code");

	r = runProcess("head", { "-c", "5000000", "/dev/zero" });
	check(r.ok() && r.out.size() == 5000000, "more output than a pipe holds");

	bool flag = false;
	QTimer::singleShot(150, [&flag] { flag = true; });
	check(waitUntil([&flag] { return flag; }, 5000), "waitUntil sees a timer fire");
	clock.restart();
	check(!waitUntil([] { return false; }, 200) && clock.elapsed() < 2000, "and gives up at its timeout");

	QProcess p;
	p.start("sleep", { "10" });
	clock.restart();
	check(!waitForProcess(&p, 200) && p.state() == QProcess::NotRunning && clock.elapsed() < 3000,
		"waitForProcess kills one that runs too long, and it's gone");
	return fails ? 1 : 0;
}
