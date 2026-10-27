/* Printing (shell/print): reading CUPS's answers, desktop printer files,
 * and Printer Picker and a print queue window driven against stand-ins for
 * lpstat, lpoptions, lp, cancel, ipptool, cupsenable and cupsdisable that
 * keep their state in files (ZACOS9_CUPS_BIN). Nothing reaches a real
 * printer. */
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include "desktopprinter.h"
#include "pickerwindow.h"
#include "printers.h"
#include "queuewindow.h"

static int fails;
static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

static void put(const QString &path, const QString &text, bool exec = false) {
	QDir().mkpath(QFileInfo(path).path());
	QFile f(path);
	f.open(QIODevice::WriteOnly);
	f.write(text.toUtf8());
	f.close();
	if (exec) {
		f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
	}
}

static QString read(const QString &path) {
	QFile f(path);
	return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

static void settle(int ms = 200) {
	QElapsedTimer t;
	t.start();
	while (t.elapsed() < ms) {
		QCoreApplication::processEvents();
		QThread::msleep(5);
	}
}

int main(int argc, char **argv) {
	QTemporaryDir dir;
	const QString bin = dir.path() + "/bin", st = dir.path() + "/state", log = dir.path() + "/log";
	QDir().mkpath(st);
	// Two printers: Laser (stopped once cupsdisable runs) and Inkjet.
	put(st + "/default", "Laser");
	put(st + "/Laser.state", "3");
	put(st + "/jobs", "7,pending,\"Letter, \\\\\\\"draft\\\\\\\"\",adam,12,0,1792026828\n"
		"8,pending-held,Photo.png,adam,300,0,1792026900\n");
	const QString sh = "#!/bin/sh\nS='" + st + "'\necho \"$(basename $0) $*\" >> '" + log + "'\n";
	put(bin + "/lpstat", sh + "case $1 in -e) printf 'Inkjet\\nLaser\\n' ;; -d) "
		"printf 'system default destination: %s\\n' \"$(cat $S/default)\" ;; esac\n", true);
	put(bin + "/lpoptions", sh + "case $1 in\n"
		"-d) printf %s \"$2\" > $S/default ;;\n"
		"-p) if [ \"$2\" = Laser ]; then echo \"printer-info='Office Laser' printer-location='Back room' "
		"printer-make-and-model='Laser Co L1' printer-state=$(cat $S/Laser.state) printer-is-accepting-jobs=true "
		"printer-state-reasons=none\"; else echo \"printer-info='Inkjet' printer-state=3 printer-is-temporary=true "
		"printer-state-reasons=media-empty-error\"; fi ;;\nesac\n", true);
	put(bin + "/ipptool", sh + "echo job-id,job-state,job-name,job-originating-user-name,job-k-octets,"
		"job-impressions-completed,time-at-creation\ncat $S/jobs\n", true);
	put(bin + "/lp", sh + "if [ \"$1\" = -i ]; then id=${2##*-}; "
		"if [ \"$4\" = resume ]; then sed -i \"s/^$id,pending-held,/$id,pending,/\" $S/jobs; "
		"else sed -i \"s/^$id,pending,/$id,pending-held,/\" $S/jobs; fi; exit 0; fi\n"
		"echo \"request id is $2-42 (1 file(s))\"\n", true);
	put(bin + "/cancel", sh + "id=${1##*-}; sed -i \"/^$id,/d\" $S/jobs\n", true);
	put(bin + "/cupsdisable", sh + "echo 5 > $S/$1.state\n", true);
	put(bin + "/cupsenable", sh + "echo 3 > $S/$1.state\n", true);
	qputenv("ZACOS9_CUPS_BIN", bin.toUtf8());
	qputenv("ZACOS9_DESKTOP_DIR", (dir.path() + "/Desktop").toUtf8());
	qputenv("ZACOS9_PRINTER_SETTINGS", "/bin/true");
	QApplication app(argc, argv);

	// Alerts are answered as they appear, and counted.
	int alerts = 0;
	QTimer answer;
	answer.callOnTimeout([&alerts] {
		if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
			++alerts;
			d->accept();
		}
	});
	answer.start(50);

	// ---- reading CUPS's answers
	const QMap<QString, QString> o = parseOptions(
		"printer-info='Brother MFC-9130CW' printer-location printer-make-and-model=\"Brother \\\"X\\\"\" "
		"device-uri=ipp://a%20b/ printer-is-accepting-jobs=false printer-state=4 "
		"printer-state-reasons=media-empty-error,cups-waiting-for-job-completed");
	check(o.value("printer-info") == "Brother MFC-9130CW" && o.contains("printer-location") &&
		o.value("printer-make-and-model") == "Brother \"X\"" && o.value("device-uri") == "ipp://a%20b/",
		"lpoptions' pairs: quoted, escaped, bare");
	const Printer p = printerFrom("Brother_MFC", o);
	check(p.shownName() == "Brother MFC-9130CW" && !p.accepting && p.state == 4, "a printer from its options");
	check(p.status() == "Printing, not taking new documents (media empty)", "its status, reasons in words");
	check(printerFrom("Some_Queue", {}).shownName() == "Some Queue" && printerFrom("Q", {}).status() == "Idle",
		"no options: named by its queue, idle");
	const QList<PrintJob> jobs = parseJobs("job-id,job-state,job-name,job-originating-user-name,job-k-octets,"
		"job-impressions-completed,time-at-creation\n1,pending-held,\"Test, \\\\\\\"quoted\\\\\\\" job\",adam,1,0,"
		"1792026828\n3,processing,b.pdf,eve,20,2,5\n");
	check(jobs.size() == 2 && jobs[0].title == "Test, \"quoted\" job" && jobs[0].held() &&
		jobs[0].owner == "adam" && jobs[0].created == 1792026828 && jobs[1].status() == "Printing page 3",
		"ipptool's jobs: quoting undone, states in words");
	check(parseJobId("request id is Laser-12 (1 file(s))\n") == 12 && parseJobId("nonsense") == 0, "lp's job id");
	check(parsePrinterNames("Inkjet\nLaser\n") == QStringList({ "Inkjet", "Laser" }) &&
		parseDefaultPrinter("system default destination: Laser\n") == "Laser" &&
		parseDefaultPrinter("no system default destination\n").isEmpty(), "lpstat's printers and default");

	// ---- desktop printers
	const QString desk = dir.path() + "/Desktop";
	QString error;
	const QString made = createDesktopPrinter(desk, "Office/Laser", "Laser", &error);
	check(made == desk + "/Office-Laser.printer" && desktopPrinterQueue(made) == "Laser",
		"a desktop printer file names its queue");
	check(createDesktopPrinter(desk, "Office/Laser", "Laser", &error) == made &&
		createDesktopPrinter(desk, "Office/Laser", "Other", &error) == desk + "/Office-Laser 2.printer",
		"made again, the same file; another queue by that name, a second one");
	put(desk + "/notes.printer", "just text");
	check(desktopPrinterQueue(desk + "/notes.printer").isEmpty() && desktopPrinterQueue(dir.path() + "/x").isEmpty(),
		"other files aren't desktop printers");
	put(dir.path() + "/a.pdf", "%PDF-1.4\n");
	put(dir.path() + "/b.txt", "hello\n");
	put(dir.path() + "/c.odt", "PK\x03\x04");
	check(canPrintDirectly(dir.path() + "/a.pdf") && canPrintDirectly(dir.path() + "/b.txt") &&
		!canPrintDirectly(dir.path() + "/c.odt"), "PDF and text print directly; office documents don't");

	// ---- asking CUPS (the stand-ins)
	const QList<Printer> printers = listPrinters();
	check(printers.size() == 2 && printers[1].name == "Laser" && printers[1].isDefault && !printers[0].isDefault &&
		printers[0].temporary && printers[1].location == "Back room", "the printers, the default marked");

	PickerWindow picker;
	picker.show();
	settle();
	check(picker.rows() == QStringList({ "Inkjet", "Office Laser" }) && picker.selectedRow() == 1,
		"Printer Picker lists them, your printer selected");
	check(picker.details().contains("Location: Back room") && picker.details().contains("Status: Idle"),
		"and says where and how it is");
	picker.pick(0);
	settle();
	check(read(st + "/default") == "Inkjet" && picker.selectedRow() == 0, "clicking another makes it your printer");
	check(picker.details().contains("Found on the network") && picker.details().contains("media empty"),
		"a printer found on the network, out of paper");
	const QString deskPrinter = picker.createDesktopPrinterForSelected();
	check(deskPrinter == desk + "/Inkjet.printer" && desktopPrinterQueue(deskPrinter) == "Inkjet",
		"Create Desktop Printer puts it on the desktop");

	QueueWindow queue("Laser");
	queue.show();
	settle();
	check(queue.rows() == QStringList({ "Letter, \"draft\"  –  Waiting", "Photo.png  –  On hold" }),
		"the queue's documents and how they are");
	check(queue.printerStatus() == "Idle. 2 documents.", "the printer's status");
	queue.selectJob(1);
	check(queue.jobDetails().startsWith("On hold, from adam, 300 KB, sent "), "the selected document's details");
	queue.holdOrResume();
	settle();
	check(read(log).contains("lp -i Laser-8 -H resume") && queue.rows().value(1) == "Photo.png  –  Waiting",
		"Resume lets a held document print");
	queue.selectJob(0);
	queue.holdOrResume();
	settle();
	check(read(log).contains("lp -i Laser-7 -H hold") && queue.rows().value(0).endsWith("On hold"), "Hold holds one");
	queue.removeJob();
	settle();
	check(read(log).contains("cancel Laser-7") && queue.rows().size() == 1, "Remove cancels it");
	queue.stopOrStart();
	settle();
	check(read(log).contains("cupsdisable Laser") && queue.printerStatus().startsWith("Stopped"),
		"Stop Printing stops the printer");
	queue.stopOrStart();
	settle();
	check(read(log).contains("cupsenable Laser") && queue.printerStatus().startsWith("Idle"), "Start Printing");
	const int before = alerts;
	queue.printDropped({ dir.path() + "/a.pdf", dir.path() + "/c.odt" });
	settle();
	check(read(log).contains("lp -d Laser -t a.pdf -- " + dir.path() + "/a.pdf") &&
		!read(log).contains("c.odt") && alerts == before + 1,
		"dropped documents print there; one that can't is explained");
	return fails ? 1 : 0;
}
