#include "folderwindow.h"

#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QProcess>
#include <QSet>
#include <QStorageInfo>

#include "fileops.h"

/* Layout. The item-count header matches HIG figure 2-24; the icon grid
 * spacing is not in the HIG (TODO: measure). */
static constexpr int HEADER_H = 21;      /* white row, #DDD, #AAA shade */
static constexpr int HEADER_BASELINE = 13;
static constexpr int CONTENT_Y = HEADER_H + 1; /* below the separator line */
static constexpr int CELL_W = 80, CELL_H = 64;
static constexpr int MARGIN = 8;
static constexpr int ARROW_STEP = 16;
static constexpr int REPEAT_FIRST_MS = 300, REPEAT_MS = 50;

static QHash<QString, FolderWindow *> &openWindows() {
	static QHash<QString, FolderWindow *> windows;
	return windows;
}

FolderWindow *FolderWindow::open(const QString &path) {
	const QString key = QDir(path).absolutePath();
	if (FolderWindow *w = openWindows().value(key)) {
		w->show();
		w->raise();
		w->activateWindow();
		return w;
	}
	auto *w = new FolderWindow(key);
	openWindows().insert(key, w);
	w->resize(420, 280);
	w->show();
	return w;
}

void FolderWindow::reloadAll(const QString &folder) {
	if (FolderWindow *w = openWindows().value(QDir(folder).absolutePath())) {
		w->reload();
	}
}

FolderWindow::FolderWindow(const QString &path) : m_path(path) {
	setAttribute(Qt::WA_DeleteOnClose);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setWindowTitle(displayName(path));
	setAcceptDrops(true);
	m_repeat.callOnTimeout([this] { scrollStep(); });
	/* Other programs change folders too; follow them. */
	m_watcher.addPath(path);
	QObject::connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
		[this](const QString &) { reload(); });
	reload();
}

FolderWindow::~FolderWindow() {
	openWindows().remove(m_path);
	Finder::instance().viewClosed(this);
}

void FolderWindow::closeEvent(QCloseEvent *e) {
	Finder::instance().viewClosed(this);
	QWidget::closeEvent(e);
}

void FolderWindow::changeEvent(QEvent *e) {
	if (e->type() == QEvent::ActivationChange && isActiveWindow()) {
		Finder::instance().setFront(this);
	}
	QWidget::changeEvent(e);
}

/* Re-read the folder, keeping the selection by name. */
void FolderWindow::reload() {
	if (!QDir(m_path).exists()) {
		/* Trashed, deleted or moved away: its window goes with it. */
		QTimer::singleShot(0, this, [this] { close(); });
		return;
	}
	QSet<QString> selected;
	for (auto &item : m_items) {
		if (item->selected) {
			selected.insert(item->name);
		}
	}
	m_pressItem = nullptr;
	m_items = listFolder(m_path);
	for (auto &item : m_items) {
		item->selected = selected.contains(item->name);
	}
	layoutIcons();
	update();
}

std::vector<Item *> FolderWindow::selectedItems() {
	std::vector<Item *> out;
	for (auto &item : m_items) {
		if (item->selected) {
			out.push_back(item.get());
		}
	}
	return out;
}

void FolderWindow::selectByName(const QString &name) {
	for (auto &item : m_items) {
		item->selected = item->name == name;
	}
	update();
}

void FolderWindow::layoutIcons() {
	int viewW = width() - SB_WIDTH + 1;
	int cols = std::max(1, (viewW - 2 * MARGIN) / CELL_W);
	int i = 0;
	for (auto &item : m_items) {
		int col = i % cols, row = i / cols;
		item->pos = QPoint(MARGIN + col * CELL_W + (CELL_W - PL_ICON_LARGE) / 2,
			MARGIN + row * CELL_H);
		i++;
	}
	int rows = (static_cast<int>(m_items.size()) + cols - 1) / cols;
	m_contentHeight = 2 * MARGIN + rows * CELL_H;
	scrollTo(m_scroll);
}

int FolderWindow::viewHeight() const {
	return height() - CONTENT_Y - (SB_WIDTH - 1);
}

int FolderWindow::scrollMax() const {
	return std::max(0, m_contentHeight - viewHeight());
}

void FolderWindow::scrollTo(int y) {
	y = std::clamp(y, 0, scrollMax());
	if (y != m_scroll) {
		m_scroll = y;
		update();
	}
}

pl_scrollbar FolderWindow::verticalBar() const {
	pl_scrollbar sb{};
	sb.vertical = true;
	sb.length = height() - (SB_WIDTH - 1) - HEADER_H + 1;
	sb.enabled = scrollMax() > 0;
	int range = sb_thumb_range(sb.length);
	sb.thumb = sb.enabled ? m_scroll * range / scrollMax() : 0;
	return sb;
}

QString FolderWindow::headerText() const {
	const int n = static_cast<int>(m_items.size());
	QString items = n == 1 ? QStringLiteral("1 item") : QStringLiteral("%1 items").arg(n);
	const qint64 free = QStorageInfo(m_path).bytesAvailable();
	QString avail;
	if (free >= 1024LL * 1024 * 1024) {
		avail = QString::number(free / (1024.0 * 1024 * 1024), 'f', 1) + " GB";
	} else {
		avail = QString::number(free / (1024 * 1024)) + " MB";
	}
	return items + ", " + avail + " available";
}

void FolderWindow::paintEvent(QPaintEvent *) {
	const int W = width(), H = height();
	Pixels px(W, H);
	pl_canvas *c = &px.c;

	/* Item-count header (HIG figure 2-24). */
	pl_hline(c, 0, W - 1, 0, C_WHITE);
	pl_fill(c, 0, 1, W - 1, HEADER_H - 2, GRAY(0xD));
	pl_vline(c, 0, 1, HEADER_H - 2, C_WHITE);
	pl_hline(c, 0, W - 1, HEADER_H - 1, GRAY(0xA));
	Text header(headerText(), W - 20, PL_FONT_VIEWS);
	pl_text(c, header.t, (W - header.inkWidth()) / 2, HEADER_BASELINE, C_BLACK);
	pl_hline(c, 0, W - 1, HEADER_H, C_BLACK);

	/* Icons, through a canvas whose origin follows the scroll position. */
	const int viewW = W - SB_WIDTH + 1, viewH = viewHeight();
	pl_canvas content = {
		c->px + CONTENT_Y * c->stride, c->stride, 0, m_scroll, viewW, std::max(0, viewH),
	};
	pl_fill(&content, 0, m_scroll, viewW - 1, m_scroll + viewH - 1, C_WHITE);
	for (auto &item : m_items) {
		if (item->pos.y() + CELL_H >= m_scroll && item->pos.y() <= m_scroll + viewH) {
			paintIconItem(&content, *item, item->pos.x(), item->pos.y(), false);
		}
	}

	/* Scroll bars share their outer lines with the window frame. The
	 * compositor draws the resize box in the corner. */
	pl_scrollbar v = verticalBar();
	pl_scrollbar_paint(c, W - SB_WIDTH + 1, HEADER_H, &v, PL_ACCENT_DEFAULT);
	pl_scrollbar h{};
	h.length = W - SB_WIDTH + 3;
	h.enabled = false; /* icons flow to the window width */
	pl_scrollbar_paint(c, -1, H - SB_WIDTH + 1, &h, PL_ACCENT_DEFAULT);

	QPainter p(this);
	px.blit(p);
}

void FolderWindow::resizeEvent(QResizeEvent *) {
	layoutIcons();
}

Item *FolderWindow::itemAt(QPoint pos) {
	QPoint p(pos.x(), pos.y() - CONTENT_Y + m_scroll);
	for (auto it = m_items.rbegin(); it != m_items.rend(); ++it) {
		if (iconItemContains(**it, (*it)->pos.x(), (*it)->pos.y(), p)) {
			return it->get();
		}
	}
	return nullptr;
}

void FolderWindow::scrollStep() {
	const int page = std::max(ARROW_STEP, viewHeight() - ARROW_STEP);
	switch (m_sbPart) {
	case SB_DEC_ARROW: scrollTo(m_scroll - ARROW_STEP); break;
	case SB_INC_ARROW: scrollTo(m_scroll + ARROW_STEP); break;
	case SB_DEC_PAGE: scrollTo(m_scroll - page); break;
	case SB_INC_PAGE: scrollTo(m_scroll + page); break;
	default: m_repeat.stop(); return;
	}
	m_repeat.start(REPEAT_MS);
}

void FolderWindow::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const QPoint pos = e->position().toPoint();
	const int barX = width() - SB_WIDTH + 1;
	if (pos.x() >= barX && pos.y() >= HEADER_H) {
		pl_scrollbar v = verticalBar();
		int along = pos.y() - HEADER_H;
		m_sbPart = sb_hit(&v, along);
		if (m_sbPart == SB_THUMB_PART) {
			m_thumbGrab = along - (SB_ARROW + 1 + v.thumb);
		} else if (m_sbPart != SB_NONE) {
			scrollStep();
			m_repeat.start(REPEAT_FIRST_MS);
		}
		return;
	}
	if (pos.y() < CONTENT_Y || pos.x() >= barX) {
		return;
	}
	Item *hit = itemAt(pos);
	const bool extend = e->modifiers() & Qt::ShiftModifier;
	if (extend) {
		if (hit) {
			hit->selected = !hit->selected;
		}
	} else if (!hit || !hit->selected) {
		/* Pressing a selected icon keeps the selection, to drag them all. */
		for (auto &item : m_items) {
			item->selected = item.get() == hit;
		}
	}
	m_pressItem = hit && hit->selected ? hit : nullptr;
	m_pressPos = pos;
	update();
	Finder::instance().setFront(this);
	Finder::instance().notifyState();
}

QPoint FolderWindow::toWindow(QPoint contentPos) const {
	return QPoint(contentPos.x(), contentPos.y() - m_scroll + CONTENT_Y);
}

void FolderWindow::mouseMoveEvent(QMouseEvent *e) {
	if (m_pressItem && (e->buttons() & Qt::LeftButton) &&
			(e->position().toPoint() - m_pressPos).manhattanLength() >=
				QApplication::startDragDistance()) {
		std::vector<Item *> items = selectedItems();
		std::vector<QPoint> origins;
		for (Item *item : items) {
			origins.push_back(toWindow(item->pos));
		}
		m_pressItem = nullptr;
		startItemDrag(this, items, origins, m_pressPos);
		return;
	}
	if (m_sbPart != SB_THUMB_PART) {
		return;
	}
	pl_scrollbar v = verticalBar();
	int range = sb_thumb_range(v.length);
	if (range <= 0) {
		return;
	}
	int thumb = static_cast<int>(e->position().y()) - HEADER_H - (SB_ARROW + 1) - m_thumbGrab;
	thumb = std::clamp(thumb, 0, range);
	scrollTo(thumb * scrollMax() / range);
}

void FolderWindow::mouseReleaseEvent(QMouseEvent *) {
	m_pressItem = nullptr;
	m_sbPart = SB_NONE;
	m_repeat.stop();
}

void FolderWindow::mouseDoubleClickEvent(QMouseEvent *e) {
	if (itemAt(e->position().toPoint())) {
		Finder::instance().openSelection();
	}
}

void FolderWindow::keyPressEvent(QKeyEvent *e) {
	if (!finderShortcut(e)) {
		QWidget::keyPressEvent(e);
	}
}

Item *FolderWindow::dropTargetAt(QPoint pos, const QStringList &dragged) {
	Item *item = itemAt(pos);
	return item && acceptsDrops(*item) && !dragged.contains(item->path) ? item : nullptr;
}

void FolderWindow::clearDropTarget() {
	for (auto &item : m_items) {
		item->dropTarget = false;
	}
	update();
}

void FolderWindow::dragEnterEvent(QDragEnterEvent *e) {
	if (e->mimeData()->hasUrls()) {
		e->acceptProposedAction();
	}
}

void FolderWindow::dragMoveEvent(QDragMoveEvent *e) {
	Item *target = dropTargetAt(e->position().toPoint(), draggedPaths(e->mimeData()));
	for (auto &item : m_items) {
		item->dropTarget = item.get() == target;
	}
	update();
	e->acceptProposedAction();
}

void FolderWindow::dragLeaveEvent(QDragLeaveEvent *) {
	clearDropTarget();
}

void FolderWindow::dropEvent(QDropEvent *e) {
	Item *target = dropTargetAt(e->position().toPoint(), draggedPaths(e->mimeData()));
	clearDropTarget();
	dropItems(e, target, m_path);
}

bool finderShortcut(QKeyEvent *e) {
	if (!(e->modifiers() & Qt::ControlModifier)) {
		return false;
	}
	Finder &f = Finder::instance();
	switch (e->key()) {
	case Qt::Key_N: f.newFolder(); return true;
	case Qt::Key_O: f.openSelection(); return true;
	case Qt::Key_W: f.closeWindow(); return true;
	case Qt::Key_Backspace: f.moveSelectionToTrash(); return true;
	default: return false;
	}
}

void FolderWindow::wheelEvent(QWheelEvent *e) {
	scrollTo(m_scroll - e->angleDelta().y() / 120 * ARROW_STEP * 3);
}
