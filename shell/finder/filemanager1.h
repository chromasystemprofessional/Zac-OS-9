#pragma once

#include <QStringList>

/*
 * org.freedesktop.FileManager1 on the session bus: how other programs ask
 * the file manager to show something. Firefox's "Show in Folder" after a
 * download, Chrome's and VS Code's "Reveal" all call it.
 *
 *   ShowFolders(as uris, s startup)        open each folder's window
 *   ShowItems(as uris, s startup)          open each item's folder, select it
 *   ShowItemProperties(as uris, s startup) the same, then Get Info
 *
 * Only file:// URIs; others are ignored.
 */
void fileManager1Start();

/* For `zacos9-finder --open URI...` (folders opened from other programs
 * through xdg-open): asks the running Finder to show them. False if no
 * Finder answered. */
bool fileManager1ShowFolders(const QStringList &uris);
