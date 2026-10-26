#pragma once

/*
 * Clippings (Missing Manual pp. 168-170): text or a picture dragged out of
 * a program onto the desktop or into a folder becomes a clipping file, and
 * a link dragged out of a browser an Internet location. Opening a clipping
 * shows what it holds; dragging it into a document puts that back, and an
 * Internet location dragged into a browser goes there. Edit > Show
 * Clipboard shows what was last copied.
 *
 * On disk they are ordinary files other programs can open: "Some words
 * clipping.txt", "picture clipping.png" (a number before the extension if
 * the name is taken), and Internet locations as "Title.url" in the
 * [InternetShortcut] format browsers and file managers already know.
 */

#include <QString>
#include <QStringList>

class QMimeData;

enum class ClippingKind { None, Text, Picture, Location };

/* What a file is, by its name. */
ClippingKind clippingKind(const QString &path);
/* An Internet location's address, or empty. */
QString internetLocationUrl(const QString &path);

/* A drag from another program that would make clippings here: text, a
 * picture or links, and no files. */
bool canMakeClipping(const QMimeData *mime);
/* Makes them in `folder`: the new files' paths, or none (and *error). */
QStringList makeClippings(const QMimeData *mime, const QString &folder, QString *error);
/* A clipping's name for some text: its first few words. */
QString clippingBaseName(const QString &text);

/* What a clipping dragged out of the Sniffer carries for the program it
 * lands in: its text, its picture, or its address. */
void addClippingContent(QMimeData *mime, const QString &path);

/* A window showing a clipping's text or picture. */
void openClippingWindow(const QString &path);
/* Edit > Show Clipboard: a window that follows the clipboard. */
void showClipboardWindow();
