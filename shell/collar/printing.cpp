#include "printing.h"

#include <QProcess>

#include "printers.h"

PrinterChoice::PrinterChoice(QObject *parent) : QObject(parent) {}

QString PrinterChoice::shownName(const QString &queue) {
	return QString(queue).replace('_', ' ');
}

/* A shell command in the C locale, with the stand-ins first if asked;
 * `arg` is its $0. */
QProcess *PrinterChoice::start(const QString &script, const QString &arg) {
	auto *p = new QProcess(this);
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	env.insert("LC_ALL", "C");
	const QString bin = qEnvironmentVariable("ZACOS9_CUPS_BIN");
	if (!bin.isEmpty()) {
		env.insert("PATH", bin + ":" + env.value("PATH"));
	}
	p->setProcessEnvironment(env);
	p->start("/bin/sh", arg.isEmpty() ? QStringList{ "-c", script } : QStringList{ "-c", script, arg });
	return p;
}

void PrinterChoice::refresh() {
	if (m_look) {
		return; /* the last look is still going */
	}
	m_look = start("lpstat -e; echo '--'; lpstat -d");
	connect(m_look, &QProcess::finished, this, [this] {
		const QString out = QString::fromUtf8(m_look->readAllStandardOutput());
		m_look->deleteLater();
		m_look = nullptr;
		const QStringList was = printers;
		const QString wasCurrent = current;
		printers = parsePrinterNames(out.section("--\n", 0, 0));
		current = parseDefaultPrinter(out.section("--\n", 1));
		if ((printers != was || current != wasCurrent) && changed) {
			changed();
		}
	});
}

void PrinterChoice::choose(const QString &printer) {
	QProcess *p = start("lpoptions -d \"$0\" >/dev/null", printer);
	connect(p, &QProcess::finished, this, [this, p, printer](int code) {
		const QString error = QString::fromUtf8(p->readAllStandardError()).trimmed();
		p->deleteLater();
		if (code != 0 && failed) {
			failed(QString("“%1” couldn't be made your printer. %2").arg(shownName(printer), error));
		}
		refresh();
	});
}
