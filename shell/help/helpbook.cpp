#include "helpbook.h"
#include "programs.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QUrl>
#include <algorithm>

namespace {

QString readFile(const QString &path) {
	QFile f(path);
	return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.read(4 * 1024 * 1024)) : QString();
}

QStringList words(const QString &query) {
	QStringList out;
	for (const QString &w : query.toLower().split(QRegularExpression("[^\\w']+"), Qt::SkipEmptyParts)) {
		if (!out.contains(w)) {
			out << w;
		}
	}
	return out;
}

/* How often a word starts a word in the text ("print" finds "printing"). */
int occurrences(const QString &lower, const QString &word) {
	int count = 0;
	for (int at = lower.indexOf(word); at >= 0; at = lower.indexOf(word, at + word.size())) {
		if (at == 0 || !lower[at - 1].isLetterOrNumber()) {
			++count;
		}
	}
	return count;
}

/* The sentence with the most of the words (the first, of equals); a
 * heading (no full stop) takes the sentence after it, if one follows.
 * Shortened. */
QString excerpt(const QString &text, const QStringList &ws) {
	QStringList sentences;
	QString cur;
	for (const QChar ch : text) {
		if (ch == '\n') {
			if (!cur.trimmed().isEmpty()) {
				sentences << cur.simplified();
			}
			cur.clear();
			continue;
		}
		cur += ch;
		if (ch == '.' || ch == '!' || ch == '?') {
			sentences << cur.simplified();
			cur.clear();
		}
	}
	if (!cur.trimmed().isEmpty()) {
		sentences << cur.simplified();
	}
	int best = -1, bestCount = 0;
	for (int i = 0; i < sentences.size(); ++i) {
		const QString lower = sentences[i].toLower();
		int count = 0;
		for (const QString &w : ws) {
			count += occurrences(lower, w) > 0;
		}
		if (count > bestCount) {
			best = i;
			bestCount = count;
		}
	}
	if (best < 0) {
		return text.left(160).simplified();
	}
	QString s = sentences[best];
	const auto sentence = [](const QString &t) { return t.endsWith('.') || t.endsWith('!') || t.endsWith('?'); };
	if (!sentence(s) && best + 1 < sentences.size() && sentence(sentences[best + 1])) {
		s += ": " + sentences[best + 1];
	}
	return s.size() > 220 ? s.left(219) + "…" : s;
}

QString escaped(const QString &s) {
	return s.toHtmlEscaped();
}

const char *const STYLE =
	"<style>body { font-family: sans-serif; } h1 { font-size: x-large; } "
	".summary { color: #444444; } .excerpt { color: #444444; }</style>";

} // namespace

QString helpRoot() {
	const QString env = qEnvironmentVariable("ZACOS9_HELP_DIR");
	if (!env.isEmpty()) {
		return env;
	}
	// From a build tree, the source tree's share/zacos9/help; installed,
	// bin/ -> share/zacos9/help.
	const QString dir = QCoreApplication::applicationDirPath();
	for (const QString &candidate : { sourceTreePath("share/zacos9/help"), dir + "/../share/zacos9/help" }) {
		if (!candidate.isEmpty() && QFileInfo(candidate + "/zacos9/index.html").exists()) {
			return QFileInfo(candidate).canonicalFilePath();
		}
	}
	return "/usr/share/zacos9/help";
}

QString htmlTitle(const QString &html) {
	const QRegularExpressionMatch m =
		QRegularExpression("<title>(.*?)</title>", QRegularExpression::CaseInsensitiveOption |
			QRegularExpression::DotMatchesEverythingOption).match(html);
	return m.hasMatch() ? QTextDocumentFragment::fromHtml(m.captured(1)).toPlainText().simplified() : QString();
}

QString htmlMeta(const QString &html, const QString &name) {
	const QRegularExpressionMatch m = QRegularExpression(
		"<meta\\s+name=\"" + QRegularExpression::escape(name) + "\"\\s+content=\"([^\"]*)\"",
		QRegularExpression::CaseInsensitiveOption).match(html);
	return m.hasMatch() ? QTextDocumentFragment::fromHtml(m.captured(1)).toPlainText() : QString();
}

QString htmlText(const QString &html) {
	QString body = html;
	body.remove(QRegularExpression("<(head|style|script)\\b.*?</\\1>", QRegularExpression::CaseInsensitiveOption |
		QRegularExpression::DotMatchesEverythingOption));
	// A table's row on one line: "⌘-N – New Folder".
	body.replace(QRegularExpression("</td>\\s*<td[^>]*>", QRegularExpression::CaseInsensitiveOption), " – ");
	return QTextDocumentFragment::fromHtml(body).toPlainText();
}

QString tutorialHtml(const QString &html, const QString &path) {
	static const QRegularExpression open("<([a-z][a-z0-9]*)\\b[^>]*\\bclass=\"([^\"]*)\"[^>]*>",
		QRegularExpression::CaseInsensitiveOption);
	static const QStringList empty{ "br", "hr", "img", "input" };
	QString out = html;
	int at = 0;
	for (QRegularExpressionMatch m = open.match(out); m.hasMatch(); m = open.match(out, at)) {
		bool tagged = false, shown = false;
		for (const QString &c : m.captured(2).split(' ', Qt::SkipEmptyParts)) {
			if (c.startsWith("for-")) {
				tagged = true;
				shown |= c == "for-" + path;
			}
		}
		if (!tagged || shown) {
			at = m.capturedEnd();
			continue;
		}
		// Out to its own closing tag, past any of the same kind inside it;
		// an element left open loses only its tag.
		const QString tag = m.captured(1).toLower();
		int end = m.capturedEnd();
		if (!empty.contains(tag) && !m.captured(0).endsWith("/>")) {
			const QRegularExpression either("<(/?)" + tag + "\\b[^>]*>", QRegularExpression::CaseInsensitiveOption);
			int depth = 1;
			for (auto it = either.globalMatch(out, end); it.hasNext() && depth > 0;) {
				const QRegularExpressionMatch t = it.next();
				depth += t.captured(1).isEmpty() ? 1 : -1;
				if (depth == 0) {
					end = t.capturedEnd();
				}
			}
		}
		out.remove(m.capturedStart(), end - m.capturedStart());
		at = m.capturedStart();
	}
	return out;
}

QList<HelpBook> helpBooks(const QString &root) {
	QList<HelpBook> out;
	for (const QString &id : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
		const QString index = root + "/" + id + "/index.html";
		const QString html = readFile(index);
		if (html.isEmpty()) {
			continue;
		}
		out << HelpBook{ id, htmlTitle(html).isEmpty() ? id : htmlTitle(html), index, htmlMeta(html, "description") };
	}
	// ZacOS Help first, the others by title.
	std::stable_sort(out.begin(), out.end(), [](const HelpBook &a, const HelpBook &b) {
		if ((a.id == "zacos9") != (b.id == "zacos9")) {
			return a.id == "zacos9";
		}
		return a.title.localeAwareCompare(b.title) < 0;
	});
	return out;
}

QList<HelpPage> helpPages(const QString &root) {
	QList<HelpPage> out;
	for (const HelpBook &book : helpBooks(root)) {
		const QDir dir(root + "/" + book.id);
		for (const QString &name : dir.entryList({ "*.html" }, QDir::Files, QDir::Name)) {
			const QString html = readFile(dir.filePath(name));
			out << HelpPage{ QFileInfo(dir.filePath(name)).absoluteFilePath(), book.id, htmlTitle(html), htmlText(html) };
		}
	}
	return out;
}

QList<HelpHit> searchHelp(const QList<HelpPage> &pages, const QString &query) {
	const QStringList ws = words(query);
	if (ws.isEmpty()) {
		return {};
	}
	struct Scored {
		HelpHit hit;
		int found;
	};
	QList<Scored> all;
	for (const HelpPage &page : pages) {
		const QString title = page.title.toLower(), text = page.text.toLower();
		int score = 0, found = 0;
		for (const QString &w : ws) {
			const int inTitle = occurrences(title, w), inText = occurrences(text, w);
			if (inTitle + inText > 0) {
				++found;
				score += 10 * inTitle + std::min(inText, 20);
			}
		}
		if (found > 0) {
			all << Scored{ HelpHit{ &page, score, excerpt(page.text, ws) }, found };
		}
	}
	// Pages with every word, if there are any; else those with the most.
	int best = 0;
	for (const Scored &s : all) {
		best = std::max(best, s.found);
	}
	QList<HelpHit> out;
	for (const Scored &s : all) {
		if (s.found == best) {
			out << s.hit;
		}
	}
	std::stable_sort(out.begin(), out.end(), [](const HelpHit &a, const HelpHit &b) { return a.score > b.score; });
	return out;
}

QString helpCenterHtml(const QList<HelpBook> &books) {
	QString html = QString("<html><head><title>Help Center</title>") + STYLE + "</head><body>"
		"<h1>Help Center</h1><p>Choose a book, or type words in the search field above and click Search.</p>";
	if (books.isEmpty()) {
		html += "<p>No help is installed.</p>";
	}
	for (const HelpBook &b : books) {
		html += "<p><a href=\"" + QUrl::fromLocalFile(b.index).toString(QUrl::FullyEncoded) + "\"><b>" +
			escaped(b.title) + "</b></a>";
		if (!b.summary.isEmpty()) {
			html += "<br><span class=\"summary\">" + escaped(b.summary) + "</span>";
		}
		html += "</p>";
	}
	return html + "</body></html>";
}

QString searchResultsHtml(const QString &query, const QList<HelpHit> &hits) {
	QString html = QString("<html><head><title>Search Results</title>") + STYLE + "</head><body>";
	html += hits.isEmpty()
		? QString("<h1>No topics found</h1><p>Nothing in the help mentions “%1”. Try fewer or other words.</p>")
			.arg(escaped(query))
		: QString("<h1>%1 topic%2 found</h1><p>For “%3”, the best match first.</p>")
			.arg(hits.size()).arg(hits.size() == 1 ? "" : "s").arg(escaped(query));
	const int top = hits.isEmpty() ? 1 : std::max(1, hits.first().score);
	for (const HelpHit &h : hits) {
		// Relevance as five blocks, as Mac Help showed a bar.
		const int filled = std::clamp((h.score * 5 + top - 1) / top, 1, 5);
		html += "<p><a href=\"" + QUrl::fromLocalFile(h.page->path).toString(QUrl::FullyEncoded) + "\"><b>" +
			escaped(h.page->title) + "</b></a> <span class=\"summary\">" + QString("■").repeated(filled) +
			QString("□").repeated(5 - filled) + "</span><br><span class=\"excerpt\">" + escaped(h.excerpt) +
			"</span></p>";
	}
	return html + "</body></html>";
}
