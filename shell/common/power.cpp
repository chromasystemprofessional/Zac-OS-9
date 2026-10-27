#include "power.h"
#include "process.h"

#include <QCoreApplication>
#include <QProcess>
#include <QRegularExpression>

QString systemPower(Power what) {
	const QString verb = what == Power::ShutDown ? "shut down" : "restart";
	QProcess p;
	p.setProcessChannelMode(QProcess::MergedChannels);
	p.start("systemctl", { what == Power::ShutDown ? "poweroff" : "reboot" });
	const bool done = waitForProcess(&p, 15000);
	if (p.error() == QProcess::FailedToStart) {
		return QString("The computer couldn’t %1: systemctl isn’t there.").arg(verb);
	}
	if (!done) {
		return QString("The computer didn’t %1: it isn’t answering.").arg(verb);
	}
	if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0) {
		return QString();
	}
	/* "Operation inhibited by "unattended-upgrades" ...", "Access denied". */
	QString why = QString::fromUtf8(p.readAll()).trimmed().section('\n', 0, 0);
	why.remove(QRegularExpression("^(Failed to [^:]*: )"));
	return QString("The computer couldn’t %1%2").arg(verb, why.isEmpty() ? QString(".") : ": " + why);
}
