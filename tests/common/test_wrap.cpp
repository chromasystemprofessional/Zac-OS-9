/* Line wrapping: words break at spaces in English, at Unicode line-break
 * opportunities in Japanese, and never overflow the width. */
#include <QString>
#include <QStringList>
#include <cstdio>

#include "pixels.h"

static int fails;

static void check(bool ok, const char *what) {
	std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

static bool fits(const QStringList &lines, int width, pl_font font) {
	for (const QString &line : lines) {
		if (Text(line, 100000, font).inkWidth() > width) {
			return false;
		}
	}
	return true;
}

int main() {
	const QString english = "The newer copy of a file replaces the older one in both folders.";
	const QStringList words = wrapText(english, 100, PL_FONT_SYSTEM);
	check(words.size() > 1 && fits(words, 100, PL_FONT_SYSTEM), "English wraps at spaces");

	/* Japanese has no spaces: without Unicode line breaks this is one line. */
	const QString japanese = QString::fromUtf8(
		"日本語の文章には空白がないので、単語で折り返すことができません。");
	const QStringList japaneseLines = wrapText(japanese, 100, PL_FONT_VIEWS);
	bool noLeadingPunctuation = true;
	for (const QString &line : japaneseLines) {
		if (line.startsWith(QString::fromUtf8("。")) ||
			line.startsWith(QString::fromUtf8("、"))) {
			noLeadingPunctuation = false;
		}
	}
	check(japaneseLines.size() > 1 && fits(japaneseLines, 100, PL_FONT_VIEWS) &&
			noLeadingPunctuation,
		"Japanese wraps without spaces and keeps punctuation off the line start");

	/* A single run wider than the line (a URL) is cut, not left overflowing. */
	const QStringList url = wrapText("https://example.com/a/very/long/path/that/never/ends", 60,
		PL_FONT_VIEWS);
	check(!url.isEmpty() && fits(url, 60, PL_FONT_VIEWS), "an unbreakable run is cut to fit");

	/* A newline ends the line: it is not a break opportunity to wrap
	 * across. A newline left inside a line has no glyph, so it would draw
	 * as a missing-glyph box. */
	const QStringList hard = wrapText("first line\nsecond line", 1000, PL_FONT_SYSTEM);
	bool noNewlineInside = true;
	for (const QString &line : hard) {
		if (line.contains('\n')) {
			noNewlineInside = false;
		}
	}
	check(hard.size() == 2 && hard[0] == "first line" && hard[1] == "second line" &&
			noNewlineInside,
		"a newline ends the line");

	/* A blank line between paragraphs stays one empty row. */
	const QStringList paragraphs = wrapText("first paragraph\n\nsecond paragraph", 1000,
		PL_FONT_SYSTEM);
	check(paragraphs.size() == 3 && paragraphs[0] == "first paragraph" &&
			paragraphs[1].isEmpty() && paragraphs[2] == "second paragraph",
		"a blank line separates paragraphs");

	/* Blank lines at the ends do not make empty rows. */
	const QStringList edges = wrapText("\n\nfirst\n\n", 1000, PL_FONT_SYSTEM);
	check(edges.size() == 1 && edges[0] == "first",
		"leading and trailing blank lines are not rows");

	return fails ? 1 : 0;
}
