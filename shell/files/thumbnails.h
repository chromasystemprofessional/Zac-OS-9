#pragma once

/* Picture files shown by their pictures: a framed preview drawn in place of
 * the document icon, like the preview icons Mac OS 9 picture programs saved.
 *
 * Decoding happens on worker threads, never in a paint. The 128-pixel
 * previews are shared with other desktop software through the freedesktop
 * thumbnail cache ($XDG_CACHE_HOME/thumbnails/normal), checked against the
 * file's modification time, so an edited picture gets a new preview. */

#include <QImage>
#include <QString>
#include <cstdint>
#include <vector>

class QWidget;

enum class ThumbnailState { Pending, Ready, None };

/* The preview icons for `path` (32x32 and 16x16 straight-alpha ARGB). Pending
 * queues the work once and repaints the watched widgets when it is done; None
 * means the file is not a readable picture. Aliases show their original's. */
ThumbnailState thumbnailFor(const QString &path, std::vector<uint32_t> *large,
		std::vector<uint32_t> *small);
/* Repaint `widget` whenever previews become ready. */
void watchThumbnails(QWidget *widget);

/* Building blocks, exposed for tests. */
bool isThumbnailable(const QString &path);
QString thumbnailCachePath(const QString &canonicalPath);
/* The 128-pixel preview, from the cache when current, else decoded (and
 * cached). Null if the file can't be read as a picture. */
QImage normalThumbnail(const QString &canonicalPath);
/* `thumb` fitted into a `size` square: framed, on white, never enlarged. */
QImage thumbnailIcon(const QImage &thumb, int size);
