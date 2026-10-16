#pragma once

#include <QImage>
#include <QString>
#include <QVector>

class QMimeData;

/* One page of the Stash: some text or a picture. */
struct Clip {
	enum Kind { Text, Picture };
	Kind kind = Text;
	QString text;
	QImage image;
	QString file;   /* its file's name in the Stash folder */
	qint64 bytes = 0;
};

/*
 * The Stash's pages, kept in a folder (by default
 * ~/.local/share/zacos9/Stash): one file per clip, .txt or .png, and an
 * index listing them in order. Every change is written at once.
 */
class Stash {
public:
	explicit Stash(const QString &dir = defaultDir());
	static QString defaultDir();

	int count() const { return m_clips.size(); }
	const Clip &clip(int index) const { return m_clips.at(index); }

	/* Add a clip so it becomes page `at` (clamped to the ends). */
	bool insert(int at, Clip clip);
	bool insertText(int at, const QString &text);
	bool insertImage(int at, const QImage &image);
	/* Add what was pasted or dropped from `data` at `at`: picture and
	 * text files, else a picture, else text. How many clips were added. */
	int insertMime(int at, const QMimeData *data);
	bool remove(int index);

	/* The clip as clipboard or drag data; the caller owns it. */
	QMimeData *mimeData(int index) const;

private:
	bool saveIndex() const;

	QString m_dir;
	QVector<Clip> m_clips;
};
