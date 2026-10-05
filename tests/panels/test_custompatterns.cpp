#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTemporaryDir>
#include <QThread>
#include <QTextStream>
#include <cassert>
#include <functional>

#include "custompatterns.h"
#include "patterns.h"
#include "appearance.h"
#include "settings.h"

static bool waitFor(std::function<bool()> ready) {
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 10000) {
		QCoreApplication::processEvents();
		QThread::msleep(10);
	}
	return ready();
}

int main(int argc, char **argv) {
	QTemporaryDir root;
	assert(root.isValid());
	qputenv("XDG_DATA_HOME", root.path().toUtf8());
	qputenv("XDG_CONFIG_HOME", (root.path() + "/config").toUtf8());
	QApplication app(argc, argv);
	const QString folder = desktopPatternsFolder();
	assert(folder == root.path() + "/zacos9/appearance/Desktop Patterns");
	QObject owner;
	int changed = 0;
	watchDesktopPatterns(&owner, [&] { changed++; });
	assert(QDir(folder).exists());
	assert(desktopPatternCount() == pl_pattern_count());
	QImage image(2, 2, QImage::Format_RGB32);
	image.setPixel(0, 0, qRgb(255, 0, 0));
	image.setPixel(1, 0, qRgb(0, 255, 0));
	image.setPixel(0, 1, qRgb(0, 0, 255));
	image.setPixel(1, 1, qRgb(255, 255, 0));
	const QString path = folder + "/My Pattern.png";
	assert(image.save(path));
	assert(waitFor([] { return desktopPatternCount() == pl_pattern_count() + 1; }));
	assert(changed > 0);
	const int index = pl_pattern_count();
	const QString id = desktopPatternId(index);
	assert(id.startsWith("user:") && id.size() < 120);
	assert(desktopPatternFind(id) == index);
	assert(desktopPatternName(index).contains("My Pattern"));
	assert(desktopPatternErrors().isEmpty());
	AppearancePanel panel;
	panel.showTab(1);
	QKeyEvent select(QEvent::KeyPress, Qt::Key_M, Qt::NoModifier, "My Pattern");
	QApplication::sendEvent(&panel, &select);
	char selected[128];
	assert(pl_setting("pattern", selected, sizeof(selected)));
	assert(QString::fromUtf8(selected) == id);
	QImage preview(panel.size(), QImage::Format_RGB32);
	QPainter painter(&preview);
	panel.render(&painter);
	painter.end();
	int customPixels = 0;
	for (int y = 0; y < preview.height(); ++y) {
		for (int x = 0; x < preview.width(); ++x) {
			if (preview.pixel(x, y) == qRgb(255, 0, 0) ||
					preview.pixel(x, y) == qRgb(0, 255, 0) ||
					preview.pixel(x, y) == qRgb(0, 0, 255) ||
					preview.pixel(x, y) == qRgb(255, 255, 0)) {
				customPixels++;
			}
		}
	}
	assert(customPixels == 112 * 112);
	uint32_t pixels[36] = {};
	pl_canvas canvas = {pixels, 6, -1, -1, 6, 6};
	desktopPatternFill(&canvas, index, -1, -1, 4, 4);
	for (int y = -1; y <= 4; ++y) {
		for (int x = -1; x <= 4; ++x) {
			assert(pixels[(y + 1) * 6 + x + 1] == image.pixel((x + 2) % 2, (y + 2) % 2));
		}
	}
	image.fill(qRgb(12, 34, 56));
	assert(image.save(path));
	const int previous = changed;
	assert(waitFor([&] { return changed > previous; }));
	desktopPatternFill(&canvas, desktopPatternFind(id), -1, -1, 4, 4);
	assert(pixels[0] == qRgb(12, 34, 56));
	assert(desktopPatternId(desktopPatternFind(id)) == id);
	QFile invalid(folder + "/Bad.ppat");
	assert(invalid.open(QIODevice::WriteOnly));
	invalid.write("bad");
	invalid.close();
	assert(waitFor([] { return !desktopPatternErrors().isEmpty(); }));
	assert(desktopPatternCount() == pl_pattern_count() + 1);
	assert(QFile::remove(path));
	assert(waitFor([] { return desktopPatternCount() == pl_pattern_count(); }));
	assert(desktopPatternFind(id) == 0);
	assert(desktopPatternFind("ocean-ripple") == pl_pattern_find("ocean-ripple"));
	QObject *temporary = new QObject;
	watchDesktopPatterns(temporary, [] { assert(!"Destroyed watcher called"); });
	delete temporary;
	assert(QFile::remove(invalid.fileName()));
	assert(waitFor([] { return desktopPatternErrors().isEmpty(); }));
	// Wallpaper is a separate catalog and mode, not a tile repeated on screen.
	const QString photos = desktopWallpaperFolder();
	QObject photoOwner;
	watchDesktopWallpaper(&photoOwner, [&] { changed++; });
	assert(QDir(photos).exists());
	assert(photos == root.path() + "/zacos9/wallpaper");
	QImage photo(4, 2, QImage::Format_RGB32);
	photo.fill(qRgb(17, 91, 163));
	const QString photoPath = photos + "/Photo.png";
	assert(photo.save(photoPath));
	assert(waitFor([] { return desktopWallpaperCount() == 1; }));
	const QString photoId = desktopWallpaperId(0);
	assert(photoId.startsWith("wallpaper:"));
	assert(desktopWallpaperFind(photoId) == 0);
	panel.showTab(2);
	QKeyEvent choosePhoto(QEvent::KeyPress, Qt::Key_P, Qt::NoModifier, "Photo");
	QApplication::sendEvent(&panel, &choosePhoto);
	assert(pl_setting("wallpaper", selected, sizeof(selected)) && QString::fromUtf8(selected) == photoId);
	assert(pl_setting("background", selected, sizeof(selected)) && QString::fromUtf8(selected) == "wallpaper");
	assert(desktopPatternFind(id) == 0); // Removed pattern retains its old setting.
	QImage photoPreview(panel.size(), QImage::Format_RGB32);
	QPainter photoPainter(&photoPreview);
	panel.render(&photoPainter);
	photoPainter.end();
	int photoPixels = 0;
	for (int y = 0; y < photoPreview.height(); ++y) {
		for (int x = 0; x < photoPreview.width(); ++x) {
			photoPixels += photoPreview.pixel(x, y) == qRgb(17, 91, 163);
		}
	}
	assert(photoPixels == 112 * 56);
	panel.show();
	const int popupX = 10 + 3 + PL_GROUP_MARGIN + 80 + 10;
	const int itemTop = 10 + PL_TAB_H + 3 + 16 + 1 + PL_GROUP_MARGIN_TOP;
	const int popupY = itemTop + 6 * PL_LIST_ROW_H + 1 + 16 + 8;
	const QPointF position(popupX, popupY);
	QMouseEvent openPlacement(QEvent::MouseButtonPress, position, panel.mapToGlobal(position.toPoint()),
		Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(&panel, &openPlacement);
	QWidget *popup = QApplication::activePopupWidget();
	assert(popup);
	QKeyEvent nextPlacement(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
	QKeyEvent applyPlacement(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
	QApplication::sendEvent(popup, &nextPlacement);
	QApplication::sendEvent(popup, &applyPlacement);
	assert(pl_setting("wallpaper-mode", selected, sizeof(selected)) &&
		QString::fromUtf8(selected) == "fill");
	for (const QString mode : { "fit", "fill", "stretch", "center" }) {
		uint32_t background[64] = {};
		pl_canvas screen = {background, 8, 0, 0, 8, 8};
		desktopWallpaperFill(&screen, 0, mode, 0, 0, 7, 7);
		for (int y = 0; y < 8; ++y) {
			for (int x = 0; x < 8; ++x) {
				const bool inPhoto = mode == "fit" ? y >= 2 && y < 6 :
					mode == "center" ? x >= 2 && x < 6 && y >= 3 && y < 5 : true;
				assert(background[y * 8 + x] == (inPhoto ? qRgb(17, 91, 163) : GRAY(3)));
			}
		}
	}
	uint32_t widePixels[96] = {};
	pl_canvas wide = {widePixels, 12, 0, 0, 12, 8};
	assert(pl_setting_set("wallpaper-mode", "fit"));
	desktopBackgroundFill(&wide, 12, 8);
	for (int y = 0; y < 8; ++y) {
		assert(widePixels[y * 12] == (y >= 1 && y < 7 ? qRgb(17, 91, 163) : GRAY(3)));
	}
	// A portrait monitor computes its own placement, rather than sharing
	// the landscape monitor's crop or a combined-desktop image.
	uint32_t tallPixels[96] = {};
	pl_canvas tall = {tallPixels, 8, 0, 0, 8, 12};
	desktopBackgroundFill(&tall, 8, 12);
	for (int y = 0; y < 12; ++y) {
		assert(tallPixels[y * 8] == (y >= 4 && y < 8 ? qRgb(17, 91, 163) : GRAY(3)));
	}
	AppearancePanel reopened;
	reopened.showTab(2);
	assert(pl_setting("wallpaper", selected, sizeof(selected)) && QString::fromUtf8(selected) == photoId);
	photo.fill(qRgb(91, 17, 163));
	const int beforePhotoChange = changed;
	assert(photo.save(photoPath));
	assert(waitFor([&] { return changed > beforePhotoChange; }));
	desktopBackgroundFill(&wide, 12, 8);
	assert(widePixels[12] == qRgb(91, 17, 163)); // Invalidates the placement cache.
	QImage striped(8, 4, QImage::Format_RGB32);
	striped.fill(qRgb(0, 255, 0));
	for (int y = 0; y < 4; ++y) {
		striped.setPixel(0, y, qRgb(255, 0, 0));
		striped.setPixel(7, y, qRgb(0, 0, 255));
	}
	const int beforeStripes = changed;
	assert(striped.save(photoPath));
	assert(waitFor([&] { return changed > beforeStripes; }));
	uint32_t cropped[16] = {};
	pl_canvas crop = {cropped, 4, 0, 0, 4, 4};
	desktopWallpaperFill(&crop, 0, "fill", 0, 0, 3, 3);
	for (uint32_t pixel : cropped) {
		assert(pixel == qRgb(0, 255, 0));
	}
	desktopWallpaperFill(&crop, 0, "stretch", 0, 0, 3, 3);
	assert(cropped[0] != qRgb(0, 255, 0) && cropped[3] != qRgb(0, 255, 0));
	assert(QFile::remove(photoPath));
	assert(waitFor([] { return desktopWallpaperCount() == 0; }));
	assert(desktopWallpaperFind(photoId) == -1);
	// Switching back to a built-in pattern remembers the wallpaper choice.
	panel.showTab(1);
	QKeyEvent choosePattern(QEvent::KeyPress, Qt::Key_P, Qt::NoModifier, "Pewter");
	QApplication::sendEvent(&panel, &choosePattern);
	assert(pl_setting("background", selected, sizeof(selected)) && QString::fromUtf8(selected) == "pattern");
	assert(pl_setting("wallpaper", selected, sizeof(selected)) && QString::fromUtf8(selected) == photoId);
	QTextStream(stdout) << "ok: discovery, selection persistence, 112x112 preview, tiled pixels, replacement, IDs, errors, removal and watchers\n";
	QTextStream(stdout) << "ok: separate wallpaper folder, four placement modes, per-monitor geometry, switching and cache invalidation\n";
}
