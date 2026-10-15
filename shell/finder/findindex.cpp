#include "findindex.h"

#include <QFile>
#include <QHash>
#include <QMimeDatabase>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <algorithm>

static QString program(const char *env, const char *fallback) {
	const QString p = qEnvironmentVariable(env);
	return p.isEmpty() ? QString(fallback) : p;
}

static QString service() {
	return program("ZACOS9_LOCALSEARCH_SERVICE", "org.freedesktop.LocalSearch3");
}

/* A SPARQL string literal. */
static QString literal(const QString &s) {
	QString out = s;
	out.replace('\\', "\\\\").replace('"', "\\\"").replace('\n', ' ');
	return '"' + out + '"';
}

QString indexQuery(const QStringList &words, const QString &root, bool any, int limit) {
	// Each word quoted for the full-text engine, so punctuation in it is
	// just text: "budget" "2026" (all) or "budget" OR "2026" (any).
	QStringList terms;
	for (const QString &w : words) {
		QString t = w;
		t.remove('"');
		if (!t.trimmed().isEmpty()) {
			terms << '"' + t.trimmed() + '"';
		}
	}
	QString filter;
	if (!root.isEmpty() && root != "/") {
		QString prefix = QUrl::fromLocalFile(root).toString(QUrl::FullyEncoded);
		if (!prefix.endsWith('/')) {
			prefix += '/';
		}
		filter = QString(" FILTER(STRSTARTS(?url, %1))").arg(literal(prefix));
	}
	return QString("SELECT fts:rank(?ie) ?url WHERE { ?ie fts:match %1 ; nie:isStoredAs ?f . "
		"?f nie:url ?url .%2 } ORDER BY DESC(fts:rank(?ie)) LIMIT %3")
		.arg(literal(terms.join(any ? " OR " : " ")), filter).arg(limit);
}

QStringList indexParseResults(const QString &output) {
	QStringList paths;
	bool results = false;
	for (const QString &raw : output.split('\n')) {
		if (raw.trimmed() == "Results:") {
			results = true;
			continue;
		}
		if (!results || !raw.startsWith("  ")) {
			continue;
		}
		// "  rank, file:///path": the URL is last and has no ", " of its own
		// before the scheme, so it starts at the first "file://".
		const int at = raw.indexOf("file://");
		if (at < 0) {
			continue;
		}
		const QUrl url(raw.mid(at).trimmed(), QUrl::StrictMode);
		if (url.isLocalFile() && !paths.contains(url.toLocalFile())) {
			paths << url.toLocalFile();
		}
	}
	return paths;
}

QStringList indexDistinctiveWords(const QString &text, int count) {
	static const QSet<QString> common = {
		"that", "this", "with", "from", "have", "were", "they", "their", "there", "which", "would",
		"about", "been", "into", "than", "then", "them", "these", "those", "what", "when", "where",
		"will", "your", "more", "some", "such", "only", "also", "other", "could", "should", "each",
		"just", "like", "over", "very", "does", "here", "much", "many", "most", "because", "while",
		"after", "before", "being", "both", "same", "make", "made", "well", "upon", "page",
	};
	QHash<QString, int> counts;
	static const QRegularExpression word("[\\p{L}][\\p{L}\\p{N}'-]{3,}");
	auto it = word.globalMatch(text.left(400000));
	while (it.hasNext()) {
		const QString w = it.next().captured(0).toLower();
		if (!common.contains(w)) {
			counts[w]++;
		}
	}
	QList<QPair<QString, int>> ranked;
	for (auto i = counts.cbegin(); i != counts.cend(); ++i) {
		ranked << qMakePair(i.key(), i.value());
	}
	std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
		return a.second != b.second ? a.second > b.second : a.first < b.first;
	});
	QStringList out;
	for (int i = 0; i < ranked.size() && i < count; ++i) {
		out << ranked[i].first;
	}
	return out;
}

/* Runs a program to the end, without blocking (CLAUDE.md: no waitForFinished). */
static void run(const QString &prog, const QStringList &args,
		const std::function<void(int code, const QString &out, const QString &err)> &done) {
	auto *p = new QProcess;
	QObject::connect(p, &QProcess::finished, [p, done](int code, QProcess::ExitStatus status) {
		const QString out = QString::fromUtf8(p->readAllStandardOutput());
		const QString err = QString::fromUtf8(p->readAllStandardError()).trimmed();
		p->deleteLater();
		done(status == QProcess::NormalExit ? code : -1, out, err);
	});
	QObject::connect(p, &QProcess::errorOccurred, [p, done, prog](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			p->deleteLater();
			done(-1, QString(), prog + " isn't installed.");
		}
	});
	p->start(prog, args);
}

static const QString NoIndex = "Searching contents needs the search index (localsearch), and it isn't answering.";

static void query(const QString &sparql, const IndexResults &done) {
	run(program("ZACOS9_TINYSPARQL", "tinysparql"), { "query", "--dbus-service", service(), sparql },
		[done](int code, const QString &out, const QString &err) {
			if (code != 0 || !out.contains("Results:")) {
				done({}, NoIndex + (err.isEmpty() ? QString() : " (" + err.section('\n', -1) + ")"));
				return;
			}
			done(indexParseResults(out), QString());
		});
}

void indexSearch(const QStringList &words, const QString &root, const IndexResults &done) {
	query(indexQuery(words, root, false, 5000), done);
}

void indexSimilar(const QString &path, const IndexResults &done) {
	auto withText = [path, done](const QString &text) {
		const QStringList words = indexDistinctiveWords(text, 8);
		if (words.size() < 2) {
			done({}, "There isn't enough text in it to find similar items by.");
			return;
		}
		query(indexQuery(words, "/", true, 200), [path, done](const QStringList &paths, const QString &error) {
			QStringList others = paths;
			others.removeAll(path);
			done(others, error);
		});
	};
	// Text files are read here; the index has the text of others (PDFs,
	// office documents) if it took them in.
	static const QMimeDatabase db;
	if (db.mimeTypeForFile(path).inherits("text/plain")) {
		QFile f(path);
		withText(f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.read(400000)) : QString());
		return;
	}
	const QString url = QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
	run(program("ZACOS9_TINYSPARQL", "tinysparql"), { "query", "--dbus-service", service(),
			QString("SELECT ?text WHERE { ?f nie:url %1 . ?ie nie:isStoredAs ?f ; nie:plainTextContent ?text } LIMIT 1")
				.arg(literal(url)) },
		[withText, done](int code, const QString &out, const QString &) {
			if (code != 0 || !out.contains("Results:")) {
				done({}, NoIndex);
				return;
			}
			withText(out.section("Results:", 1));
		});
}

void indexAddFolder(const QString &path, const std::function<void(const QString &error)> &done) {
	run(program("ZACOS9_LOCALSEARCH", "localsearch"), { "index", "--add", "--recursive", path },
		[done](int code, const QString &, const QString &err) {
			done(code == 0 ? QString()
				: "The search index couldn't take in the folder." + (err.isEmpty() ? QString() : " " + err.section('\n', -1)));
		});
}
