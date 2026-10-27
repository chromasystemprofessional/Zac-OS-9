#pragma once

/*
 * The desktop's own settings files: key=value lines (sharing.conf,
 * screensharing.conf, Walkies' places...). C reads desktop.conf through
 * lib/settings.h; this is the same idea for the Qt programs.
 *
 * Keys and values are trimmed of spaces; the first line of a key is the
 * one that counts. Lines that aren't key=value, a [section] or a comment,
 * are kept as they are when the file is written again; blank lines aren't.
 */

#include <QByteArray>
#include <QString>
#include <QStringList>

class KeyValueFile {
public:
	KeyValueFile() = default;
	/* The file at `path`, or an empty one (exists() false) if it can't be read. */
	static KeyValueFile read(const QString &path);
	static KeyValueFile parse(const QByteArray &text);

	bool exists() const { return m_exists; }
	bool contains(const QString &key) const;
	QString value(const QString &key, const QString &fallback = QString()) const;
	/* value() == "yes": how these files write a switch. */
	bool yes(const QString &key) const { return value(key) == QLatin1String("yes"); }

	/* Replaces the key's first line (dropping any later ones), else adds a line at the end. */
	void set(const QString &key, const QString &value);
	void setYes(const QString &key, bool on) { set(key, on ? "yes" : "no"); }
	void remove(const QString &key);

	QByteArray text() const;
	/* Writes it all at once (QSaveFile): a reader never sees half of it. */
	bool save(const QString &path, QString *error = nullptr) const;

private:
	int find(const QString &key) const;
	QStringList m_lines;
	bool m_exists = false;
};
