/* Retriever's two-way copy (sync.cpp): newest wins, nothing deleted. */
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

#include "sync.h"

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

static void write(const QString &path, const QByteArray &text, int ageSeconds) {
	QFile f(path);
	f.open(QIODevice::WriteOnly);
	f.write(text);
	f.flush(); /* before the time: writing would set it again */
	f.setFileTime(QDateTime::currentDateTime().addSecs(-ageSeconds), QFileDevice::FileModificationTime);
}

static QByteArray read(const QString &path) {
	QFile f(path);
	return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("(missing)");
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	QTemporaryDir dir;
	qputenv("XDG_CONFIG_HOME", dir.filePath("config").toUtf8());
	const QString a = dir.filePath("Laptop"), b = dir.filePath("Backup");
	QDir().mkpath(a + "/Letters");
	QDir().mkpath(b);
	write(a + "/Letters/old.txt", "only here", 100);
	write(a + "/both.txt", "older", 500);
	write(b + "/both.txt", "newer", 10);
	write(b + "/backup-only.txt", "kept", 50);

	QString error = "unset";
	QEventLoop loop;
	syncPair({ a, b }, [&](const QString &e) { error = e; loop.quit(); });
	loop.exec();
	CHECK(error.isEmpty());
	CHECK(read(b + "/Letters/old.txt") == "only here");
	CHECK(read(a + "/both.txt") == "newer");   /* the newest copy wins, both ways */
	CHECK(read(b + "/both.txt") == "newer");
	CHECK(read(a + "/backup-only.txt") == "kept");

	syncPair({ a, dir.filePath("Gone") }, [&](const QString &e) { error = e; }); /* at once */
	CHECK(error.contains("can't be found"));

	CHECK(savePairs({ { a, b } }));
	const QList<FolderPair> pairs = loadPairs();
	CHECK(pairs.size() == 1 && pairs[0].left == a && pairs[0].right == b);
	std::puts("ok: Retriever keeps folder pairs alike, newest wins");
	return 0;
}
