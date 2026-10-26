/* Shut Down and Restart outside a session (shell/finder/power.cpp, issue
 * #38): systemctl poweroff|reboot found on a user's PATH (not
 * /usr/sbin/reboot), nothing said when logind takes it, and its reason
 * said when it doesn't, never silence. A stand-in systemctl first on PATH. */
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "power.h"

static int fails;
static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

int main(int argc, char *argv[]) {
	QCoreApplication app(argc, argv);
	QTemporaryDir dir;
	const QString log = dir.path() + "/log", fake = dir.path() + "/systemctl";
	auto stand_in = [&](const QString &body) {
		QFile f(fake);
		f.open(QIODevice::WriteOnly);
		f.write(("#!/bin/sh\necho \"$*\" >>'" + log + "'\n" + body + "\n").toUtf8());
		f.close();
		f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
	};
	qputenv("PATH", (dir.path() + ":/usr/bin:/bin").toUtf8());

	stand_in("exit 0");
	check(systemPower(Power::ShutDown).isEmpty(), "shutting down: nothing to say once logind has it");
	check(systemPower(Power::Restart).isEmpty(), "restarting too");
	QFile f(log);
	f.open(QIODevice::ReadOnly);
	check(f.readAll() == "poweroff\nreboot\n", "systemctl poweroff, systemctl reboot");

	stand_in("echo 'Failed to reboot system via logind: Access denied' >&2; exit 1");
	const QString denied = systemPower(Power::Restart);
	check(denied == "The computer couldn’t restart: Access denied", "refused: logind's reason (" + denied + ")");

	qputenv("PATH", dir.path().toUtf8() + "/nowhere");
	check(!systemPower(Power::ShutDown).isEmpty(), "no systemctl: said, not silent");

	QTextStream(stdout) << (fails ? "FAILED\n" : "all passed\n");
	return fails ? 1 : 0;
}
