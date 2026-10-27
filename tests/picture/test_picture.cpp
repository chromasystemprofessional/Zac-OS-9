/* Picture Viewer: fitting to the screen, zooming, drawing and panning, offscreen. */
#include "picturewindow.h"

#include <QApplication>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTextStream>
#include <QWheelEvent>
#include <cmath>

static int failures;
static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	failures += !ok;
}

/* Four coloured quarters, so where the picture is drawn can be seen. */
static QImage quarters(int w, int h) {
	QImage image(w, h, QImage::Format_RGB32);
	QPainter p(&image);
	p.fillRect(0, 0, w / 2, h / 2, QColor(0xFF0000));
	p.fillRect(w / 2, 0, w - w / 2, h / 2, QColor(0x00FF00));
	p.fillRect(0, h / 2, w / 2, h - h / 2, QColor(0x0000FF));
	p.fillRect(w / 2, h / 2, w - w / 2, h - h / 2, QColor(0xFFFF00));
	return image;
}

static void settle() {
	for (int i = 0; i < 5; ++i) {
		QApplication::processEvents();
	}
}

int main(int argc, char **argv) {
	QTemporaryDir dir;
	qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
	qputenv("XDG_RUNTIME_DIR", dir.path().toUtf8());
	QApplication app(argc, argv);
	const QSize screen = app.primaryScreen()->availableGeometry().size();
	// zacos9-wm's menu bar (20) and window frame (12 × 28), 8 px clear of each edge.
	const QSize room(screen.width() - 12 - 16, screen.height() - 20 - 28 - 16);

	const QString big = dir.path() + "/big photo.png", small = dir.path() + "/small.png";
	check(quarters(3000, 2000).save(big) && quarters(100, 80).save(small), "test pictures written");
	QFile junk(dir.path() + "/junk.png");
	junk.open(QIODevice::WriteOnly);
	junk.write("not a picture");
	junk.close();

	{
		PictureWindow window;
		QString error;
		check(!window.open(junk.fileName(), &error) && !error.isEmpty(), "a broken file reports why it can't open");
	}

	auto *window = new PictureWindow;
	check(window->open(big), "a large picture opens");
	window->show();
	settle();
	const double fit = window->fitScale();
	check(fit < 1 && std::abs(window->scale() - fit) < 1e-9, "a picture larger than the screen opens fitted to it");
	check(window->size().width() <= room.width() && window->size().height() <= room.height(),
		QString("the window fits on the screen (%1×%2 in %3×%4)").arg(window->width()).arg(window->height())
			.arg(room.width()).arg(room.height()));
	QScrollArea *scroll = window->scrollArea();
	check(scroll->horizontalScrollBar()->maximum() == 0 && scroll->verticalScrollBar()->maximum() == 0,
		"fitted, the whole picture shows without scrolling");
	check(window->windowTitle() == "big photo.png", "the window is titled with the file's name");
	check(window->zoomText() == QString("%1%").arg(std::lround(fit * 100)), "the zoom bar shows the fitted percentage");
	auto *dimensions = window->findChild<QLabel *>("dimensions");
	check(dimensions && dimensions->text() == QString::fromUtf8("3000 \u00d7 2000"), "and the picture's size in pixels");

	QImage shot = scroll->viewport()->grab().toImage();
	const QPoint mid(shot.width() / 2, shot.height() / 2);
	check(shot.pixelColor(mid.x() - 20, mid.y() - 20) == QColor(0xFF0000) &&
		shot.pixelColor(mid.x() + 20, mid.y() + 20) == QColor(0xFFFF00), "the reduced picture is drawn centred");

	window->zoomInButton()->click();
	settle();
	check(window->scale() > fit && window->zoomText() != QString("%1%").arg(std::lround(fit * 100)),
		"the + button zooms in a step (" + window->zoomText() + ")");
	check(scroll->horizontalScrollBar()->maximum() > 0, "zoomed in past the screen, the picture scrolls");
	window->actualSize();
	settle();
	check(window->zoomText() == "100%", "Actual Size shows every pixel");
	shot = scroll->viewport()->grab().toImage();
	check(shot.pixelColor(shot.width() / 2 - 3, shot.height() / 2 - 3) == QColor(0xFF0000) &&
		shot.pixelColor(shot.width() / 2 + 3, shot.height() / 2 + 3) == QColor(0xFFFF00),
		"zooming keeps the middle of the picture in the middle of the window");
	window->zoomIn();
	check(window->zoomText() == "150%", "Zoom In from 100% goes to 150%");
	window->zoomOut();
	window->zoomOut();
	check(window->zoomText() == "75%", "Zoom Out steps back through 100% to 75%");

	// Drag to move the picture when it's larger than the window.
	window->actualSize();
	settle();
	QScrollBar *h = scroll->horizontalScrollBar();
	const int before = h->value();
	QWidget *viewport = scroll->viewport();
	const QPointF at(100, 100);
	QMouseEvent press(QEvent::MouseButtonPress, at, viewport->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton,
		Qt::NoModifier);
	QApplication::sendEvent(viewport, &press);
	const QPointF to(60, 100);
	QMouseEvent move(QEvent::MouseMove, to, viewport->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(viewport, &move);
	QMouseEvent release(QEvent::MouseButtonRelease, to, viewport->mapToGlobal(to), Qt::LeftButton, Qt::NoButton,
		Qt::NoModifier);
	QApplication::sendEvent(viewport, &release);
	check(h->value() == before + 40, "dragging the picture moves it like a hand");

	// ⌘ and the scroll wheel (⌘ is Ctrl to applications).
	QWheelEvent wheelIn(QPointF(50, 50), viewport->mapToGlobal(QPointF(50, 50)), QPoint(), QPoint(0, 120),
		Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
	QApplication::sendEvent(viewport, &wheelIn);
	check(window->zoomText() == "150%", "⌘ and the scroll wheel zoom in");
	QWheelEvent wheelOut(QPointF(50, 50), viewport->mapToGlobal(QPointF(50, 50)), QPoint(), QPoint(0, -120),
		Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
	QApplication::sendEvent(viewport, &wheelOut);
	check(window->zoomText() == "100%", "and out");

	for (int i = 0; i < 20; ++i) {
		window->zoomIn();
	}
	check(window->zoomText() == "1600%" && !window->zoomInButton()->isEnabled() && window->zoomOutButton()->isEnabled(),
		"zooming in stops at 1600%, with + dimmed");
	window->fitToScreen();
	settle();
	check(std::abs(window->scale() - fit) < 1e-9 && scroll->verticalScrollBar()->maximum() == 0,
		"Fit to Screen brings the whole picture back");
	window->close();

	auto *little = new PictureWindow;
	check(little->open(small), "a small picture opens");
	little->show();
	settle();
	check(little->zoomText() == "100%", "a picture smaller than the screen opens at actual size, not enlarged");
	shot = little->scrollArea()->viewport()->grab().toImage();
	const QRect drawn(QPoint((shot.width() - 100) / 2, (shot.height() - 80) / 2), QSize(100, 80));
	check(shot.pixelColor(drawn.left() + 1, drawn.top() + 1) == QColor(0xFF0000) &&
		shot.pixelColor(drawn.right() - 1, drawn.bottom() - 1) == QColor(0xFFFF00) &&
		shot.pixelColor(drawn.left() - 2, drawn.top() + 1) == QColor(0xCCCCCC),
		"centred on gray at its own size");
	little->fitToScreen();
	settle();
	check(little->scale() > 1 && little->scrollArea()->verticalScrollBar()->maximum() == 0,
		QString("Fit to Screen enlarges a small picture to fill the screen (%1)").arg(little->zoomText()));
	little->close();
	return failures ? 1 : 0;
}
