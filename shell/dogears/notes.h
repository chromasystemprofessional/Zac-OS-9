#pragma once

#include <QList>
#include <QPoint>
#include <QSize>
#include <QString>

/*
 * Dog Ears' notes (Mac OS 9's Stickies), all kept in one file,
 * $XDG_DATA_HOME/zacos9/dog-ears.json: each note's text, colour, size and
 * place.
 */
enum class NoteColor { Yellow, Blue, Green, Pink, Purple, Gray };
inline constexpr int NOTE_COLORS = 6;
const char *noteColorName(NoteColor c);
unsigned noteColorRgb(NoteColor c);

struct Note {
	QString text;
	NoteColor color = NoteColor::Yellow;
	QSize size = QSize(220, 160);
	QPoint position;
	bool hasPosition = false;
};

QString notesPath();
/* Notes from `path`; none if it's missing or unreadable. */
QList<Note> loadNotes(const QString &path);
bool saveNotes(const QString &path, const QList<Note> &notes);
/* A note's window title: its first line, shortened. */
QString noteTitle(const QString &text);
