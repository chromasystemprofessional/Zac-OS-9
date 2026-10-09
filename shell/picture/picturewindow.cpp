#include "picturewindow.h"

#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QStyle>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWindow>
#include <algorithm>
#include <cmath>

#include "alert.h"

/* zacos9-wm's menu bar and the frame it draws around the window
 * (compositor/src/decor.h), plus a little room so the frame isn't flush with
 * the screen's edges. */
static constexpr int MenuBarHeight = 20, FrameWidth = 6 + 6, FrameHeight = 22 + 6, Margin = 8;
static constexpr int MinWidth = 220;
static const double Steps[] = { 1.0 / 16, 1.0 / 8, 1.0 / 4, 1.0 / 3, 1.0 / 2, 2.0 / 3, 3.0 / 4, 1, 1.5, 2, 3,
	4, 6, 8, 12, 16 };

PictureView::PictureView(QWidget *parent) : QWidget(parent) {
	setAutoFillBackground(false);
}

void PictureView::setImage(const QImage &image) {
	m_image = image;
	rescale();
}

void PictureView::setScale(double scale) {
	m_scale = scale;
	rescale();
}

void PictureView::rescale() {
	const QSize size(std::max(1, int(std::lround(m_image.width() * m_scale))),
		std::max(1, int(std::lround(m_image.height() * m_scale))));
	// Reductions are smoothed once; enlargements keep every pixel square and are drawn as needed.
	m_scaled = m_scale < 1 && !m_image.isNull()
		? QPixmap::fromImage(m_image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation))
		: QPixmap();
	setMinimumSize(m_image.isNull() ? QSize(0, 0) : size);
	update();
}

QRect PictureView::pictureRect() const {
	const QSize size = minimumSize();
	return QRect(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
}

void PictureView::paintEvent(QPaintEvent *event) {
	QPainter painter(this);
	painter.fillRect(event->rect(), QColor(0xCC, 0xCC, 0xCC));
	if (m_image.isNull()) {
		return;
	}
	const QRect r = pictureRect();
	// Transparent pictures show on white, as on paper.
	painter.fillRect(r, Qt::white);
	if (!m_scaled.isNull()) {
		painter.drawPixmap(r.topLeft(), m_scaled);
		return;
	}
	const QRect exposed = event->rect() & r;
	if (exposed.isEmpty()) {
		return;
	}
	// Only the picture's exposed part, so a large picture at 1600% stays quick to draw.
	const QRectF source((exposed.left() - r.left()) / m_scale, (exposed.top() - r.top()) / m_scale,
		exposed.width() / m_scale, exposed.height() / m_scale);
	const QRect pixels = source.toAlignedRect() & m_image.rect();
	const QRectF target(r.left() + pixels.left() * m_scale, r.top() + pixels.top() * m_scale,
		pixels.width() * m_scale, pixels.height() * m_scale);
	painter.drawImage(target, m_image, pixels);
}

PictureWindow::PictureWindow(QWidget *parent) : QMainWindow(parent) {
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle("Picture Viewer");
	m_view = new PictureView;
	m_scroll = new QScrollArea;
	m_scroll->setFrameShape(QFrame::NoFrame);
	m_scroll->setWidgetResizable(true);
	m_scroll->setWidget(m_view);
	m_scroll->viewport()->installEventFilter(this);

	m_bar = new QWidget;
	m_bar->setFixedHeight(24);
	auto *barLayout = new QHBoxLayout(m_bar);
	barLayout->setContentsMargins(6, 2, 18, 2); // clear of the frame's grow box
	barLayout->setSpacing(4);
	m_out = new QPushButton(QString::fromUtf8("\u2212"));
	m_in = new QPushButton("+");
	for (QPushButton *button : { m_out, m_in }) {
		button->setFixedSize(26, 20);
		button->setFocusPolicy(Qt::NoFocus);
	}
	m_out->setToolTip("Zoom Out");
	m_in->setToolTip("Zoom In");
	m_zoom = new QLabel;
	m_zoom->setAlignment(Qt::AlignCenter);
	m_zoom->setMinimumWidth(44);
	auto *dimensions = new QLabel;
	dimensions->setObjectName("dimensions");
	barLayout->addWidget(m_out);
	barLayout->addWidget(m_zoom);
	barLayout->addWidget(m_in);
	barLayout->addStretch();
	barLayout->addWidget(dimensions);
	connect(m_out, &QPushButton::clicked, this, [this] { zoomOut(); });
	connect(m_in, &QPushButton::clicked, this, [this] { zoomIn(); });

	auto *central = new QWidget;
	auto *layout = new QVBoxLayout(central);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(m_scroll, 1);
	layout->addWidget(m_bar);
	setCentralWidget(central);

	// Exported to ZacOS 9's menu bar when it shows applications' menus.
	QMenu *file = menuBar()->addMenu("File");
	QAction *openAction = file->addAction("Open...", QKeySequence::Open, this, [this] { chooseFile(); });
	openAction->setMenuRole(QAction::NoRole);
	file->addSeparator();
	file->addAction("Close Window", QKeySequence::Close, this, [this] { close(); });
	QMenu *view = menuBar()->addMenu("View");
	QAction *in = view->addAction("Zoom In", this, [this] { zoomIn(); });
	in->setShortcuts({ QKeySequence::ZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal) });
	view->addAction("Zoom Out", QKeySequence::ZoomOut, this, [this] { zoomOut(); });
	view->addSeparator();
	view->addAction("Actual Size", QKeySequence(Qt::CTRL | Qt::Key_0), this, [this] { actualSize(); });
	view->addAction("Fit to Screen", QKeySequence(Qt::CTRL | Qt::Key_9), this, [this] { fitToScreen(); });
	zoomTo(1, QPoint(-1, -1), false);
}

double PictureWindow::scale() const {
	return m_view->scale();
}

QSize PictureWindow::available() const {
	const QScreen *s = windowHandle() && windowHandle()->screen() ? windowHandle()->screen() : screen();
	const QSize screenSize = s ? s->availableGeometry().size() : QSize(1024, 768);
	return QSize(screenSize.width() - FrameWidth - 2 * Margin,
		screenSize.height() - MenuBarHeight - FrameHeight - 2 * Margin);
}

/* Room the window keeps apart from the picture: the zoom bar, and the menu bar when it's in the window. */
static int chromeHeight(const QMainWindow *window, const QWidget *bar) {
	const QMenuBar *menus = window->menuBar();
	const int menus_h = menus->isNativeMenuBar() ? 0 : menus->sizeHint().height();
	return bar->maximumHeight() + menus_h;
}

double PictureWindow::fitScale() const {
	const QImage &image = m_view->image();
	if (image.isNull()) {
		return 1;
	}
	const QSize room = available();
	const double fit = std::min(double(room.width()) / image.width(),
		double(room.height() - chromeHeight(this, m_bar)) / image.height());
	return std::clamp(fit, MinScale, MaxScale);
}

QSize PictureWindow::sizeFor(double scale) const {
	const QImage &image = m_view->image();
	const QSize room = available();
	const int chrome = chromeHeight(this, m_bar);
	const int extent = style()->pixelMetric(QStyle::PM_ScrollBarExtent);
	int w = int(std::lround(image.width() * scale)), h = int(std::lround(image.height() * scale));
	const bool tooWide = w > room.width(), tooTall = h + chrome > room.height();
	// A scroll bar along one side takes room the picture would otherwise have on the other.
	if (tooTall) {
		w += extent;
	}
	if (tooWide) {
		h += extent;
	}
	return QSize(std::clamp(w, MinWidth, std::max(MinWidth, room.width())),
		std::clamp(h + chrome, chrome + 40, std::max(chrome + 40, room.height())));
}

bool PictureWindow::open(const QString &path, QString *error) {
	QImageReader reader(path);
	reader.setAutoTransform(true); // a camera's EXIF orientation
	const QImage image = reader.read();
	if (image.isNull()) {
		if (error) {
			*error = reader.errorString();
		}
		return false;
	}
	m_path = QFileInfo(path).absoluteFilePath();
	setWindowTitle(QFileInfo(path).fileName());
	setWindowFilePath(m_path);
	m_view->setImage(image);
	if (auto *dimensions = m_bar->findChild<QLabel *>("dimensions")) {
		dimensions->setText(QString::fromUtf8("%1 \u00d7 %2").arg(image.width()).arg(image.height()));
	}
	// Fitted to the screen, but never enlarged past actual size to get there.
	zoomTo(std::min(1.0, fitScale()));
	return true;
}

QString PictureWindow::zoomText() const {
	return QString("%1%").arg(std::lround(scale() * 100));
}

void PictureWindow::zoomTo(double target, QPoint anchor, bool resizeWindow) {
	target = std::clamp(target, MinScale, MaxScale);
	QScrollBar *hbar = m_scroll->horizontalScrollBar(), *vbar = m_scroll->verticalScrollBar();
	const bool centred = anchor.x() < 0;
	if (centred) {
		anchor = QPoint(m_scroll->viewport()->width() / 2, m_scroll->viewport()->height() / 2);
	}
	// The picture point under the anchor, in the picture's own pixels.
	const QRect before = m_view->pictureRect();
	const QPointF point((hbar->value() + anchor.x() - before.left()) / scale(),
		(vbar->value() + anchor.y() - before.top()) / scale());
	m_view->setScale(target);
	if (resizeWindow && !m_view->image().isNull()) {
		resize(sizeFor(target));
	}
	// Let the scroll area take in the new sizes before scrolling it.
	QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
	if (QLayout *l = centralWidget()->layout()) {
		l->activate();
	}
	QApplication::sendPostedEvents(m_scroll, QEvent::LayoutRequest);
	const QRect after = m_view->pictureRect();
	if (centred) {
		// The window may have grown or shrunk: the picture's middle goes to the new middle.
		anchor = QPoint(m_scroll->viewport()->width() / 2, m_scroll->viewport()->height() / 2);
	}
	hbar->setValue(int(std::lround(point.x() * target + after.left() - anchor.x())));
	vbar->setValue(int(std::lround(point.y() * target + after.top() - anchor.y())));
	m_zoom->setText(zoomText());
	m_out->setEnabled(target > MinScale + 1e-9);
	m_in->setEnabled(target < MaxScale - 1e-9);
	m_view->setCursor(hbar->maximum() > 0 || vbar->maximum() > 0 ? Qt::OpenHandCursor : Qt::ArrowCursor);
}

void PictureWindow::zoomIn() {
	for (double step : Steps) {
		if (step > scale() * 1.001) {
			zoomTo(step);
			return;
		}
	}
}

void PictureWindow::zoomOut() {
	for (auto it = std::rbegin(Steps); it != std::rend(Steps); ++it) {
		if (*it < scale() / 1.001) {
			zoomTo(*it);
			return;
		}
	}
}

void PictureWindow::actualSize() {
	zoomTo(1);
}

void PictureWindow::fitToScreen() {
	zoomTo(fitScale());
}

bool PictureWindow::eventFilter(QObject *watched, QEvent *event) {
	if (watched != m_scroll->viewport()) {
		return QMainWindow::eventFilter(watched, event);
	}
	switch (event->type()) {
	case QEvent::Wheel: {
		auto *wheel = static_cast<QWheelEvent *>(event);
		if (wheel->modifiers() & Qt::ControlModifier) {
			const QPoint at = wheel->position().toPoint();
			const int delta = wheel->angleDelta().y();
			if (delta > 0) {
				for (double step : Steps) {
					if (step > scale() * 1.001) {
						zoomTo(step, at);
						break;
					}
				}
			} else if (delta < 0) {
				for (auto it = std::rbegin(Steps); it != std::rend(Steps); ++it) {
					if (*it < scale() / 1.001) {
						zoomTo(*it, at);
						break;
					}
				}
			}
			return true;
		}
		break;
	}
	case QEvent::MouseButtonPress: {
		// Drag a picture larger than the window to move it, as with a hand.
		auto *mouse = static_cast<QMouseEvent *>(event);
		if (mouse->button() == Qt::LeftButton && m_view->cursor().shape() == Qt::OpenHandCursor) {
			m_grab = mouse->globalPosition().toPoint();
			m_view->setCursor(Qt::ClosedHandCursor);
			return true;
		}
		break;
	}
	case QEvent::MouseMove: {
		auto *mouse = static_cast<QMouseEvent *>(event);
		if (m_view->cursor().shape() == Qt::ClosedHandCursor) {
			const QPoint now = mouse->globalPosition().toPoint(), moved = now - m_grab;
			m_grab = now;
			m_scroll->horizontalScrollBar()->setValue(m_scroll->horizontalScrollBar()->value() - moved.x());
			m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->value() - moved.y());
			return true;
		}
		break;
	}
	case QEvent::MouseButtonRelease:
		if (m_view->cursor().shape() == Qt::ClosedHandCursor) {
			m_view->setCursor(Qt::OpenHandCursor);
			return true;
		}
		break;
	default:
		break;
	}
	return QMainWindow::eventFilter(watched, event);
}

bool PictureWindow::chooseFile() {
	QStringList filters;
	for (const QByteArray &suffix : QImageReader::supportedImageFormats()) {
		filters << "*." + QString::fromLatin1(suffix);
	}
	const QString start = m_path.isEmpty()
		? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
		: QFileInfo(m_path).absolutePath();
	const QString path = QFileDialog::getOpenFileName(this, "Open", start,
		QString("Pictures (%1)").arg(filters.join(' ')));
	if (path.isEmpty()) {
		return false;
	}
	PictureWindow *target = m_path.isEmpty() ? this : new PictureWindow;
	QString error;
	if (!target->open(path, &error)) {
		Alert::ask(QString::fromUtf8("\u201c%1\u201d could not be opened. %2").arg(QFileInfo(path).fileName(), error),
			"OK", QString());
		if (target != this) {
			target->deleteLater();
		}
		return false;
	}
	target->show();
	return true;
}
