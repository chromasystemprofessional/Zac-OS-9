#pragma once

/*
 * The Help Center's books (Mac OS 9's Help Viewer, Missing Manual
 * Appendix A): HTML pages in share/zacos9/help, a folder per book whose
 * index.html names it in its <title>. ZACOS9_HELP_DIR points at another
 * help folder (tests). Search ranks every page of every book by the words
 * asked for.
 */

#include <QList>
#include <QString>
#include <QStringList>

struct HelpPage {
	QString path;  /* absolute */
	QString book;  /* the book's folder name, e.g. "zacos9" */
	QString title; /* <title> */
	QString text;  /* the words, tags and entities taken out */
};

struct HelpBook {
	QString id, title, index; /* folder name, its index's title, index.html */
	QString summary;          /* <meta name="description"> */
};

struct HelpHit {
	const HelpPage *page;
	int score;          /* higher is better */
	QString excerpt;    /* a sentence around the first word found */
};

/* The help folder: ZACOS9_HELP_DIR, installed beside us, or the source tree's. */
QString helpRoot();
QList<HelpBook> helpBooks(const QString &root);
QList<HelpPage> helpPages(const QString &root);

/* An HTML page's title, its readable text, a meta description. */
QString htmlTitle(const QString &html);
QString htmlText(const QString &html);
QString htmlMeta(const QString &html, const QString &name);

/* A page as one of the tutorial's paths sees it ("new", "kids", "mac"): an
 * element whose class has for-… words shows only on those paths, so
 * <p class="for-kids"> is left out for "new"; untagged ones show on all.
 * See docs/tutorial.md. */
QString tutorialHtml(const QString &html, const QString &path);

/* Pages with every word of the query (or, failing that, any), best first:
 * words in the title count most. */
QList<HelpHit> searchHelp(const QList<HelpPage> &pages, const QString &query);

/* The Help Center's front page, and a page of search results, as HTML. */
QString helpCenterHtml(const QList<HelpBook> &books);
QString searchResultsHtml(const QString &query, const QList<HelpHit> &hits);
