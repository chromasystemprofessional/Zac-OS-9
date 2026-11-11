#pragma once

#include <QImage>
#include <QString>

/*
 * A classic Mac OS file or folder's custom icon. Mac OS 9 kept it in a
 * resource fork: a folder's in "Icon\r" inside it (its data fork empty),
 * a file's in the file. The icon resources are 'icl8'/'icl4' (32x32
 * colour), 'ICN#' (their 1-bit mask, and a black-and-white fallback), the
 * 16x16 'ics8'/'ics4'/'ics#', and an 'icns' icon suite (Mac OS 8.5+) that
 * groups those same resources.
 *
 * On Unix the forks arrive as AppleDouble "._" companions, Netatalk
 * ".rsrc" copies, or an HFS mount's "..namedfork/rsrc"; `path` may be
 * either a folder or a file. Returns a 32x32 transparent image, or a null
 * image when the item carries no icon.
 */
QImage macResourceIconImage(const QString &path);
