/* Bloodhound's Files search (shell/bloodhound/search.cpp): a name query
 * walks a folder and reports the matching files in batches. */
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

#include "findcriteria.h"
#include "search.h"

static bool writeFile(const QString &path) {
	QFile f(path);
	return f.open(QIODevice::WriteOnly) && f.write("test\n") == 5;
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	QTemporaryDir dir;
	if (!dir.isValid()) {
		fprintf(stderr, "could not make a temporary folder\n");
		return 1;
	}
	QDir().mkpath(dir.filePath("sub"));
	if (!writeFile(dir.filePath("report.txt")) ||
			!writeFile(dir.filePath("notes.txt")) ||
			!writeFile(dir.filePath("sub/report-two.txt"))) {
		fprintf(stderr, "could not write test files\n");
		return 1;
	}

	FindQuery query;
	query.scope = FindScope::Folder;
	query.folder = dir.path();
	FindCriterion criterion;
	criterion.field = FindField::Name;
	criterion.op = FindOp::Contains;
	criterion.text = "report";
	query.criteria << criterion;

	BloodhoundSearch search;
	QStringList found;
	QString problem = "the search didn't finish";
	QEventLoop loop;
	QObject::connect(&search, &BloodhoundSearch::batch, [&found](const QStringList &paths) {
		found += paths;
	});
	QObject::connect(&search, &BloodhoundSearch::finished, [&](const QString &why) {
		problem = why;
		loop.quit();
	});
	QTimer::singleShot(10000, &loop, &QEventLoop::quit);
	search.start(query);
	loop.exec();

	if (!problem.isEmpty()) {
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
	return 0;
}
