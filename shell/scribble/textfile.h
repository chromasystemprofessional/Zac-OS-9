#pragma once

#include <QString>

/*
 * Scribble's files: plain text. UTF-8 when it is, else Latin-1 (old Read Me
 * files); the line endings it had (Mac OS's CR, DOS's CR LF or Unix's LF)
 * are written back the same way.
 */
enum class LineEnding { Lf, CrLf, Cr };

struct TextFile {
	QString text;            /* with \n line breaks */
	LineEnding ending = LineEnding::Lf;
};

/* False, with a reason, for a file that isn't text or is too big. */
bool readTextFile(const QString &path, TextFile *file, QString *error);
/* Written whole or not at all. */
bool writeTextFile(const QString &path, const TextFile &file, QString *error);
