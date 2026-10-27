/* The Help Center (shell/help): books and pages from a help folder, search
 * ranking, the made pages, the window's history; and ZacOS Help itself
 * (ZACOS9_HELP_SOURCE, the source tree's share/zacos9/help): every page
 * titled and described, every link going somewhere, every page reachable. */
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextStream>
#include <QUrlQuery>

#include "helpbook.h"
#include "helpwindow.h"

static int fails;
static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

static void put(const QString &path, const QString &text) {
	QDir().mkpath(QFileInfo(path).path());
	QFile f(path);
	f.open(QIODevice::WriteOnly);
	f.write(text.toUtf8());
}

static QString read(const QString &path) {
	QFile f(path);
	return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

int main(int argc, char **argv) {
	QApplication app(argc, argv);
	QTemporaryDir dir;
	const QString root = dir.path();
	put(root + "/zacos9/index.html", "<html><head><title>ZacOS Help</title>"
		"<meta name=\"description\" content=\"How to use ZacOS.\"><style>p { color: red }</style></head>"
		"<body><h1>ZacOS Help</h1><p>See <a href=\"print.html\">printing</a>.</p></body></html>");
	put(root + "/zacos9/print.html", "<html><head><title>Printing documents</title></head><body>"
		"<p>Choose File &gt; Print. Printers are found by themselves. Printing a printer's test page "
		"helps.</p></body></html>");
	put(root + "/zacos9/keys.html", "<html><head><title>Keys</title></head><body><h2>Sticky Keys</h2><p>Sticky Keys hold a key. "
		"Mouse keys move the pointer. Nothing about the other thing.</p>"
		"<table><tr><td>Shift five times</td><td>Sticky toggles</td></tr><tr><td>Clear</td><td>Mouse on</td></tr></table>"
		"</body></html>");
	put(root + "/aardvark/index.html", "<html><head><title>Aardvark Help</title></head><body>Ants.</body></html>");
	put(root + "/empty/readme.txt", "not a book");

	const QList<HelpBook> books = helpBooks(root);
	check(books.size() == 2 && books[0].id == "zacos9" && books[0].title == "ZacOS Help" &&
		books[0].summary == "How to use ZacOS." && books[1].title == "Aardvark Help",
		"books: folders with an index, ZacOS Help first, titles and descriptions from the index");
	check(htmlText(read(root + "/zacos9/index.html")).trimmed() == "ZacOS Help\nSee printing.",
		"a page's text: no head, no style, no tags");

	const QString lesson = "<p>All</p><p class=\"for-new\">New</p>"
		"<div class=\"for-kids\"><div>Kid <b>bone</b></div><p>Wag</p></div>"
		"<p class=\"tip for-new for-kids\">Both</p><img class=\"for-mac\" src=\"x.png\"><p>End</p>";
	check(tutorialHtml(lesson, "kids") == "<p>All</p><div class=\"for-kids\"><div>Kid <b>bone</b></div><p>Wag</p></div>"
		"<p class=\"tip for-new for-kids\">Both</p><p>End</p>",
		"a tutorial path keeps its own wording and the untagged, not the others'");
	check(tutorialHtml(lesson, "mac") == "<p>All</p><img class=\"for-mac\" src=\"x.png\"><p>End</p>",
		"a whole element goes, nested ones of its kind with it; an empty one keeps");
	check(tutorialHtml("<p>A</p><p class=\"for-kids\">open", "new") == "<p>A</p>open",
		"an element left open loses only its tag");

	const QList<HelpPage> pages = helpPages(root);
	check(pages.size() == 4, "every page of every book");
	QList<HelpHit> hits = searchHelp(pages, "print");
	check(hits.size() == 2 && hits[0].page->title == "Printing documents" && hits[1].page->title == "ZacOS Help",
		"a word in the title counts most; \"print\" finds \"printing\"");
	check(hits[0].excerpt == "Choose File > Print.", "the sentence where it was found");
	hits = searchHelp(pages, "sticky keys");
	check(hits.size() == 1 && hits[0].page->title == "Keys", "pages with every word, when there are some");
	check(searchHelp(pages, "pointer mouse").value(0).excerpt == "Mouse keys move the pointer.",
		"the sentence with the most of the words");
	check(searchHelp(pages, "sticky").value(0).excerpt == "Sticky Keys: Sticky Keys hold a key.",
		"a heading brings the sentence after it");
	check(searchHelp(pages, "toggles").value(0).excerpt == "Shift five times – Sticky toggles",
		"a table's row is one line, and doesn't take the next row");
	hits = searchHelp(pages, "mouse printers");
	check(hits.size() == 2, "else the pages with the most of them");
	check(searchHelp(pages, "zebra").isEmpty() && searchHelp(pages, "  ").isEmpty(), "nothing found, nothing asked");

	const QString center = helpCenterHtml(books);
	check(center.contains("<title>Help Center</title>") && center.contains("How to use ZacOS.") &&
		center.indexOf("ZacOS Help") < center.indexOf("Aardvark Help") &&
		center.contains(QUrl::fromLocalFile(root + "/aardvark/index.html").toString(QUrl::FullyEncoded)),
		"the Help Center lists the books with links");
	const QString results = searchResultsHtml("print <b>", searchHelp(pages, "print"));
	check(results.contains("2 topics found") && results.contains("print &lt;b&gt;") && results.contains("■■■■■"),
		"results: how many, the words escaped, relevance");
	check(searchResultsHtml("zebra", {}).contains("No topics found"), "and when there are none");

	qputenv("ZACOS9_HELP_DIR", root.toUtf8());
	HelpWindow w;
	w.showCenter();
	check(w.browser()->documentTitle() == "Help Center" && w.windowTitle() == "Help Center", "opens on the Help Center");
	w.follow(QUrl::fromLocalFile(root + "/zacos9/index.html"));
	check(w.browser()->documentTitle() == "ZacOS Help", "a book's link opens it");
	w.follow(QUrl("print.html"));
	check(w.browser()->documentTitle() == "Printing documents" && w.windowTitle() == "Printing documents",
		"a link within a book, relative to its page");
	w.search("sticky");
	check(w.browser()->documentTitle() == "Search Results" && w.browser()->toPlainText().contains("Keys") &&
		w.searchText() == "sticky", "Search shows the results");
	w.back();
	check(w.browser()->documentTitle() == "Printing documents", "Back");
	w.back();
	w.back();
	check(w.browser()->documentTitle() == "Help Center", "back to the start");
	w.forward();
	check(w.browser()->documentTitle() == "ZacOS Help", "Forward");
	check(w.showPage("aardvark") && w.browser()->documentTitle() == "Aardvark Help" &&
		w.showPage("zacos9/keys.html") && w.browser()->documentTitle() == "Keys" && !w.showPage("nonsense"),
		"a book by name, a page by its path in the help folder");

	put(root + "/tutorial/one.html", "<html><head><title>One</title></head><body>"
		"<p class=\"for-new\">Click the box.</p><p class=\"for-kids\">Click the little square!</p>"
		"<p><a href=\"two.html\">Next</a></p></body></html>");
	put(root + "/tutorial/two.html", "<html><head><title>Two</title></head><body>"
		"<p class=\"for-new\">Drag it.</p><p class=\"for-kids\">Drag the ball!</p></body></html>");
	w.follow(QUrl::fromLocalFile(root + "/tutorial/one.html"));
	check(w.browser()->toPlainText().contains("Click the box.") && !w.browser()->toPlainText().contains("square"),
		"a tutorial page with no path chosen: the newcomers' wording");
	w.follow(QUrl("one.html?for=kids"));
	check(w.browser()->toPlainText().contains("little square") && !w.browser()->toPlainText().contains("the box."),
		"?for=kids: the children's wording");
	w.follow(QUrl("two.html"));
	check(w.browser()->toPlainText().contains("Drag the ball!") && !w.browser()->toPlainText().contains("Drag it.") &&
		QUrlQuery(w.browser()->source()).queryItemValue("for") == "kids", "a link keeps the path");

	// The books as shipped: ZacOS Help and Learn ZacOS.
	const QString source = qEnvironmentVariable("ZACOS9_HELP_SOURCE");
	QStringList problems;
	const QRegularExpression href("href=\"([^\"]+)\"");
	for (const QString &book : { QStringLiteral("zacos9"), QStringLiteral("tutorial") }) {
		const QString bookDir = source + "/" + book;
		const QStringList files = QDir(bookDir).entryList({ "*.html" }, QDir::Files);
		check(files.size() >= (book == "zacos9" ? 10 : 5), QString("%1 has its pages (%2)").arg(book).arg(files.size()));
		QSet<QString> linked;
		for (const QString &name : files) {
			const QString html = read(bookDir + "/" + name);
			if (htmlTitle(html).isEmpty()) {
				problems << book + "/" + name + ": no title";
			}
			if (htmlMeta(html, "description").isEmpty()) {
				problems << book + "/" + name + ": no description";
			}
			if (name != "index.html" && !html.contains("href=\"index.html\"")) {
				problems << book + "/" + name + ": no way back to the index";
			}
			for (auto it = href.globalMatch(html); it.hasNext();) {
				const QString target = it.next().captured(1);
				if (target.contains(':')) {
					continue; /* the web */
				}
				const QString file = target.section('#', 0, 0).section('?', 0, 0);
				if (!QFile::exists(bookDir + "/" + file)) {
					problems << book + "/" + name + ": link to missing " + target;
				}
				linked << file;
			}
		}
		for (const QString &name : files) {
			if (name != "index.html" && !linked.contains(name)) {
				problems << book + "/" + name + ": nothing links to it";
			}
		}
	}
	check(problems.isEmpty(), "every page titled, described and linked; every link goes somewhere" +
		(problems.isEmpty() ? QString() : ": " + problems.join("; ")));
	const QList<HelpPage> shipped = helpPages(source);
	const auto top = [&](const QString &words) {
		const QList<HelpHit> h = searchHelp(shipped, words);
		return h.isEmpty() ? QString() : QFileInfo(h.first().page->path).fileName();
	};
	check(top("sticky keys") == "accessibility.html" && top("old dog rom") == "old-dog.html" &&
		(top("force quit") == "troubleshooting.html" || top("force quit") == "programs.html"),
		"searching ZacOS Help finds the right pages");
	return fails ? 1 : 0;
}
