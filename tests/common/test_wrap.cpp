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

	return fails ? 1 : 0;
}
