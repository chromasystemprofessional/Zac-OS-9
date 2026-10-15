#pragma once

/*
 * The search index behind Find's "contents include" and Find Similar
 * Items (Mac OS 9's Sherlock index): localsearch, GNOME's file indexer,
 * asked through tinysparql's full-text search. It indexes the folders in
 * its settings (Desktop, Documents, Music, Pictures, Videos at first);
 * File > Index Selection adds more.
 *
 * For tests: $ZACOS9_TINYSPARQL and $ZACOS9_LOCALSEARCH replace the
 * programs, $ZACOS9_LOCALSEARCH_SERVICE the D-Bus name.
 */

#include <QString>
#include <QStringList>
#include <functional>

using IndexResults = std::function<void(const QStringList &paths, const QString &error)>;

/* Files whose contents have every word, best matches first, under `root`
 * ("/" for anywhere). */
void indexSearch(const QStringList &words, const QString &root, const IndexResults &done);
/* Files like `path`: sharing its most distinctive words, best first. */
void indexSimilar(const QString &path, const IndexResults &done);
/* Have the indexer take in a folder and everything in it. */
void indexAddFolder(const QString &path, const std::function<void(const QString &error)> &done);

/* ---- the pieces, for tests ---- */
/* The SPARQL for a full-text search: one of the words (any) or all. */
QString indexQuery(const QStringList &words, const QString &root, bool any, int limit);
/* tinysparql's "Results:" lines (rank, URL) as local paths, in order. */
QStringList indexParseResults(const QString &output);
/* The words that say most about a text: frequent, not common, at least
 * four letters, most frequent first. */
QStringList indexDistinctiveWords(const QString &text, int count);
