/* Bloodhound's Files search (shell/bloodhound/search.cpp): a name query
 * walks a folder, a contents query asks the index first, and both report
 * the matching files in batches. Also the snippet made for a contents
 * result (shell/files/findindex.cpp). */
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <cstdio>

#include "findcriteria.h"
#include "findindex.h"
#include "search.h"

static bool writeFile(const QString &path, const QByteArray &data = "test\n") {
	QFile f(path);
	return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

static bool runSearch(BloodhoundSearch &search, const FindQuery &query, QStringList *found,
		QString *problem) {
	found->clear();
	*problem = "the search didn't finish";
	QEventLoop loop;
	QObject::connect(&search, &BloodhoundSearch::batch, &loop, [found](const QStringList &paths) {
		*found += paths;
	});
	QObject::connect(&search, &BloodhoundSearch::finished, &loop, [&](const QString &why) {
		*problem = why;
		loop.quit();
	});
	QTimer::singleShot(10000, &loop, &QEventLoop::quit);
	search.start(query);
	loop.exec();
	return problem->isEmpty();
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	QTemporaryDir dir;
	if (!dir.isValid()) {
		fprintf(stderr, "could not make a temporary folder\n");
		return 1;
	}
	QDir().mkpath(dir.filePath("sub"));
	if (!writeFile(dir.filePath("report.txt"), "the harbour report\n") ||
			!writeFile(dir.filePath("notes.txt"), "notes\n") ||
			!writeFile(dir.filePath("sub/report-two.txt"), "the harbour again\n") ||
			!writeFile(dir.filePath("sub/other.txt"), "other words\n")) {
		fprintf(stderr, "could not write test files\n");
		return 1;
	}

	FindCriterion name;
	name.field = FindField::Name;
	name.op = FindOp::Contains;
	name.text = "report";

	FindQuery byName;
	byName.scope = FindScope::Folder;
	byName.folder = dir.path();
	byName.criteria << name;

	BloodhoundSearch search;
	QStringList found;
	QString problem;
	if (!runSearch(search, byName, &found, &problem)) {
		fprintf(stderr, "search problem: %s\n", qPrintable(problem));
		return 1;
	}
	found.sort();
	QStringList expected = {
		QFileInfo(dir.filePath("report.txt")).absoluteFilePath(),
		QFileInfo(dir.filePath("sub/report-two.txt")).absoluteFilePath(),
	};
	expected.sort();
	if (found != expected) {
		fprintf(stderr, "found %s\nexpected %s\n", qPrintable(found.join(", ")),
			qPrintable(expected.join(", ")));
		return 1;
	}
	if (search.running()) {
		fprintf(stderr, "the search still says it is running\n");
		return 1;
	}
	printf("ok: a name search finds the matching files\n");

	/* The fake index answers for three files; the name row then keeps only
	 * the report. */
	const QString log = dir.filePath("tinysparql.log");
	const QString fake = dir.filePath("tinysparql");
	QString script = "#!/bin/sh\necho \"$@\" >> " + log + "\n"
		"[ -e '" + dir.filePath("down") + "' ] && { echo 'not running' >&2; exit 1; }\n"
		"echo 'Results:'\n"
		"echo '  -1, " + QUrl::fromLocalFile(dir.filePath("report.txt")).toString(QUrl::FullyEncoded) + "'\n"
		"echo '  -2, " + QUrl::fromLocalFile(dir.filePath("notes.txt")).toString(QUrl::FullyEncoded) + "'\n"
		"echo '  -3, " + QUrl::fromLocalFile(dir.filePath("sub/report-two.txt")).toString(QUrl::FullyEncoded) + "'\n";
	if (!writeFile(fake, script.toUtf8())) {
		fprintf(stderr, "could not write the fake tinysparql\n");
		return 1;
	}
	QFile::setPermissions(fake, QFile::permissions(fake) | QFileDevice::ExeOwner);
	qputenv("ZACOS9_TINYSPARQL", fake.toUtf8());

	FindCriterion contents;
	contents.field = FindField::Contents;
	contents.op = FindOp::Includes;
	contents.text = "harbour";

	FindQuery byContents;
	byContents.scope = FindScope::Folder;
	byContents.folder = dir.path();
	byContents.criteria << contents << name;
	if (!runSearch(search, byContents, &found, &problem)) {
		fprintf(stderr, "contents search problem: %s\n", qPrintable(problem));
		return 1;
	}
	found.sort();
	if (found != expected) {
		fprintf(stderr, "contents found %s\nexpected %s\n", qPrintable(found.join(", ")),
			qPrintable(expected.join(", ")));
		return 1;
	}
	QFile asked(log);
	asked.open(QIODevice::ReadOnly);
	const QString askedText = QString::fromUtf8(asked.readAll());
	if (!askedText.contains("harbour") || !askedText.contains("--dbus-service")) {
		fprintf(stderr, "the index wasn't asked for the contents\n");
		return 1;
	}
	printf("ok: a contents search asks the index, then sifts its answers by name\n");

	/* With the index down, the window has an honest error to show. */
	writeFile(dir.filePath("down"));
	FindQuery onlyContents;
	onlyContents.scope = FindScope::Folder;
	onlyContents.folder = dir.path();
	onlyContents.criteria << contents;
	runSearch(search, onlyContents, &found, &problem);
	if (problem.isEmpty() || !problem.contains("localsearch")) {
		fprintf(stderr, "the index error was lost: %s\n", qPrintable(problem));
		return 1;
	}
	QFile::remove(dir.filePath("down"));
	printf("ok: an unavailable index is reported\n");

	const IndexSnippet snippet = indexSnippetText(
		"The harbour at dawn was quiet, and the ferry waited.", { "harbour", "ferry" });
	if (snippet.text.isEmpty() || !snippet.text.contains("harbour") ||
			snippet.words != QStringList({ "harbour", "ferry" })) {
		fprintf(stderr, "the snippet lost its match: %s\n", qPrintable(snippet.text));
		return 1;
	}
	if (!indexSnippetText("nothing here", { "harbour" }).text.isEmpty()) {
		fprintf(stderr, "a missing word still made a snippet\n");
		return 1;
	}
	const IndexSnippet longText = indexSnippetText(
		QString(100, 'x') + " harbour " + QString(100, 'y'), { "harbour" });
	if (!longText.text.startsWith(QChar(0x2026)) || !longText.text.endsWith(QChar(0x2026)) ||
			!longText.text.contains("harbour")) {
		fprintf(stderr, "a long snippet wasn't shortened: %s\n", qPrintable(longText.text));
		return 1;
	}
	printf("ok: a contents result gets a snippet around the matched words\n");

	if (search.running()) {
		fprintf(stderr, "the search still says it is running at the end\n");
		return 1;
	}
	return 0;
}
