#pragma once

#include <QString>
#include <QStringList>

/*
 * Where a folder sits, as the Mac showed it: a ⌘-click on a Finder window's
 * title lists the folder and every folder enclosing it, out to its disk;
 * ⌘-↑ opens the enclosing one. Folders in the Macintosh view go up through
 * it (Applications, then Zacintosh HD); a real folder goes up through real
 * folders until one the view stands for (Home for $HOME), or a disk's top.
 */

/* The folder enclosing `path`, or empty at a disk's top (or the Trash). */
QString enclosingFolder(const QString &path);
/* `path`, then each enclosing folder, out to its disk. */
QStringList folderChain(const QString &path);
/* The name a folder shows in that list (and its window title). */
QString folderTitle(const QString &path);
