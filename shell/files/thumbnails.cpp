#include "thumbnails.h"

#include <QApplication>
#include <QBuffer>
#include <QCache>
#include <QCryptographicHash>
#include <QDir>
#include <QHash>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QPainter>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimer>
#include <QWidget>
#include <cstring>
#include <glib.h>

namespace {

constexpr int NORMAL_SIZE = 128;           /* the spec's "normal" size */
constexpr qint64 MAX_BYTES = 256LL << 20;  /* bigger files keep their document icon */

QString thumbnailRoot() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
		"/thumbnails";
}

/* The URI as GLib writes it, so previews are shared with GTK software. */
QString fileUri(const QString &path) {
	char *uri = g_filename_to_uri(QFile::encodeName(path).constData(), nullptr, nullptr);
	const QString result = uri ? QString::fromUtf8(uri) : QString();
	g_free(uri);
	return result;
}

std::vector<uint32_t> argb(const QImage &image) {
	const QImage img = image.convertToFormat(QImage::Format_ARGB32);
	const int size = img.width();
	std::vector<uint32_t> out(static_cast<size_t>(size) * size);
	for (int y = 0; y < size; y++) {
		memcpy(out.data() + static_cast<size_t>(y) * size, img.constScanLine(y),
			static_cast<size_t>(size) * sizeof(uint32_t));
	}
	return out;
}

/* PNG text chunks, written and read here: Qt's own image text splits keys at
 * colons, and the spec's keys are "Thumb::URI" and "Thumb::MTime". */
quint32 crc32(const QByteArray &data) {
	static const std::vector<quint32> table = []() {
		std::vector<quint32> t(256);
		for (quint32 n = 0; n < 256; n++) {
			quint32 c = n;
			for (int k = 0; k < 8; k++) {
				c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
			}
			t[n] = c;
		}
		return t;
	}();
	quint32 c = 0xffffffffu;
	for (const char byte : data) {
		c = table[(c ^ static_cast<quint8>(byte)) & 0xff] ^ (c >> 8);
	}
	return c ^ 0xffffffffu;
}

void appendBigEndian(QByteArray *out, quint32 value) {
	for (int shift = 24; shift >= 0; shift -= 8) {
		out->append(static_cast<char>((value >> shift) & 0xff));
	}
}

quint32 bigEndian(const QByteArray &data, qsizetype at) {
	quint32 value = 0;
	for (int i = 0; i < 4; i++) {
		value = value << 8 | static_cast<quint8>(data.at(at + i));
	}
	return value;
}

const QByteArray PNG_SIGNATURE("\x89PNG\r\n\x1a\n", 8);

/* `png` with tEXt chunks added after its header chunk. */
QByteArray withPngText(const QByteArray &png, const QList<QPair<QByteArray, QByteArray>> &texts) {
	constexpr qsizetype afterHeader = 8 + 4 + 4 + 13 + 4;
	if (!png.startsWith(PNG_SIGNATURE) || png.size() < afterHeader || png.mid(12, 4) != "IHDR") {
		return {};
	}
	QByteArray out = png.left(afterHeader);
	for (const auto &[key, value] : texts) {
		const QByteArray chunk = "tEXt" + key + '\0' + value;
		appendBigEndian(&out, static_cast<quint32>(chunk.size() - 4));
		out += chunk;
		appendBigEndian(&out, crc32(chunk));
	}
	return out + png.mid(afterHeader);
}

/* The tEXt chunks before the image data. */
QHash<QByteArray, QByteArray> pngText(const QByteArray &png) {
	QHash<QByteArray, QByteArray> texts;
	if (!png.startsWith(PNG_SIGNATURE)) {
		return texts;
	}
	qsizetype at = 8;
	while (at + 12 <= png.size()) {
		const quint32 length = bigEndian(png, at);
		const QByteArray type = png.mid(at + 4, 4);
		if (length > static_cast<quint32>(png.size() - at - 12) || type == "IDAT" || type == "IEND") {
			break;
		}
		if (type == "tEXt") {
			const QByteArray body = png.mid(at + 8, length);
			const qsizetype nul = body.indexOf('\0');
			if (nul > 0) {
				texts.insert(body.left(nul), body.mid(nul + 1));
			}
		}
		at += 12 + length;
	}
	return texts;
}

struct Entry {
	qint64 modified = 0;
	qint64 size = 0;
	ThumbnailState state = ThumbnailState::None;
	std::vector<uint32_t> large, small;
};

class Service : public QObject {
public:
	Service() : QObject(qApp) {
		/* Few threads: a folder on a network volume reads whole files. */
		m_pool.setMaxThreadCount(2);
		m_cache.setMaxCost(2000);
		m_repaint.setSingleShot(true);
		m_repaint.setInterval(50);
		connect(&m_repaint, &QTimer::timeout, this, [this]() {
			m_watchers.removeAll(nullptr);
			for (const QPointer<QWidget> &widget : m_watchers) {
				widget->update();
			}
		});
	}

	~Service() override {
		m_pool.clear();
		m_pool.waitForDone();
	}

	ThumbnailState lookup(const QString &path, std::vector<uint32_t> *large,
			std::vector<uint32_t> *small) {
		const QString canonical = QFileInfo(path).canonicalFilePath();
		const QFileInfo info(canonical);
		if (canonical.isEmpty() || !info.isFile()) {
			return ThumbnailState::None;
		}
		const qint64 modified = info.lastModified().toMSecsSinceEpoch();
		const qint64 size = info.size();
		if (const Entry *entry = m_cache.object(canonical);
				entry && entry->modified == modified && entry->size == size) {
			if (entry->state == ThumbnailState::Ready) {
				*large = entry->large;
				*small = entry->small;
			}
			return entry->state;
		}
		if (m_pending.contains(canonical)) {
			return ThumbnailState::Pending;
		}
		if (size > MAX_BYTES || !isThumbnailable(canonical)) {
			m_cache.insert(canonical, new Entry{ modified, size, ThumbnailState::None, {}, {} });
			return ThumbnailState::None;
		}
		m_pending.insert(canonical);
		m_pool.start([this, canonical, modified, size]() {
			auto *entry = new Entry{ modified, size, ThumbnailState::None, {}, {} };
			const QImage thumb = normalThumbnail(canonical);
			if (!thumb.isNull()) {
				entry->state = ThumbnailState::Ready;
				entry->large = argb(thumbnailIcon(thumb, 32));
				entry->small = argb(thumbnailIcon(thumb, 16));
			}
			QMetaObject::invokeMethod(this, [this, canonical, entry]() {
				m_pending.remove(canonical);
				m_cache.insert(canonical, entry);
				if (!m_repaint.isActive()) {
					m_repaint.start();
				}
			}, Qt::QueuedConnection);
		});
		return ThumbnailState::Pending;
	}

	void watch(QWidget *widget) {
		if (!m_watchers.contains(widget)) {
			m_watchers.append(widget);
		}
	}

private:
	QThreadPool m_pool;
	QCache<QString, Entry> m_cache;
	QSet<QString> m_pending;
	QTimer m_repaint;
	QList<QPointer<QWidget>> m_watchers;
};

Service &service() {
	static QPointer<Service> instance;
	if (!instance) {
		instance = new Service;
	}
	return *instance;
}

} // namespace

ThumbnailState thumbnailFor(const QString &path, std::vector<uint32_t> *large,
		std::vector<uint32_t> *small) {
	return service().lookup(path, large, small);
}

void watchThumbnails(QWidget *widget) {
	service().watch(widget);
}

bool isThumbnailable(const QString &path) {
	static const QSet<QByteArray> supported = []() {
		QSet<QByteArray> types;
		for (const QByteArray &type : QImageReader::supportedMimeTypes()) {
			types.insert(type);
		}
		return types;
	}();
	const QFileInfo info(path);
	if (!info.isFile()) {
		return false;
	}
	static QMimeDatabase db;
	const QMimeType mime = db.mimeTypeForFile(info);
	if (!mime.name().startsWith(QLatin1String("image/"))) {
		return false;
	}
	if (supported.contains(mime.name().toLatin1())) {
		return true;
	}
	for (const QString &alias : mime.aliases()) {
		if (supported.contains(alias.toLatin1())) {
			return true;
		}
	}
	return false;
}

QString thumbnailCachePath(const QString &canonicalPath) {
	const QByteArray hash = QCryptographicHash::hash(fileUri(canonicalPath).toUtf8(),
		QCryptographicHash::Md5).toHex();
	return thumbnailRoot() + "/normal/" + QString::fromLatin1(hash) + ".png";
}

QImage normalThumbnail(const QString &canonicalPath) {
	const QFileInfo info(canonicalPath);
	if (!info.isFile() || info.size() > MAX_BYTES) {
		return {};
	}
	const QString uri = fileUri(canonicalPath);
	const QString mtime = QString::number(info.lastModified().toSecsSinceEpoch());
	const QString cachePath = thumbnailCachePath(canonicalPath);
	QFile cached(cachePath);
	if (cached.size() < (8 << 20) && cached.open(QIODevice::ReadOnly)) {
		const QByteArray data = cached.readAll();
		const QHash<QByteArray, QByteArray> texts = pngText(data);
		if (texts.value("Thumb::URI") == uri.toUtf8() &&
				texts.value("Thumb::MTime") == mtime.toLatin1()) {
			const QImage image = QImage::fromData(data, "PNG");
			if (!image.isNull()) {
				return image;
			}
		}
	}

	QImageReader reader(canonicalPath);
	reader.setAutoTransform(true); /* camera photos stand the right way up */
	const QSize full = reader.size();
	if (full.isValid() && (full.width() > NORMAL_SIZE || full.height() > NORMAL_SIZE)) {
		reader.setScaledSize(full.scaled(NORMAL_SIZE, NORMAL_SIZE, Qt::KeepAspectRatio)
			.expandedTo(QSize(1, 1)));
	}
	QImage image = reader.read();
	if (image.isNull()) {
		return {};
	}
	if (image.width() > NORMAL_SIZE || image.height() > NORMAL_SIZE) {
		image = image.scaled(NORMAL_SIZE, NORMAL_SIZE, Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
	}
	image = image.convertToFormat(image.hasAlphaChannel() ?
		QImage::Format_ARGB32 : QImage::Format_RGB32);

	/* The spec forbids previews of the previews themselves. */
	const QString root = QDir::cleanPath(thumbnailRoot());
	if (canonicalPath.startsWith(root + "/")) {
		return image;
	}
	const QString dir = QFileInfo(cachePath).absolutePath();
	if (g_mkdir_with_parents(QFile::encodeName(dir).constData(), 0700) != 0) {
		return image;
	}
	QByteArray encoded;
	QBuffer buffer(&encoded);
	buffer.open(QIODevice::WriteOnly);
	if (!image.save(&buffer, "PNG")) {
		return image;
	}
	const QByteArray png = withPngText(encoded, {
		{ "Thumb::URI", uri.toUtf8() },
		{ "Thumb::MTime", mtime.toLatin1() },
		{ "Thumb::Size", QByteArray::number(info.size()) },
		{ "Software", "ZacOS Finder" },
	});
	QSaveFile file(cachePath);
	if (!png.isEmpty() && file.open(QIODevice::WriteOnly)) {
		file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
		if (file.write(png) == png.size()) {
			file.commit();
		} else {
			file.cancelWriting();
		}
	}
	return image;
}

QImage thumbnailIcon(const QImage &thumb, int size) {
	QImage icon(size, size, QImage::Format_ARGB32);
	icon.fill(Qt::transparent);
	if (thumb.isNull() || size < 3) {
		return icon;
	}
	const int inner = size - 2;
	QSize fitted = thumb.size();
	if (fitted.width() > inner || fitted.height() > inner) {
		fitted = fitted.scaled(inner, inner, Qt::KeepAspectRatio);
	}
	fitted = fitted.expandedTo(QSize(1, 1));
	const QImage picture = thumb.scaled(fitted, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	const int x = (size - fitted.width()) / 2;
	const int y = (size - fitted.height()) / 2;
	QPainter p(&icon);
	p.fillRect(x - 1, y - 1, fitted.width() + 2, fitted.height() + 2, Qt::black);
	p.fillRect(x, y, fitted.width(), fitted.height(), Qt::white);
	p.drawImage(x, y, picture);
	p.end();
	return icon;
}
