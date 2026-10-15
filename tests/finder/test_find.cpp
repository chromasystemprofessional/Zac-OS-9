/* Find (shell/finder/findcriteria.cpp, findindex.cpp, findwindow.cpp):
 * criteria against real files, the search index's query and answers (a
 * fake tinysparql), and searches through to the Items Found window. */
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QUrl>
#include <sys/stat.h>
#include <unistd.h>

#include "findcriteria.h"
#include "findindex.h"
#include "items.h"

#define private public
#define protected public
#include "findwindow.h"
#undef protected
#undef private

static int fails;
static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

static bool until(const std::function<bool()> &done, int ms = 8000) {
	QElapsedTimer t;
	t.start();
	while (!done() && t.elapsed() < ms) {
		QCoreApplication::processEvents();
		QThread::msleep(5);
	}
	return done();
}

static void write(const QString &path, const QByteArray &data) {
	QFile f(path);
	f.open(QIODevice::WriteOnly);
	f.write(data);
}

static FindCriterion row(FindField field, FindOp op, const QString &text = QString(), int choice = 0) {
	FindCriterion c;
	c.field = field;
	c.op = op;
	c.text = text;
	c.choice = choice;
	return c;
}

static FindQuery in(const QString &folder, QList<FindCriterion> rows) {
	FindQuery q;
	q.scope = FindScope::Folder;
	q.folder = folder;
	q.criteria = rows;
	return q;
}

static bool matches(const QString &path, QList<FindCriterion> rows, const QDate &today = QDate::currentDate()) {
	return findMatches(in("/", rows), QFileInfo(path), today);
}

/* The newest Items Found window's file names, once it has finished. */
static QStringList results(int before) {
	FoundWindow *w = nullptr;
	until([&] {
		auto &all = FoundWindow::windows();
		w = int(all.size()) > before ? all.back().data() : nullptr;
		return w && !w->m_searching;
	});
	QStringList names;
	if (w) {
		for (auto &item : w->m_items) {
			names << item->name;
		}
	}
	names.sort();
	return names;
}

static FoundWindow *latest() {
	return FoundWindow::windows().empty() ? nullptr : FoundWindow::windows().back().data();
}

int main(int argc, char **argv) {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
	qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
	qputenv("XDG_RUNTIME_DIR", home.path().toUtf8());
	const QString d = home.path() + "/Stuff";
	QDir().mkpath(d + "/Empty folder");
	QDir().mkpath(d + "/Full folder");
	write(d + "/Full folder/inside.txt", "x");
	write(d + "/Budget notes.txt", "Budget for the harbour project: harbour, ferry, harbour.");
	write(d + "/big.bin", QByteArray(300 * 1024, 'x'));
	QImage(4, 4, QImage::Format_RGB32).save(d + "/photo.png");
	write(d + "/Hello clipping.txt", "hi");
	QFile::link(d + "/photo.png", d + "/photo alias");
	write(d + "/old.txt", "old");
	QFile old(d + "/old.txt");
	old.open(QIODevice::ReadWrite);
	old.setFileTime(QDateTime(QDate(2020, 5, 1), QTime(12, 0)), QFileDevice::FileModificationTime);
	old.close();
	write(d + "/locked.txt", "can't touch");
	chmod(QFile::encodeName(d + "/locked.txt").constData(), 0444);
	QApplication app(argc, argv);

	// ---- the criteria ----
	const QString budget = d + "/Budget notes.txt";
	check(matches(budget, { row(FindField::Name, FindOp::Contains, "NOTES") }) &&
		matches(budget, { row(FindField::Name, FindOp::StartsWith, "budget") }) &&
		matches(budget, { row(FindField::Name, FindOp::EndsWith, ".txt") }) &&
		matches(budget, { row(FindField::Name, FindOp::Is, "budget notes.txt") }) &&
		!matches(budget, { row(FindField::Name, FindOp::IsNot, "Budget notes.txt") }) &&
		!matches(budget, { row(FindField::Name, FindOp::DoesNotContain, "notes") }),
		"name: contains, starts with, ends with, is, is not, doesn't contain (any case)");
	check(matches(d + "/big.bin", { row(FindField::Size, FindOp::LargerThan, "200") }) &&
		!matches(d + "/big.bin", { row(FindField::Size, FindOp::SmallerThan, "200") }) &&
		matches(budget, { row(FindField::Size, FindOp::SmallerThan, "1") }) &&
		!matches(d + "/Empty folder", { row(FindField::Size, FindOp::SmallerThan, "1") }),
		"size: larger and smaller than so many K (folders have no size)");
	const QDate today = QDate::currentDate();
	check(matches(budget, { row(FindField::Modified, FindOp::Today) }, today) &&
		matches(budget, { row(FindField::Modified, FindOp::PastWeek) }, today) &&
		!matches(d + "/old.txt", { row(FindField::Modified, FindOp::PastMonth) }, today) &&
		matches(d + "/old.txt", { row(FindField::Modified, FindOp::Before, "2021-01-01") }, today) &&
		!matches(d + "/old.txt", { row(FindField::Modified, FindOp::After, "2021-01-01") }, today),
		"date modified: today, the past week and month, before and after a date");
	auto kind = [](const QString &name) { return int(findKindNames().indexOf(name)); };
	check(matches(d + "/Empty folder", { row(FindField::Kind, FindOp::Is, {}, kind("folder")) }) &&
		matches(d + "/photo.png", { row(FindField::Kind, FindOp::Is, {}, kind("picture")) }) &&
		matches(budget, { row(FindField::Kind, FindOp::Is, {}, kind("text document")) }) &&
		matches(d + "/Hello clipping.txt", { row(FindField::Kind, FindOp::Is, {}, kind("clipping")) }) &&
		matches(d + "/photo alias", { row(FindField::Kind, FindOp::Is, {}, kind("alias")) }) &&
		matches(budget, { row(FindField::Kind, FindOp::IsNot, {}, kind("picture")) }),
		"kind: folder, picture, text document, clipping, alias; is not");
	if (writeLabel(budget, 2)) {
		check(matches(budget, { row(FindField::Label, FindOp::Is, {}, 2) }) &&
			!matches(d + "/big.bin", { row(FindField::Label, FindOp::Is, {}, 2) }) &&
			matches(d + "/big.bin", { row(FindField::Label, FindOp::IsNot, {}, 2) }),
			"label: is and is not");
	} else {
		QTextStream(stdout) << "skip  label (no extended attributes here)\n";
	}
	if (getuid() != 0) {
		check(matches(d + "/locked.txt", { row(FindField::Locked, FindOp::IsLocked) }) &&
			matches(budget, { row(FindField::Locked, FindOp::IsUnlocked) }), "lock: locked and unlocked");
	}
	check(matches(d + "/Empty folder", { row(FindField::Folder, FindOp::IsEmpty) }) &&
		!matches(d + "/Full folder", { row(FindField::Folder, FindOp::IsEmpty) }) &&
		!matches(budget, { row(FindField::Folder, FindOp::IsEmpty) }),
		"folder: is empty (only folders)");
	check(matches(budget, { row(FindField::Name, FindOp::Contains, "notes"), row(FindField::Size, FindOp::SmallerThan, "5") }) &&
		!matches(budget, { row(FindField::Name, FindOp::Contains, "notes"), row(FindField::Size, FindOp::LargerThan, "5") }),
		"every row must match");
	QString why;
	check(!in(d, { row(FindField::Name, FindOp::Contains, "  ") }).ready(&why) && why.contains("Type"),
		"an empty name won't search");
	check(!in(d, { row(FindField::Modified, FindOp::Before, "yesterday") }).ready(&why) && why.contains("year-month-day"),
		"nor a date that isn't one");
	check(in(d, { row(FindField::Name, FindOp::Contains, "a"), row(FindField::Size, FindOp::LargerThan, "10") }).describe() ==
		"name contains “a” and size is larger than “10” K", "the query reads as a sentence");

	// ---- the index's query and answers ----
	const QString sparql = indexQuery({ "harbour", "say \"hi\"" }, "/home/a b", false, 50);
	check(sparql.contains("fts:match \"\\\"harbour\\\" \\\"say hi\\\"\"") &&
		sparql.contains("STRSTARTS(?url, \"file:///home/a%20b/\")") && sparql.contains("LIMIT 50"),
		"the SPARQL quotes each word and keeps to the folder");
	check(indexQuery({ "a", "b" }, "/", true, 5).contains("\\\"a\\\" OR \\\"b\\\"") &&
		!indexQuery({ "a" }, "/", false, 5).contains("FILTER"), "any word: OR; anywhere: no filter");
	check(indexParseResults("Results:\n  -1.5, file:///tmp/a%20b.txt\n  -0.5, file:///tmp/x,y.txt\n") ==
		QStringList({ "/tmp/a b.txt", "/tmp/x,y.txt" }), "tinysparql's answers become paths, in order");
	check(indexParseResults("Results:\n  None\n").isEmpty(), "and no answer, none");
	check(indexDistinctiveWords("The harbour, the harbour and the ferry. Harbour ferry boat with that.", 3) ==
		QStringList({ "harbour", "ferry", "boat" }), "distinctive words: frequent first, common words left out");

	// ---- searches ----
	const QString log = home.path() + "/tinysparql.log";
	const QString fake = home.path() + "/tinysparql";
	write(fake, QString("#!/bin/sh\necho \"$@\" >> '%1'\n[ -e '%2/down' ] && { echo 'not running' >&2; exit 1; }\n"
		"echo 'Results:'; echo '  -1, %3'; echo '  -2, %4'; echo '  -3, %5'\n")
		.arg(log, home.path(), QUrl::fromLocalFile(budget).toString(QUrl::FullyEncoded),
			QUrl::fromLocalFile(d + "/old.txt").toString(QUrl::FullyEncoded),
			QUrl::fromLocalFile(d + "/gone.txt").toString(QUrl::FullyEncoded)).toUtf8());
	QFile(fake).setPermissions(QFile::permissions(fake) | QFileDevice::ExeOwner);
	qputenv("ZACOS9_TINYSPARQL", fake.toUtf8());

	int before = int(FoundWindow::windows().size());
	FoundWindow::search(in(d, { row(FindField::Name, FindOp::EndsWith, ".txt"), row(FindField::Size, FindOp::SmallerThan, "1") }));
	check(results(before) == QStringList({ "Budget notes.txt", "Hello clipping.txt", "inside.txt", "locked.txt", "old.txt" }),
		"a search walks the folder and lists what matches every row");

	before = int(FoundWindow::windows().size());
	FoundWindow::search(in(d, { row(FindField::Contents, FindOp::Includes, "harbour"),
		row(FindField::Modified, FindOp::PastWeek) }));
	const QStringList byContents = results(before);
	check(byContents == QStringList({ "Budget notes.txt" }),
		"contents come from the index, and the other rows sift them (old.txt is too old, gone.txt gone): " +
			byContents.join(", ") + (latest() ? " " + latest()->m_problem : QString()));
	check(QFileInfo(log).size() > 0, "through tinysparql");
	QFile logFile(log);
	logFile.open(QIODevice::ReadOnly);
	const QString asked = QString::fromUtf8(logFile.readAll());
	check(asked.contains("--dbus-service org.freedesktop.LocalSearch3") && asked.contains("harbour") &&
		asked.contains(QUrl::fromLocalFile(d).toString(QUrl::FullyEncoded) + "/"),
		"asking localsearch for the words, in the folder");

	write(home.path() + "/down", "");
	before = int(FoundWindow::windows().size());
	FoundWindow::search(in(d, { row(FindField::Contents, FindOp::Includes, "harbour") }));
	results(before);
	check(latest() && latest()->m_items.empty() && latest()->m_problem.contains("localsearch"),
		"with the index not answering, the window says why");
	QFile::remove(home.path() + "/down");

	before = int(FoundWindow::windows().size());
	FoundWindow::similarTo(budget);
	const QStringList similar = results(before);
	check(similar == QStringList({ "old.txt" }),
		"Find Similar Items: the index's answers for the file's words, the file itself left out: " +
			similar.join(", ") + (latest() ? " " + latest()->m_problem : QString()));
	QFile again(log);
	again.open(QIODevice::ReadOnly);
	check(QString::fromUtf8(again.readAll()).contains("\\\"harbour\\\" OR"), "asked for its distinctive words, any of them");
	return fails ? 1 : 0;
}
