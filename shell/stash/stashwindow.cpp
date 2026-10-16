#include "stashwindow.h"

#include <QApplication>
#include <QClipboard>
#include <QDrag>
#include <QDragEnterEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMimeData>
#include <QPainter>
#include <QScrollBar>
#include <QVBoxLayout>

/* The page in view; drag from it to take a copy, drop on it to add. */
class ClipView : public QWidget {
public:
	explicit ClipView(StashWindow *window) : QWidget(window), m_window(window) {
		setAcceptDrops(true);
		setMinimumSize(320, 220);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	}

protected:
	void paintEvent(QPaintEvent *) override {
		QPainter p(this);
		p.fillRect(rect(), m_dropping ? palette().highlight().color().lighter(170) : Qt::white);
		p.setPen(Qt::black);
		p.drawRect(rect().adjusted(0, 0, -1, -1));
		const QRect area = rect().adjusted(8, 8, -8, -8);
		Stash &stash = m_window->stash();
		if (stash.count() == 0) {
			p.setPen(Qt::darkGray);
			p.drawText(area, Qt::AlignCenter | Qt::TextWordWrap,
				"The Stash is empty.\nPaste or drag text and pictures here.");
			return;
		}
		const Clip &clip = stash.clip(m_window->currentPage());
		if (clip.kind == Clip::Picture) {
			/* Shrunk to fit, never enlarged, in the middle. */
			QSize size = clip.image.size();
			if (size.width() > area.width() || size.height() > area.height()) {
				size.scale(area.size(), Qt::KeepAspectRatio);
			}
			QRect r(QPoint(), size);
			r.moveCenter(area.center());
			p.setRenderHint(QPainter::SmoothPixmapTransform);
			p.drawImage(r, clip.image);
		} else {
			p.setClipRect(area);
			p.drawText(area, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, clip.text);
		}
	}

	void mousePressEvent(QMouseEvent *event) override {
		m_press = event->button() == Qt::LeftButton ? event->pos() : QPoint(-1, -1);
	}

	void mouseMoveEvent(QMouseEvent *event) override {
		if (m_press.x() < 0 || !(event->buttons() & Qt::LeftButton) ||
				(event->pos() - m_press).manhattanLength() < QApplication::startDragDistance() ||
				m_window->stash().count() == 0) {
			return;
		}
		m_press = QPoint(-1, -1);
		auto *drag = new QDrag(this);
		drag->setMimeData(m_window->stash().mimeData(m_window->currentPage()));
		const Clip &clip = m_window->stash().clip(m_window->currentPage());
		if (clip.kind == Clip::Picture) {
			drag->setPixmap(QPixmap::fromImage(clip.image.scaled(64, 64, Qt::KeepAspectRatio,
				Qt::SmoothTransformation)));
		}
		drag->exec(Qt::CopyAction);
	}

	bool accepts(const QDropEvent *event) const {
		const QMimeData *data = event->mimeData();
		return event->source() != this &&
			(data->hasUrls() || data->hasImage() || data->hasText());
	}

	void dragEnterEvent(QDragEnterEvent *event) override {
		if (accepts(event)) {
			event->setDropAction(Qt::CopyAction);
			event->accept();
			m_dropping = true;
			update();
		}
	}

	void dragLeaveEvent(QDragLeaveEvent *) override {
		m_dropping = false;
		update();
	}

	void dropEvent(QDropEvent *event) override {
		m_dropping = false;
		update();
		if (accepts(event) && m_window->add(event->mimeData())) {
			event->setDropAction(Qt::CopyAction);
			event->accept();
		}
	}

private:
	StashWindow *m_window;
	QPoint m_press{-1, -1};
	bool m_dropping = false;
};

namespace {

/* Sizes as the Finder gave them: whole K, rounded up. */
QString kilobytes(qint64 bytes) {
	return QString("%1K").arg(qMax<qint64>(1, (bytes + 1023) / 1024));
}

} // namespace

StashWindow::StashWindow(const QString &dir) : m_stash(dir) {
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle("Stash");
	auto *central = new QWidget(this);
	auto *column = new QVBoxLayout(central);
	column->setContentsMargins(8, 8, 8, 6);
	column->setSpacing(6);
	m_view = new ClipView(this);
	column->addWidget(m_view, 1);
	m_scroll = new QScrollBar(Qt::Horizontal, central);
	m_scroll->setPageStep(1);
	column->addWidget(m_scroll);
	auto *strip = new QHBoxLayout;
	m_number = new QLabel(central);
	m_info = new QLabel(central);
	m_info->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	strip->addWidget(m_number);
	strip->addWidget(m_info, 1);
	column->addLayout(strip);
	setCentralWidget(central);
	connect(m_scroll, &QScrollBar::valueChanged, this, [this](int value) {
		if (value != m_page) {
			showPage(value);
		}
	});

	QMenu *file = menuBar()->addMenu("File");
	file->addAction("Close", QKeySequence::Close, this, &QWidget::close);
	file->addSeparator();
	file->addAction("Quit", QKeySequence::Quit, qApp, &QApplication::quit)
		->setMenuRole(QAction::NoRole);
	QMenu *edit = menuBar()->addMenu("Edit");
	QAction *undoAction = edit->addAction("Undo", QKeySequence::Undo, this, &StashWindow::undo);
	edit->addSeparator();
	QAction *cutAction = edit->addAction("Cut", QKeySequence::Cut, this, &StashWindow::cut);
	QAction *copyAction = edit->addAction("Copy", QKeySequence::Copy, this, &StashWindow::copy);
	edit->addAction("Paste", QKeySequence::Paste, this, &StashWindow::paste);
	QAction *clearAction = edit->addAction("Clear", this, &StashWindow::clear);
	clearAction->setShortcut(QKeySequence::Delete);
	connect(edit, &QMenu::aboutToShow, this, [=] {
		const bool any = m_stash.count() > 0;
		undoAction->setEnabled(m_last.kind != Change::None);
		cutAction->setEnabled(any);
		copyAction->setEnabled(any);
		clearAction->setEnabled(any);
	});

	resize(400, 340);
	refresh();
}

void StashWindow::showPage(int page) {
	m_page = qBound(0, page, qMax(0, m_stash.count() - 1));
	refresh();
}

void StashWindow::refresh() {
	const int count = m_stash.count();
	m_page = qBound(0, m_page, qMax(0, count - 1));
	m_scroll->blockSignals(true);
	m_scroll->setRange(0, qMax(0, count - 1));
	m_scroll->setValue(m_page);
	m_scroll->setEnabled(count > 1);
	m_scroll->blockSignals(false);
	m_number->setText(count ? QString("%1 of %2").arg(m_page + 1).arg(count) : QString());
	m_info->setText(infoText());
	m_view->update();
}

QString StashWindow::infoText() const {
	if (m_stash.count() == 0) {
		return {};
	}
	const Clip &clip = m_stash.clip(m_page);
	if (clip.kind == Clip::Picture) {
		return QString("Picture, %1, %2 × %3 pixels").arg(kilobytes(clip.bytes))
			.arg(clip.image.width()).arg(clip.image.height());
	}
	const int chars = clip.text.size();
	return QString("Text, %1, %2 character%3").arg(kilobytes(clip.bytes)).arg(chars)
		.arg(chars == 1 ? "" : "s");
}

bool StashWindow::add(const QMimeData *data) {
	const int at = m_stash.count() ? m_page + 1 : 0;
	const int added = m_stash.insertMime(at, data);
	if (added == 0) {
		QApplication::beep();
		return false;
	}
	m_last = Change{Change::Added, at, added, Clip()};
	m_page = at;
	refresh();
	return true;
}

void StashWindow::copy() {
	if (m_stash.count()) {
		QApplication::clipboard()->setMimeData(m_stash.mimeData(m_page));
	}
}

void StashWindow::paste() {
	add(QApplication::clipboard()->mimeData());
}

void StashWindow::clear() {
	if (m_stash.count() == 0) {
		return;
	}
	const Clip clip = m_stash.clip(m_page);
	if (m_stash.remove(m_page)) {
		m_last = Change{Change::Removed, m_page, 1, clip};
	}
	refresh();
}

void StashWindow::cut() {
	copy();
	clear();
}

void StashWindow::undo() {
	const Change change = m_last;
	m_last = Change();
	if (change.kind == Change::Added) {
		for (int i = 0; i < change.count; i++) {
			m_stash.remove(change.at);
		}
		m_page = change.at - 1;
	} else if (change.kind == Change::Removed) {
		m_stash.insert(change.at, change.clip);
		m_page = change.at;
	}
	refresh();
}

void StashWindow::keyPressEvent(QKeyEvent *event) {
	switch (event->key()) {
	case Qt::Key_Left:
	case Qt::Key_PageUp:
		showPage(m_page - 1);
		break;
	case Qt::Key_Right:
	case Qt::Key_PageDown:
		showPage(m_page + 1);
		break;
	case Qt::Key_Home:
		showPage(0);
		break;
	case Qt::Key_End:
		showPage(m_stash.count() - 1);
		break;
	default:
		QMainWindow::keyPressEvent(event);
	}
}
