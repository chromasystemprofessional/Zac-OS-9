#include "custompatterns.h"

#include <QCoreApplication>
#include <QCache>
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>

#include "patterns.h"
#include "settings.h"

namespace {
struct Tile {
	QString id, name;
	QImage image;
};
struct Watch {
	QPointer<QObject> owner;
	std::function<void()> changed;
};

class Catalog : public QObject {
public:
	QList<Tile> tiles;
	QStringList errors;
	QList<Watch> watches;
	QProcess process;
	QTimer poll, timeout;
	QString fingerprint;
	bool loaded = false;
	bool wallpaper;
	QCache<QString, QImage> rendered{64 * 1024};

	explicit Catalog(bool photos = false) : QObject(QCoreApplication::instance()), wallpaper(photos) {
		const QString folder = sourceFolder();
		if (!QDir().mkpath(folder)) {
			errors << "Could not create desktop background folder: " + folder;
			qWarning().noquote() << errors.first();
		}
		poll.setInterval(2000);
		timeout.setSingleShot(true);
		timeout.setInterval(30000);
		connect(&poll, &QTimer::timeout, this, [this] { refresh(); });
		connect(&timeout, &QTimer::timeout, this, [this] {
			errors = { "Desktop pattern import timed out." };
			qWarning().noquote() << errors.first();
			process.kill();
		});
		connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
			if (error == QProcess::FailedToStart) {
				timeout.stop();
				errors = { "Desktop pattern importer could not start: " + process.errorString() };
				qWarning().noquote() << errors.first();
				notify();
			}
		});
		connect(&process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
			timeout.stop();
			if (code != 0 || status != QProcess::NormalExit) {
				errors = { "Desktop pattern import failed: " +
					QString::fromUtf8(process.readAllStandardError()).trimmed() };
				qWarning().noquote() << errors.first();
				notify();
				return;
			}
			QJsonParseError error;
			const QJsonDocument result = QJsonDocument::fromJson(process.readAllStandardOutput(), &error);
			if (!result.isObject() || !result.object().value("entries").isArray() ||
					!result.object().value("errors").isArray()) {
				errors = { "Desktop pattern importer returned invalid data: " + error.errorString() };
				qWarning().noquote() << errors.first();
				notify();
				return;
			}
			QList<Tile> next;
			errors.clear();
			for (const QJsonValue &value : result.object().value("errors").toArray()) {
				errors << value.toString();
			}
			for (const QJsonValue &value : result.object().value("entries").toArray()) {
				const QJsonObject entry = value.toObject();
				QImage image;
				if (!image.loadFromData(QByteArray::fromBase64(entry.value("png").toString().toLatin1()),
						"PNG") || entry.value("id").toString().isEmpty()) {
					errors << "A desktop pattern could not be decoded: " + entry.value("name").toString();
					continue;
				}
				next.append({ entry.value("id").toString(), entry.value("name").toString(),
					image.convertToFormat(QImage::Format_ARGB32) });
			}
			tiles = std::move(next);
			rendered.clear();
			for (const QString &message : errors) {
				qWarning().noquote() << message;
			}
			notify();
		});
		poll.start();
		QTimer::singleShot(0, this, [this] { refresh(); });
	}

	QString sourceFolder() const {
		return wallpaper ? desktopWallpaperFolder() : desktopPatternsFolder();
	}

	void notify() {
		for (int i = watches.size() - 1; i >= 0; --i) {
			if (!watches[i].owner) {
				watches.removeAt(i);
			} else {
				watches[i].changed();
			}
		}
	}

	void refresh() {
		if (process.state() != QProcess::NotRunning) {
			return;
		}
		QDir dir(sourceFolder());
		QString current = dir.exists() ? "present" : "missing";
		for (const QFileInfo &file : dir.entryInfoList(QDir::Files | QDir::Hidden, QDir::Name)) {
			current += "\n" + file.fileName() + ":" + QString::number(file.size()) + ":" +
				QString::number(file.lastModified().toMSecsSinceEpoch());
		}
		if (loaded && fingerprint == current) {
			return;
		}
		fingerprint = current;
		loaded = true;
		const QString local = QCoreApplication::applicationDirPath() + "/zacos9-patterns";
		const QString helper = QFileInfo::exists(local) ? local :
			QStandardPaths::findExecutable("zacos9-patterns");
		if (helper.isEmpty()) {
			errors = { "Desktop pattern importer isn't installed." };
			qWarning().noquote() << errors.first();
			notify();
			return;
		}
		QStringList arguments{helper};
		if (wallpaper) {
			arguments << "--wallpaper";
		}
		arguments << dir.absolutePath();
		process.start("/usr/bin/python3", arguments);
		timeout.start();
	}
};

Catalog &catalog() {
	static Catalog *instance = new Catalog;
	return *instance;
}

Catalog &wallpapers() {
	static Catalog *instance = new Catalog(true);
	return *instance;
}

QString backgroundSetting(const char *key) {
	char value[128];
	return pl_setting(key, value, sizeof(value)) ? QString::fromUtf8(value) : QString();
}
}

QString desktopPatternsFolder() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
		"/zacos9/appearance/Desktop Patterns";
}

int desktopPatternCount() {
	return pl_pattern_count() + catalog().tiles.size();
}

QString desktopPatternId(int index) {
	if (index >= 0 && index < pl_pattern_count()) {
		return QString::fromUtf8(pl_pattern_id(index));
	}
	const int custom = index - pl_pattern_count();
	return custom >= 0 && custom < catalog().tiles.size() ? catalog().tiles[custom].id : QString();
}

QString desktopPatternName(int index) {
	if (index >= 0 && index < pl_pattern_count()) {
		return QString::fromUtf8(pl_pattern_name(index));
	}
	const int custom = index - pl_pattern_count();
	return custom >= 0 && custom < catalog().tiles.size() ? catalog().tiles[custom].name : QString();
}

int desktopPatternFind(const QString &id) {
	for (int i = 0; i < desktopPatternCount(); ++i) {
		if (desktopPatternId(i) == id) {
			return i;
		}
	}
	return 0;
}

void desktopPatternFill(pl_canvas *canvas, int index, int x0, int y0, int x1, int y1) {
	const int custom = index - pl_pattern_count();
	if (custom < 0 || custom >= catalog().tiles.size()) {
		pl_pattern_fill(canvas, index, x0, y0, x1, y1);
		return;
	}
	const QImage &image = catalog().tiles[custom].image;
	for (int y = y0; y <= y1; ++y) {
		const auto *row = reinterpret_cast<const QRgb *>(
			image.constScanLine((y % image.height() + image.height()) % image.height()));
		for (int x = x0; x <= x1; ++x) {
			pl_put(canvas, x, y, row[(x % image.width() + image.width()) % image.width()]);
		}
	}
}

QStringList desktopPatternErrors() {
	return catalog().errors;
}

void watchDesktopPatterns(QObject *owner, std::function<void()> changed) {
	catalog().watches.append({ owner, std::move(changed) });
}

QString desktopWallpaperFolder() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/zacos9/wallpaper";
}

int desktopWallpaperCount() {
	return wallpapers().tiles.size();
}

QString desktopWallpaperId(int index) {
	return index >= 0 && index < wallpapers().tiles.size() ? wallpapers().tiles[index].id : QString();
}

QString desktopWallpaperName(int index) {
	return index >= 0 && index < wallpapers().tiles.size() ? wallpapers().tiles[index].name : QString();
}

int desktopWallpaperFind(const QString &id) {
	for (int i = 0; i < desktopWallpaperCount(); ++i) {
		if (desktopWallpaperId(i) == id) {
			return i;
		}
	}
	return -1;
}

QStringList desktopWallpaperErrors() {
	return wallpapers().errors;
}

void watchDesktopWallpaper(QObject *owner, std::function<void()> changed) {
	wallpapers().watches.append({ owner, std::move(changed) });
}

void desktopWallpaperFill(pl_canvas *canvas, int index, const QString &mode,
		int x0, int y0, int x1, int y1) {
	Catalog &photos = wallpapers();
	if (index < 0 || index >= photos.tiles.size()) {
		pl_pattern_fill(canvas, 0, x0, y0, x1, y1);
		return;
	}
	const QSize size(x1 - x0 + 1, y1 - y0 + 1);
	if (size.isEmpty()) {
		return;
	}
	const Tile &tile = photos.tiles[index];
	const QString key = tile.id + ":" + mode + ":" + QString::number(size.width()) +
		"x" + QString::number(size.height());
	QImage *image = photos.rendered.object(key);
	QImage uncached;
	if (!image) {
		uncached = QImage(size, QImage::Format_RGB32);
		if (uncached.isNull()) {
			qWarning() << "Could not allocate desktop wallpaper" << size;
			pl_pattern_fill(canvas, 0, x0, y0, x1, y1);
			return;
		}
		uncached.fill(GRAY(3));
		QImage scaled;
		if (mode == "center") {
			scaled = tile.image;
		} else if (mode == "fill") {
			// Crop before scaling: extreme aspect ratios must not allocate
			// an enormous off-screen image just to clip most of it away.
			QRect source = tile.image.rect();
			if (qint64(source.width()) * size.height() > qint64(source.height()) * size.width()) {
				const int width = std::max(1, int(qint64(source.height()) * size.width() / size.height()));
				source.setLeft((tile.image.width() - width) / 2);
				source.setWidth(width);
			} else {
				const int height = std::max(1, int(qint64(source.width()) * size.height() / size.width()));
				source.setTop((tile.image.height() - height) / 2);
				source.setHeight(height);
			}
			scaled = tile.image.copy(source).scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		} else {
			scaled = tile.image.scaled(size, mode == "stretch" ? Qt::IgnoreAspectRatio :
				Qt::KeepAspectRatio, Qt::SmoothTransformation);
		}
		if (scaled.isNull()) {
			qWarning() << "Could not scale desktop wallpaper" << tile.name << size;
		}
		QPainter painter(&uncached);
		painter.drawImage((size.width() - scaled.width()) / 2,
			(size.height() - scaled.height()) / 2, scaled);
		painter.end();
		const int cost = static_cast<int>(uncached.sizeInBytes() / 1024 + 1);
		if (cost <= photos.rendered.maxCost()) {
			photos.rendered.insert(key, new QImage(uncached), cost);
			image = photos.rendered.object(key);
		} else {
			image = &uncached;
		}
	}
	for (int y = 0; y < size.height(); ++y) {
		const auto *row = reinterpret_cast<const QRgb *>(image->constScanLine(y));
		for (int x = 0; x < size.width(); ++x) {
			pl_put(canvas, x0 + x, y0 + y, row[x]);
		}
	}
}

void desktopBackgroundFill(pl_canvas *canvas, int width, int height) {
	if (backgroundSetting("background") == "wallpaper") {
		desktopWallpaperFill(canvas, desktopWallpaperFind(backgroundSetting("wallpaper")),
			backgroundSetting("wallpaper-mode"), 0, 0, width - 1, height - 1);
	} else {
		desktopPatternFill(canvas, desktopPatternFind(backgroundSetting("pattern")),
			0, 0, width - 1, height - 1);
	}
}
