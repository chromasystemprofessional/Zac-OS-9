#include "printers.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QUrl>

namespace {

/* Our Get-Jobs request: as the user asking, so CUPS shows the names of
 * their own jobs (it hides them from anonymous requests). */
const char *const JOBS_TEST = R"({
	OPERATION Get-Jobs
	GROUP operation-attributes-tag
	ATTR charset attributes-charset utf-8
	ATTR language attributes-natural-language en
	ATTR uri printer-uri $uri
	ATTR name requesting-user-name $user
	ATTR keyword which-jobs not-completed
	ATTR keyword requested-attributes job-id,job-state,job-name,job-originating-user-name,job-k-octets,job-impressions-completed,time-at-creation
	STATUS successful-ok
	DISPLAY job-id
	DISPLAY job-state
	DISPLAY job-name
	DISPLAY job-originating-user-name
	DISPLAY job-k-octets
	DISPLAY job-impressions-completed
	DISPLAY time-at-creation
}
)";

QString program(const QString &name) {
	const QString dir = qEnvironmentVariable("ZACOS9_CUPS_BIN");
	if (!dir.isEmpty() && QFileInfo(dir + "/" + name).isExecutable()) {
		return dir + "/" + name;
	}
	return name;
}

struct Result {
	bool ok = false;
	QString out, err;
};

/* Runs it to the end in the C locale, keeping the window drawn. No
 * waitForFinished(): see CLAUDE.md. */
Result run(const QString &name, const QStringList &args, int timeoutMs = 20000) {
	QProcess p;
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	env.insert("LC_ALL", "C");
	p.setProcessEnvironment(env);
	bool done = false;
	QObject::connect(&p, &QProcess::finished, [&done] { done = true; });
	QObject::connect(&p, &QProcess::errorOccurred, [&done](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			done = true;
		}
	});
	p.start(program(name), args);
	QElapsedTimer t;
	t.start();
	while (!done && t.elapsed() < timeoutMs) {
		QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 50);
	}
	Result r;
	if (!done) {
		p.kill();
		r.err = name + " didn't answer.";
		return r;
	}
	r.out = QString::fromUtf8(p.readAllStandardOutput());
	r.err = QString::fromUtf8(p.readAllStandardError()).trimmed();
	r.ok = p.error() != QProcess::FailedToStart && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
	if (!r.ok && r.err.isEmpty()) {
		r.err = p.error() == QProcess::FailedToStart ? name + " isn't installed." : name + " failed.";
	}
	// "lp: Error - ..." reads better without the program's name.
	r.err = r.err.section('\n', -1).remove(QRegularExpression("^\\S+: (Error - )?"));
	if (!r.err.isEmpty() && r.err[0].isLower()) {
		r.err[0] = r.err[0].toUpper();
	}
	return r;
}

/* A CSV field from ipptool -c: quoted, with backslash escapes, the
 * value's own escapes inside. */
QStringList csvFields(const QString &line) {
	QStringList out;
	QString cur;
	bool quoted = false;
	for (int i = 0; i < line.size(); ++i) {
		const QChar c = line[i];
		if (quoted) {
			if (c == '\\' && i + 1 < line.size()) {
				cur += line[++i];
			} else if (c == '"') {
				quoted = false;
			} else {
				cur += c;
			}
		} else if (c == '"') {
			quoted = true;
		} else if (c == ',') {
			out << cur;
			cur.clear();
		} else {
			cur += c;
		}
	}
	out << cur;
	for (QString &f : out) {
		f.replace("\\\"", "\"").replace("\\\\", "\\");
	}
	return out;
}

} // namespace

QString Printer::status() const {
	QString s = state == 4 ? "Printing" : state == 5 ? "Stopped" : "Idle";
	if (!accepting) {
		s += ", not taking new documents";
	}
	// The reasons worth telling: out of paper, toner, offline...
	QStringList shown;
	for (QString r : reasons.split(',', Qt::SkipEmptyParts)) {
		r = r.trimmed();
		if (r == "none" || r == "paused" || r.startsWith("cups-") || r.endsWith("-report")) {
			continue;
		}
		r.remove(QRegularExpression("-(error|warning)$"));
		shown << r.replace('-', ' ');
	}
	if (!shown.isEmpty()) {
		s += " (" + shown.join(", ") + ")";
	}
	return s;
}

QString PrintJob::status() const {
	if (state == "pending") return "Waiting";
	if (state == "pending-held") return "On hold";
	if (state == "processing") return pagesDone > 0 ? QString("Printing page %1").arg(pagesDone + 1) : "Printing";
	if (state == "processing-stopped") return "Stopped";
	if (state == "canceled") return "Cancelled";
	if (state == "aborted") return "Failed";
	if (state == "completed") return "Done";
	return state;
}

QStringList parsePrinterNames(const QString &lpstatE) {
	QStringList out;
	for (const QString &line : lpstatE.split('\n', Qt::SkipEmptyParts)) {
		const QString name = line.trimmed();
		if (!name.isEmpty() && !name.contains(' ')) {
			out << name;
		}
	}
	return out;
}

QString parseDefaultPrinter(const QString &lpstatD) {
	const QRegularExpressionMatch m = QRegularExpression("destination: (\\S+)").match(lpstatD);
	return m.hasMatch() ? m.captured(1) : QString();
}

QMap<QString, QString> parseOptions(const QString &text) {
	QMap<QString, QString> out;
	QString key, cur;
	bool inValue = false;
	QChar quote;
	const auto finish = [&] {
		if (!key.isEmpty()) {
			out.insert(key, cur);
		} else if (!cur.isEmpty()) {
			out.insert(cur, QString()); /* a bare keyword */
		}
		key.clear();
		cur.clear();
		inValue = false;
	};
	for (int i = 0; i < text.size(); ++i) {
		const QChar c = text[i];
		if (!quote.isNull()) {
			if (c == quote) {
				quote = QChar();
			} else if (c == '\\' && quote == '"' && i + 1 < text.size()) {
				cur += text[++i];
			} else {
				cur += c;
			}
		} else if (c == '\'' || c == '"') {
			quote = c;
		} else if (c == '\\' && i + 1 < text.size()) {
			cur += text[++i];
		} else if (c.isSpace()) {
			finish();
		} else if (c == '=' && !inValue) {
			key = cur;
			cur.clear();
			inValue = true;
		} else {
			cur += c;
		}
	}
	finish();
	return out;
}

Printer printerFrom(const QString &name, const QMap<QString, QString> &o) {
	Printer p;
	p.name = name;
	p.info = o.value("printer-info");
	p.model = o.value("printer-make-and-model");
	p.location = o.value("printer-location");
	p.uri = o.value("device-uri");
	p.state = o.value("printer-state", "3").toInt();
	p.accepting = o.value("printer-is-accepting-jobs", "true") != "false";
	p.reasons = o.value("printer-state-reasons", "none");
	p.temporary = o.value("printer-is-temporary") == "true";
	return p;
}

QList<PrintJob> parseJobs(const QString &csv) {
	QList<PrintJob> out;
	const QStringList lines = csv.split('\n', Qt::SkipEmptyParts);
	if (lines.isEmpty()) {
		return out;
	}
	const QStringList header = csvFields(lines[0]);
	const auto col = [&header](const QStringList &f, const char *name) { return f.value(header.indexOf(name)); };
	for (int i = 1; i < lines.size(); ++i) {
		const QStringList f = csvFields(lines[i]);
		PrintJob j;
		j.id = col(f, "job-id").toInt();
		if (j.id <= 0) {
			continue;
		}
		j.state = col(f, "job-state");
		j.title = col(f, "job-name");
		j.owner = col(f, "job-originating-user-name");
		j.kilobytes = col(f, "job-k-octets").toInt();
		j.pagesDone = col(f, "job-impressions-completed").toInt();
		j.created = col(f, "time-at-creation").toLongLong();
		out << j;
	}
	return out;
}

int parseJobId(const QString &lpOut) {
	const QRegularExpressionMatch m = QRegularExpression("request id is \\S+-(\\d+)").match(lpOut);
	return m.hasMatch() ? m.captured(1).toInt() : 0;
}

QString defaultPrinter() {
	return parseDefaultPrinter(run("lpstat", { "-d" }).out);
}

QList<Printer> listPrinters() {
	QList<Printer> out;
	const QString def = defaultPrinter();
	for (const QString &name : parsePrinterNames(run("lpstat", { "-e" }).out)) {
		Printer p = printerFrom(name, parseOptions(run("lpoptions", { "-p", name }).out));
		p.isDefault = name == def;
		out << p;
	}
	return out;
}

QList<PrintJob> listJobs(const QString &printer) {
	QTemporaryFile test(QDir::tempPath() + "/zacos9-jobs-XXXXXX.test");
	if (!test.open()) {
		return {};
	}
	test.write(JOBS_TEST);
	test.flush();
	const QString uri = "ipp://localhost/printers/" + QString::fromUtf8(QUrl::toPercentEncoding(printer));
	return parseJobs(run("ipptool", { "-c", uri, test.fileName() }).out);
}

QList<int> printFiles(const QString &printer, const QStringList &files, QString *error) {
	QList<int> ids;
	for (const QString &file : files) {
		const Result r = run("lp", { "-d", printer, "-t", QFileInfo(file).fileName(), "--", file });
		if (!r.ok) {
			*error = r.err;
			return ids;
		}
		ids << parseJobId(r.out);
	}
	return ids;
}

bool cancelJob(const QString &printer, int id, QString *error) {
	const Result r = run("cancel", { printer + "-" + QString::number(id) });
	*error = r.err;
	return r.ok;
}

bool holdJob(const QString &printer, int id, bool hold, QString *error) {
	const Result r = run("lp", { "-i", printer + "-" + QString::number(id), "-H", hold ? "hold" : "resume" });
	*error = r.err;
	return r.ok;
}

bool urgentJob(const QString &printer, int id, QString *error) {
	/* CUPS prints the highest priority first among waiting jobs. */
	const Result r = run("lp", { "-i", printer + "-" + QString::number(id), "-q", "100" });
	*error = r.err;
	return r.ok;
}

bool setDefaultPrinter(const QString &printer, QString *error) {
	const Result r = run("lpoptions", { "-d", printer });
	*error = r.err;
	return r.ok;
}

bool setPrinterEnabled(const QString &printer, bool enabled, QString *error) {
	if (!qEnvironmentVariableIsEmpty("ZACOS9_CUPS_BIN")) {
		const Result r = run(enabled ? "cupsenable" : "cupsdisable", { printer });
		*error = r.err;
		return r.ok;
	}
	QDBusMessage call = QDBusMessage::createMethodCall("org.opensuse.CupsPkHelper.Mechanism", "/",
		"org.opensuse.CupsPkHelper.Mechanism", "PrinterSetEnabled");
	call << printer << enabled;
	QDBusPendingCallWatcher watcher(QDBusConnection::systemBus().asyncCall(call, 30000));
	while (!watcher.isFinished()) {
		QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 50);
	}
	QDBusPendingReply<QString> reply = watcher;
	if (reply.isError()) {
		*error = reply.error().name().contains("NotPrivileged") || reply.error().name().contains("NotAuthorized")
			? QString("Only an administrator can stop or start a printer.")
			: reply.error().message();
		return false;
	}
	*error = reply.value(); /* cups-pk-helper's own error, if any */
	return error->isEmpty();
}

bool canPrintDirectly(const QString &path) {
	const QMimeType type = QMimeDatabase().mimeTypeForFile(path);
	for (const char *ok : { "application/pdf", "application/postscript", "text/plain", "image/png", "image/jpeg",
			"image/gif", "image/tiff" }) {
		if (type.inherits(ok)) {
			return true;
		}
	}
	return false;
}
