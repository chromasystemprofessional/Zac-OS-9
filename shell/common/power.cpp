#include "power.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QRegularExpression>

QString systemPower(Power what) {
	const QString verb = what == Power::ShutDown ? "shut down" : "restart";
	QProcess p;
	p.setProcessChannelMode(QProcess::MergedChannels);
	bool done = false, started = true;
	QObject::connect(&p, &QProcess::finished, [&done] { done = true; });
	QObject::connect(&p, &QProcess::errorOccurred, [&](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			started = false;
			done = true;
		}
	});
	p.start("systemctl", { what == Power::ShutDown ? "poweroff" : "reboot" });
	/* As afpclient.cpp's waitForProcess: QProcess::waitForFinished()
	 * deadlocks in a Qt/Wayland program. */
	QElapsedTimer clock;
	clock.start();
	while (!done && clock.elapsed() < 15000) {
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents, 100);
	}
	if (!started) {
		return QString("The computer couldn’t %1: systemctl isn’t there.").arg(verb);
	}
	if (!done) {
		p.kill();
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
