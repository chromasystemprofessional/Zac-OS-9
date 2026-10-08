/* Picture previews: the freedesktop cache, decoding, the framed icon, the
 * asynchronous service, and the Finder's icon painter using it. */

#include <QApplication>
#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QTemporaryDir>
#include <QUrl>
#include <QWidget>
#include <cstdio>
#include <sys/stat.h>
#include <utime.h>

#include "items.h"
#include "thumbnails.h"

static int failures = 0;

static void check(bool ok, const char *message) {
	std::printf("%s: %s\n", ok ? "ok" : "FAIL", message);
	failures += ok ? 0 : 1;
}

class PaintCounter : public QWidget {
public:
	int paints = 0;

protected:
	void paintEvent(QPaintEvent *) override { paints++; }
};

static ThumbnailState waitFor(const QString &path, std::vector<uint32_t> *large,
		std::vector<uint32_t> *small) {
	QElapsedTimer timer;
	timer.start();
	ThumbnailState state = thumbnailFor(path, large, small);
	while (state == ThumbnailState::Pending && timer.elapsed() < 10000) {
		QApplication::processEvents(QEventLoop::AllEvents, 20);
		state = thumbnailFor(path, large, small);
	}
	return state;
}

static QByteArray readAll(const QString &path) {
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

static bool writeAll(const QString &path, const QByteArray &data) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

static QByteArray textChunk(const QByteArray &key, const QByteArray &value) {
	return QByteArray("tEXt") + key + '\0' + value;
}

static QByteArray pngBytes(const QImage &image) {
	QByteArray data;
	QBuffer buffer(&data);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "PNG");
	return data;
}

/* `png` given the text chunks that follow the header in `source`. */
static QByteArray withTextOf(const QByteArray &source, const QByteArray &png) {
	constexpr int afterHeader = 33;
	int end = afterHeader;
	while (end + 8 <= source.size() && source.mid(end + 4, 4) == "tEXt") {
		const QByteArray length = source.mid(end, 4);
		const int n = (quint8(length[0]) << 24) | (quint8(length[1]) << 16) |
			(quint8(length[2]) << 8) | quint8(length[3]);
		end += 12 + n;
	}
	return png.left(afterHeader) + source.mid(afterHeader, end - afterHeader) +
		png.mid(afterHeader);
}

static void setModified(const QString &path, qint64 seconds) {
	struct utimbuf times { static_cast<time_t>(seconds), static_cast<time_t>(seconds) };
	utime(QFile::encodeName(path).constData(), &times);
}

int main(int argc, char **argv) {
	QTemporaryDir home;
	const QString base = QFileInfo(home.path()).canonicalFilePath();
	qputenv("XDG_CACHE_HOME", QFile::encodeName(base + "/cache"));
	QApplication app(argc, argv);

	/* A wide picture: blue left half, red right half. */
	QImage wide(400, 200, QImage::Format_RGB32);
	wide.fill(Qt::red);
	for (int y = 0; y < 200; y++) {
		for (int x = 0; x < 200; x++) {
			wide.setPixel(x, y, qRgb(0, 0, 255));
		}
	}
	const QString png = base + "/Wide Picture.png";
	check(wide.save(png, "PNG"), "create picture");
	setModified(png, 1700000000);

	const QString text = base + "/notes.txt";
	QFile notes(text);
	notes.open(QIODevice::WriteOnly);
	notes.write("not a picture\n");
	notes.close();
	const QString broken = base + "/broken.png";
	QFile bad(broken);
	bad.open(QIODevice::WriteOnly);
	bad.write("\x89PNG\r\n\x1a\nthis is not really a picture");
	bad.close();

	check(isThumbnailable(png), "a PNG is a picture");
	check(!isThumbnailable(text) && !isThumbnailable(base), "text files and folders are not");

	/* The 128-pixel preview, stored where other software looks for it. */
	const QImage normal = normalThumbnail(png);
	check(normal.size() == QSize(128, 64), "preview fits 128 pixels, keeping its shape");
	const QString cached = thumbnailCachePath(png);
	check(cached.startsWith(base + "/cache/thumbnails/normal/") && QFileInfo(cached).isFile(),
		"preview is saved in the freedesktop thumbnail cache");
	const QByteArray stored = readAll(cached);
	check(stored.contains(textChunk("Thumb::URI", QUrl::fromLocalFile(png).toEncoded())) &&
		stored.contains(textChunk("Thumb::MTime", "1700000000")),
		"cached preview records the picture's URI and modification time");
	check((QFileInfo(cached).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther)) == 0,
		"cached preview is private to the user");
	struct stat dirStat {};
	stat(QFile::encodeName(base + "/cache/thumbnails").constData(), &dirStat);
	check((dirStat.st_mode & 0777) == 0700, "thumbnail cache folder is private");

	/* A current cached preview is used as is... */
	QImage marked(128, 64, QImage::Format_RGB32);
	marked.fill(Qt::green);
	check(writeAll(cached, withTextOf(stored, pngBytes(marked))), "mark the cached preview");
	check(normalThumbnail(png).pixel(10, 10) == qRgb(0, 255, 0),
		"a current cached preview is read instead of decoding again");
	/* ...an edited picture gets a new one. */
	setModified(png, 1700000100);
	const QImage fresh = normalThumbnail(png);
	check(fresh.pixel(10, 10) == qRgb(0, 0, 255) &&
		readAll(cached).contains(textChunk("Thumb::MTime", "1700000100")),
		"an edited picture replaces its outdated cached preview");

	check(normalThumbnail(broken).isNull() && normalThumbnail(text).isNull(),
		"unreadable files have no preview");

	/* Camera photos record their orientation rather than turning pixels. */
	const QString photo = base + "/photo.jpg";
	{
		QImageWriter writer(photo, "jpeg");
		writer.setTransformation(QImageIOHandler::TransformationRotate90);
		check(writer.write(QImage(400, 200, QImage::Format_RGB32)), "create rotated photo");
	}
	const QImage upright = normalThumbnail(photo);
	check(upright.width() < upright.height(), "photo previews follow their orientation");

	/* The icon: framed, centred, transparent around a wide picture. */
	const QImage icon = thumbnailIcon(fresh, 32);
	check(icon.size() == QSize(32, 32) && icon.format() == QImage::Format_ARGB32,
		"icon is 32 pixels square");
	check(qAlpha(icon.pixel(0, 0)) == 0 && qAlpha(icon.pixel(16, 2)) == 0,
		"space beside a wide picture stays transparent");
	check(icon.pixel(0, 16) == qRgb(0, 0, 0) && icon.pixel(31, 16) == qRgb(0, 0, 0),
		"picture is framed in black");
	check(icon.pixel(5, 16) == qRgb(0, 0, 255) && icon.pixel(26, 16) == qRgb(255, 0, 0),
		"the picture itself is shown");
	QImage four(4, 4, QImage::Format_RGB32);
	four.fill(Qt::white);
	const QImage tiny = thumbnailIcon(four, 32);
	check(qAlpha(tiny.pixel(10, 10)) == 0 && qAlpha(tiny.pixel(16, 16)) == 255,
		"small pictures are not enlarged");

	/* The service decodes off the paint path and repaints watchers. */
	PaintCounter watcher;
	watcher.resize(10, 10);
	watcher.show();
	QApplication::processEvents();
	watchThumbnails(&watcher);
	const int paintsBefore = watcher.paints;
	std::vector<uint32_t> large, small;
	check(thumbnailFor(png, &large, &small) == ThumbnailState::Pending && large.empty(),
		"first request is queued, not decoded while painting");
	check(waitFor(png, &large, &small) == ThumbnailState::Ready &&
		large.size() == 32 * 32 && small.size() == 16 * 16,
		"preview icons arrive at 32 and 16 pixels");
	QElapsedTimer settle;
	settle.start();
	while (watcher.paints == paintsBefore && settle.elapsed() < 2000) {
		QApplication::processEvents(QEventLoop::AllEvents, 20);
	}
	check(watcher.paints > paintsBefore, "watching windows repaint when previews arrive");
	check(waitFor(broken, &large, &small) == ThumbnailState::None &&
		thumbnailFor(text, &large, &small) == ThumbnailState::None,
		"unreadable pictures and other files keep their icons");

	const QString alias = base + "/Picture alias";
	QFile::link(png, alias);
	std::vector<uint32_t> aliasLarge, aliasSmall;
	waitFor(png, &large, &small);
	check(waitFor(alias, &aliasLarge, &aliasSmall) == ThumbnailState::Ready &&
		aliasLarge == large, "an alias shows its original's preview");

	/* The Finder's painter: the document icon until ready, then the preview. */
	const QString other = base + "/Second.png";
	check(wide.save(other, "PNG"), "create second picture");
	auto item = makeItem(QFileInfo(other));
	Pixels canvas(32, 32);
	paintIcon(&canvas.c, *item, 0, 0, PL_ICON_LARGE, false);
	check(item->customIcon32.empty() && !item->thumbnailResolved,
		"a picture keeps its document icon while its preview is made");
	QElapsedTimer ready;
	ready.start();
	while (!item->thumbnailResolved && ready.elapsed() < 10000) {
		QApplication::processEvents(QEventLoop::AllEvents, 20);
		paintIcon(&canvas.c, *item, 0, 0, PL_ICON_LARGE, false);
	}
	check(item->thumbnailResolved && item->customIcon32 == large,
		"Finder icons show the picture's preview");
	const QRgb painted = canvas.img.pixel(5, 16);
	check(qBlue(painted) > 200 && qRed(painted) < 50, "the preview is painted in place of the icon");

	auto textItem = makeItem(QFileInfo(text));
	paintIcon(&canvas.c, *textItem, 0, 0, PL_ICON_LARGE, false);
	check(textItem->thumbnailResolved && textItem->customIcon32.empty(),
		"other documents keep the document icon");

	return failures ? 1 : 0;
}
