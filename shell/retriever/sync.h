#pragma once

/*
 * Retriever (Mac OS 9's File Synchronization, gap plan 5.8): folder pairs
 * kept alike. Each way in turn, rsync copies what is newer, so the newest
 * copy of each file wins; nothing is deleted on either side.
 */

#include <QList>
#include <QString>
#include <functional>

struct FolderPair {
	QString left, right;
};

/* ~/.config/zacos9/retriever.conf: LEFT<TAB>RIGHT lines. */
QList<FolderPair> loadPairs();
bool savePairs(const QList<FolderPair> &pairs);

/* Brings the two folders alike (rsync; $ZACOS9_RSYNC for tests); `done`
 * gets an empty string, or what went wrong. */
void syncPair(const FolderPair &pair, std::function<void(const QString &error)> done);
