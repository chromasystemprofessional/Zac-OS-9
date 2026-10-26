#pragma once

/*
 * Printing through CUPS (Mac OS 9's Chooser, desktop printers and
 * PrintMonitor; Missing Manual ch. 19): the printers it knows or has found
 * on the network, their queues, and printing files. Done with CUPS's own
 * programs (lpstat, lpoptions, lp, cancel, ipptool) in the C locale, as
 * this machine has no libcups headers; stopping and starting a printer,
 * which needs an administrator, through cups-pk-helper on the system bus.
 *
 * ZACOS9_CUPS_BIN puts a folder of stand-ins for those programs first
 * (tests); cupsenable and cupsdisable from there replace cups-pk-helper.
 */

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

struct Printer {
	QString name;     /* the queue */
	QString info;     /* what to call it: "Brother MFC-9130CW" */
	QString model, location, uri;
	int state = 3;    /* IPP: 3 idle, 4 printing, 5 stopped */
	bool accepting = true;
	QString reasons;  /* printer-state-reasons, "none" when all is well */
	bool isDefault = false;
	bool temporary = false; /* found on the network, not set up yet */

	QString shownName() const { return info.isEmpty() ? QString(name).replace('_', ' ') : info; }
	/* "Idle", "Printing", "Stopped", with a reason if there is one. */
	QString status() const;
};

struct PrintJob {
	int id = 0;
	QString title, owner;
	QString state; /* pending, pending-held, processing, processing-stopped, ... */
	int kilobytes = 0, pagesDone = 0;
	qint64 created = 0; /* seconds since 1970 */

	bool held() const { return state == "pending-held"; }
	QString status() const; /* "Waiting", "On hold", "Printing", ... */
};

/* ---- reading CUPS's answers (pure, for tests) ---- */
/* lpstat -e: one name a line. */
QStringList parsePrinterNames(const QString &lpstatE);
/* lpstat -d: the default's name, or "". */
QString parseDefaultPrinter(const QString &lpstatD);
/* lpoptions -p: key=value pairs, values quoted as a shell would. */
QMap<QString, QString> parseOptions(const QString &lpoptions);
Printer printerFrom(const QString &name, const QMap<QString, QString> &options);
/* ipptool -c with our Get-Jobs request: a header, then a job a line. */
QList<PrintJob> parseJobs(const QString &csv);
/* lp: "request id is NAME-12 (1 file(s))" -> 12, or 0. */
int parseJobId(const QString &lpOut);

/* ---- asking CUPS ---- */
QList<Printer> listPrinters();
QString defaultPrinter();
QList<PrintJob> listJobs(const QString &printer);
/* Prints each file; the job ids, or an error. */
QList<int> printFiles(const QString &printer, const QStringList &files, QString *error);
bool cancelJob(const QString &printer, int id, QString *error);
bool holdJob(const QString &printer, int id, bool hold, QString *error);
/* Print Next: the job goes ahead of the others waiting (priority 100). */
bool urgentJob(const QString &printer, int id, QString *error);
/* Your default printer (lpoptions -d): no password needed. */
bool setDefaultPrinter(const QString &printer, QString *error);
/* Stop Printing / Start Printing: an administrator's, through cups-pk-helper. */
bool setPrinterEnabled(const QString &printer, bool enabled, QString *error);

/* Files CUPS's filters can print by themselves: PDF, PostScript, text,
 * pictures. Others are printed from their own programs. */
bool canPrintDirectly(const QString &path);
