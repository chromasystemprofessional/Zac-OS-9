#pragma once

#include <QHash>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <functional>
#include <memory>

class QFileSystemWatcher;
class QTimer;

/*
 * Folder Tricks, ZacOS 9's Folder Actions (Missing Manual ch. 10): a
 * program from the Folder Tricks folder (System Folder) attached to a
 * folder runs whenever items arrive in it, as
 *   TRICK FOLDER ITEM...
 * with the folder as its working directory. Arrivals are gathered until
 * the folder has been quiet for a moment, so a copy in progress is one
 * run; hidden files and partial downloads wait until they're renamed.
 * Which tricks are attached to which folders is kept in
 * ~/.config/zacos9/folder-tricks.json.
 */
class FolderTricks {
public:
	FolderTricks(const QString &config = defaultConfig(), const QString &tricks = defaultTricksDir());
	~FolderTricks();
	static FolderTricks &instance();
	static QString defaultConfig();
	static QString defaultTricksDir();

	QString tricksDir() const { return m_tricksDir; }
	/* The programs in the Folder Tricks folder, by name. */
	QStringList available() const;
	/* The tricks attached to the folder. */
	QStringList attached(const QString &folder) const;
	bool attach(const QString &folder, const QString &trick);
	bool remove(const QString &folder, const QString &trick);

	/* How long a folder must be quiet before its tricks run. */
	int settleMs = 1000;
	/* Told of each run (tests), after it starts. */
	std::function<void(const QString &trick, const QString &folder, const QStringList &items)> ran;

private:
	void load();
	bool save() const;
	void watch(const QString &folder);
	void changed(const QString &folder);
	void settle(const QString &folder);
	QSet<QString> entries(const QString &folder) const;

	QString m_config, m_tricksDir;
	QMap<QString, QStringList> m_attached; /* canonical folder -> tricks */
	QHash<QString, QSet<QString>> m_seen;  /* what each folder held */
	QHash<QString, QTimer *> m_timers;
	std::unique_ptr<QFileSystemWatcher> m_watcher;
};
