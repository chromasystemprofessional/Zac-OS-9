#include "folderwindow.h"

#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStorageInfo>
#include <algorithm>

#include "fileops.h"
#include "infowindow.h"
#include "platinumshell.h"

/* Item-count header: HIG figure 2-24. */
static constexpr int HEADER_H = 21;       /* white row, #DDD, #AAA shade */
static constexpr int HEADER_BASELINE = 13;

/* Icon view. The grid is not in the HIG (TODO: measure). */
static constexpr int ICONS_TOP = HEADER_H + 1; /* below a black separator */
static constexpr int CELL_W = 80, CELL_H = 64;
static constexpr int MARGIN = 8;

/* List view, HIG figure 2-24: a 21 px column-header band, 19 px rows. */
static constexpr int BAND_H = 21;
static constexpr int LIST_TOP = HEADER_H + BAND_H;
static constexpr int ROW_H = 19;          /* 18 px row + white separator */
static constexpr int ROW_BASELINE = 11;
static constexpr int TRI_X = 12, ICON_X = 28, NAME_X = 50;
static constexpr int INDENT = 20;         /* per expanded level (TODO: measure) */
static constexpr int COL_TEXT_X = 6;      /* text inset in non-name columns */

struct Column {
	const char *title;
	int width;
};
static const Column COLUMNS[] = {
	{ "Name", 220 }, { "Date Modified", 170 }, { "Size", 64 }, { "Kind", 140 },
};
static constexpr int N_COLUMNS = sizeof(COLUMNS) / sizeof(COLUMNS[0]);

static constexpr int ARROW_STEP = 16;
static constexpr int REPEAT_FIRST_MS = 300, REPEAT_MS = 50;

/* Disclosure triangle, collapsed (pointing right), with its shadow:
 * HIG figure 2-24. 0 black, d/c/e lavender light/mid/dark, 6 #888, f #BBB. */
static const char *const TRIANGLE[12] = {
	"0......",
	"00.....",
	"0d0....",
	"0dc0...",
	"0dcc0..",
	"0dcce0.",
	"0dce06f",
	"0de06f.",
	"0c06f..",
	"006f...",
	"06f....",
	".f.....",
};

static uint32_t triangleColor(char ch) {
	switch (ch) {
	case '0': return C_BLACK;
	case 'd': return RGB(0xCC, 0xCC, 0xFF);
	case 'c': return RGB(0x99, 0x99, 0xFF);
	case 'e': return RGB(0x66, 0x66, 0xCC);
	case '6': return GRAY(0x8);
	case 'f': return GRAY(0xB);
	default: return 0;
	}
}

/* Expanded triangles point down: the same art transposed.
 * TODO: measure the expanded triangle (figure 2-24's right window). */
static void paintTriangle(pl_canvas *c, int x, int y, bool expanded) {
	for (int j = 0; j < 12; j++) {
		for (int i = 0; i < 7; i++) {
			uint32_t v = triangleColor(TRIANGLE[j][i]);
			if (v) {
				pl_put(c, expanded ? x + j - 3 : x + i, expanded ? y + i + 3 : y + j, v);
			}
		}
	}
}

/* ---- windows --------------------------------------------------------------- */

static QHash<QString, FolderWindow *> &openWindows() {
	static QHash<QString, FolderWindow *> windows;
	return windows;
}

FolderWindow *FolderWindow::open(const QString &path) {
	const QString key = QDir(path).absolutePath();
	FolderWindow *w = openWindows().value(key);
	if (w) {
		w->show();
		w->raise();
		w->activateWindow();
	} else {
		/* Spatial: the window comes back with its old size, view and place. */
		w = new FolderWindow(key);
		openWindows().insert(key, w);
		w->resize(w->m_state.size);
		w->show();
		if (w->m_state.hasPosition) {
			platinumSetWindowPosition(w, w->m_state.position);
		}
		platinumOnWindowPosition(w, [w](QPoint p) {
			w->m_state.position = p;
			w->m_state.hasPosition = true;
			w->saveStateSoon();
		});
	}
	/* A window the Finder opens is the front one (Qt only learns about
	 * activation from keyboard focus, which menus briefly take away). */
	Finder::instance().setFront(w);
	return w;
}

bool FolderWindow::isOpen(const QString &path) {
	return openWindows().contains(QDir(path).absolutePath());
}

void FolderWindow::reloadAll(const QString &folder) {
	const QString key = QDir(folder).absolutePath();
	for (FolderWindow *w : openWindows()) {
		if (w->m_path == key || w->m_expanded.contains(key)) {
			w->reload();
		}
	}
}

FolderWindow::FolderWindow(const QString &path) : m_path(path) {
	m_state = FolderState::load(path);
	m_mode = m_state.viewMode == 1 ? ViewMode::List : ViewMode::Icons;
	m_sortColumn = std::clamp(m_state.sortColumn, 0, N_COLUMNS - 1);
	m_saveTimer.setSingleShot(true);
	m_saveTimer.callOnTimeout([this] { m_state.save(m_path); });
	setAttribute(Qt::WA_DeleteOnClose);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setWindowTitle(displayName(path));
	setAcceptDrops(true);
	m_repeat.callOnTimeout([this] { scrollStep(); });
	m_renameTimer.setSingleShot(true);
	m_renameTimer.callOnTimeout([this] { beginRename(m_renameItem); });
	/* Other programs change folders too; follow them. */
	m_watcher.addPath(path);
	QObject::connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
		[this](const QString &) { reload(); });
	reload();
}

FolderWindow::~FolderWindow() {
	if (m_saveTimer.isActive()) {
		m_state.save(m_path);
	}
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

void FolderWindow::saveStateSoon() {
	m_saveTimer.start(300);
}

void FolderWindow::setViewMode(ViewMode mode) {
	if (mode == m_mode) {
		return;
	}
	m_state.viewMode = mode == ViewMode::List ? 1 : 0;
	saveStateSoon();
	m_editor.cancel();
	m_mode = mode;
	m_scrollX = m_scrollY = 0;
	relayout();
	update();
	Finder::instance().notifyState();
}

/* ---- items ------------------------------------------------------------------ */

template <typename F> void FolderWindow::forEachItem(F f) {
	for (auto &item : m_items) {
		f(item.get());
	}
	for (auto &entry : m_children) {
		for (auto &item : entry.second) {
			f(item.get());
		}
	}
}

/* Re-read the folder (and expanded subfolders), keeping the selection. */
void FolderWindow::reload() {
	if (!QDir(m_path).exists()) {
		/* Trashed, deleted or moved away: its window goes with it. */
		QTimer::singleShot(0, this, [this] { close(); });
		return;
	}
	QSet<QString> selected;
	forEachItem([&](Item *item) {
		if (item->selected) {
			selected.insert(item->path);
		}
	});
	m_pressItem = nullptr;
	auto fresh = listFolder(m_path);
	m_editor.retarget(fresh);
	m_renameTimer.stop();
	m_renameItem = nullptr;
	m_items = std::move(fresh);

	m_children.clear();
	for (const QString &dir : QSet<QString>(m_expanded)) {
		if (QDir(dir).exists()) {
			m_children[dir] = listFolder(dir);
		} else {
			m_expanded.remove(dir);
		}
	}
	forEachItem([&](Item *item) { item->selected = selected.contains(item->path); });
	relayout();
	update();
}

std::vector<Item *> FolderWindow::selectedItems() {
	std::vector<Item *> out;
	forEachItem([&](Item *item) {
		if (item->selected) {
			out.push_back(item);
		}
	});
	return out;
}

void FolderWindow::selectByName(const QString &name) {
	forEachItem([&](Item *item) { item->selected = false; });
	for (auto &item : m_items) {
		item->selected = item->name == name;
	}
	update();
}

/* ---- layout and scrolling ---------------------------------------------------- */

int FolderWindow::contentTop() const {
	return m_mode == ViewMode::List ? LIST_TOP : ICONS_TOP;
}

int FolderWindow::viewWidth() const {
	return width() - SB_WIDTH + 1;
}

int FolderWindow::viewHeight() const {
	return height() - (SB_WIDTH - 1) - contentTop();
}

int FolderWindow::listWidth() const {
	int w = 0;
	for (const Column &col : COLUMNS) {
		w += col.width;
	}
	return w;
}

int FolderWindow::scrollMaxX() const {
	const int content = m_mode == ViewMode::List ? listWidth() : m_contentWidth;
	return std::max(0, content - viewWidth());
}

int FolderWindow::scrollMaxY() const {
	return std::max(0, m_contentHeight - viewHeight());
}

void FolderWindow::scrollTo(int x, int y) {
	x = std::clamp(x, 0, scrollMaxX());
	y = std::clamp(y, 0, scrollMaxY());
	if (x != m_scrollX || y != m_scrollY) {
		m_scrollX = x;
		m_scrollY = y;
		update();
	}
}

void FolderWindow::relayout() {
	if (m_mode == ViewMode::Icons) {
		layoutIcons();
	} else {
		buildRows();
		m_contentHeight = static_cast<int>(m_rows.size()) * ROW_H;
	}
	scrollTo(m_scrollX, m_scrollY);
}

void FolderWindow::resizeEvent(QResizeEvent *) {
	relayout();
	if (isVisible()) {
		m_state.size = size();
		saveStateSoon();
	}
}

QPoint FolderWindow::toContent(QPoint pos) const {
	return QPoint(pos.x() + m_scrollX, pos.y() - contentTop() + m_scrollY);
}

QPoint FolderWindow::toWindow(QPoint content) const {
	return QPoint(content.x() - m_scrollX, content.y() - m_scrollY + contentTop());
}

pl_scrollbar FolderWindow::verticalBar() const {
	pl_scrollbar sb{};
	sb.vertical = true;
	const int top = contentTop() - 1; /* shares the line above the content */
	sb.length = height() - (SB_WIDTH - 1) - top + 1;
	sb.enabled = scrollMaxY() > 0;
	int range = sb_thumb_range(sb.length);
	sb.thumb = sb.enabled ? m_scrollY * range / scrollMaxY() : 0;
	return sb;
}

pl_scrollbar FolderWindow::horizontalBar() const {
	pl_scrollbar sb{};
	sb.length = width() - SB_WIDTH + 3;
	sb.enabled = scrollMaxX() > 0;
	int range = sb_thumb_range(sb.length);
	sb.thumb = sb.enabled ? m_scrollX * range / scrollMaxX() : 0;
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

void FolderWindow::scrollStep() {
	const int view = m_sbVertical ? viewHeight() : viewWidth();
	const int page = std::max(ARROW_STEP, view - ARROW_STEP);
	int delta = 0;
	switch (m_sbPart) {
	case SB_DEC_ARROW: delta = -ARROW_STEP; break;
	case SB_INC_ARROW: delta = ARROW_STEP; break;
	case SB_DEC_PAGE: delta = -page; break;
	case SB_INC_PAGE: delta = page; break;
	default: m_repeat.stop(); return;
	}
	if (m_sbVertical) {
		scrollTo(m_scrollX, m_scrollY + delta);
	} else {
		scrollTo(m_scrollX + delta, m_scrollY);
	}
	m_repeat.start(REPEAT_MS);
}

/* ---- icon view ----------------------------------------------------------------- */

/* Placed icons stay put (spatial); the rest flow into free grid cells. */
void FolderWindow::layoutIcons() {
	const int cols = std::max(1, (viewWidth() - 2 * MARGIN) / CELL_W);
	std::vector<Item *> items;
	for (auto &item : m_items) {
		items.push_back(item.get());
	}
	placeIcons(items, m_state.icons, [cols](int i) {
		return QPoint(MARGIN + (i % cols) * CELL_W + (CELL_W - PL_ICON_LARGE) / 2,
			MARGIN + (i / cols) * CELL_H);
	}, CELL_W, CELL_H);
	m_contentHeight = 0;
	m_contentWidth = 0;
	for (Item *item : items) {
		m_contentHeight = std::max(m_contentHeight, item->pos.y() + CELL_H + MARGIN);
		m_contentWidth = std::max(m_contentWidth,
			item->pos.x() + PL_ICON_LARGE + (CELL_W - PL_ICON_LARGE) / 2 + MARGIN);
	}
}

void FolderWindow::itemRenamed(const QString &from, const QString &to) {
	auto it = m_state.icons.find(from);
	if (it != m_state.icons.end()) {
		QPoint pos = *it;
		m_state.icons.erase(it);
		m_state.icons.insert(to, pos);
		saveStateSoon();
	}
}

void FolderWindow::paintIcons(pl_canvas *content) {
	const int viewH = viewHeight();
	pl_fill(content, 0, m_scrollY, viewWidth() - 1, m_scrollY + viewH - 1, C_WHITE);
	for (auto &item : m_items) {
		if (item->pos.y() + CELL_H >= m_scrollY && item->pos.y() <= m_scrollY + viewH) {
			const bool editing = m_editor.editing(item.get());
			paintIconItem(content, *item, item->pos.x(), item->pos.y(), false, !editing);
			if (editing) {
				m_editor.paint(content, item->pos.x(), item->pos.y());
			}
		}
	}
}

/* ---- list view ----------------------------------------------------------------- */

void FolderWindow::sortItems(std::vector<std::unique_ptr<Item>> &items) const {
	auto byName = [](const std::unique_ptr<Item> &a, const std::unique_ptr<Item> &b) {
		return a->name.compare(b->name, Qt::CaseInsensitive) < 0;
	};
	switch (m_sortColumn) {
	case 1: /* newest first */
		std::stable_sort(items.begin(), items.end(), [](auto &a, auto &b) {
			return a->modified > b->modified;
		});
		break;
	case 2: /* largest first; folders last */
		std::stable_sort(items.begin(), items.end(), [](auto &a, auto &b) {
			if (a->isDir != b->isDir) {
				return !a->isDir;
			}
			return a->size > b->size;
		});
		break;
	case 3:
		std::stable_sort(items.begin(), items.end(), [](auto &a, auto &b) {
			return a->kindName().compare(b->kindName(), Qt::CaseInsensitive) < 0;
		});
		break;
	default:
		std::stable_sort(items.begin(), items.end(), byName);
		break;
	}
}

void FolderWindow::buildRows() {
	m_rows.clear();
	std::function<void(std::vector<std::unique_ptr<Item>> &, int)> add =
		[&](std::vector<std::unique_ptr<Item>> &items, int depth) {
			sortItems(items);
			for (auto &item : items) {
				m_rows.push_back({ item.get(), depth });
				auto children = m_children.find(item->path);
				if (item->isDir && children != m_children.end()) {
					add(children->second, depth + 1);
				}
			}
		};
	add(m_items, 0);
}

void FolderWindow::toggleExpanded(Item *item) {
	const QString path = item->path;
	if (m_expanded.contains(path)) {
		m_expanded.remove(path);
		m_children.erase(path);
		m_watcher.removePath(path);
	} else {
		m_expanded.insert(path);
		m_children[path] = listFolder(path);
		m_watcher.addPath(path);
	}
	relayout();
	update();
}

int FolderWindow::columnAt(int contentX) const {
	int x = 0;
	for (int i = 0; i < N_COLUMNS; i++) {
		if (contentX < x + COLUMNS[i].width) {
			return i;
		}
		x += COLUMNS[i].width;
	}
	return -1;
}

/* Column headers (HIG figure 2-24): the sorted column dark, the others
 * light, each bevelled. TODO: the unsorted columns' edges are estimated. */
void FolderWindow::paintColumnHeaders(pl_canvas *b) {
	auto paintColumn = [&](int x0, int x1, bool sorted, const char *title, int textX) {
		if (sorted) {
			pl_hline(b, x0, x1, 0, GRAY(0x1));
			pl_hline(b, x0 + 1, x1, 1, GRAY(0x5));
			pl_fill(b, x0 + 2, 2, x1, 18, GRAY(0x8));
			pl_vline(b, x0, 0, 19, GRAY(0x1));
			pl_vline(b, x0 + 1, 1, 18, GRAY(0x5));
			pl_hline(b, x0 + 1, x1, 19, GRAY(0xA));
			pl_put(b, x0 + 1, 19, GRAY(0x7));
			pl_hline(b, x0, x1, 20, GRAY(0x4));
			pl_put(b, x0, 20, GRAY(0x2));
			pl_vline(b, x1 - 1, 2, 19, GRAY(0xA));
			pl_vline(b, x1, 1, 20, GRAY(0x4));
		} else {
			pl_hline(b, x0, x1, 0, GRAY(0x6));
			pl_hline(b, x0 + 1, x1, 1, C_WHITE);
			pl_fill(b, x0 + 2, 2, x1, 18, GRAY(0xC));
			pl_vline(b, x0, 0, 19, GRAY(0x6));
			pl_vline(b, x0 + 1, 1, 18, C_WHITE);
			pl_hline(b, x0 + 1, x1, 19, GRAY(0x8));
			pl_hline(b, x0, x1, 20, GRAY(0x3));
			pl_vline(b, x1, 1, 19, GRAY(0x8));
		}
		if (title) {
			Text t(title, x1 - textX - 2, PL_FONT_VIEWS);
			pl_text(b, t.t, textX, 13, C_BLACK);
		}
	};
	int x = 0;
	for (int i = 0; i < N_COLUMNS; i++) {
		const int x1 = x + COLUMNS[i].width - 1;
		paintColumn(x, x1, i == m_sortColumn, COLUMNS[i].title,
			x + (i == 0 ? NAME_X : COL_TEXT_X));
		x = x1 + 1;
	}
	const int end = b->x + b->width - 1;
	if (x <= end) {
		paintColumn(x, end, false, nullptr, 0);
	}
}

void FolderWindow::paintList(pl_canvas *c) {
	const int viewH = viewHeight(), right = m_scrollX + viewWidth() - 1;
	const int y0 = m_scrollY, y1 = m_scrollY + viewH - 1;

	/* Column backgrounds: the sorted column a shade darker. */
	int x = 0;
	for (int i = 0; i < N_COLUMNS; i++) {
		pl_fill(c, x, y0, x + COLUMNS[i].width - 1, y1,
			i == m_sortColumn ? GRAY(0xD) : GRAY(0xE));
		x += COLUMNS[i].width;
	}
	pl_fill(c, x, y0, std::max(x, right), y1, GRAY(0xE));

	const int first = std::max(0, m_scrollY / ROW_H);
	const int last = std::min<int>(m_rows.size() - 1, (m_scrollY + viewH) / ROW_H);
	for (int r = first; r <= last; r++) {
		const Row &row = m_rows[r];
		Item *item = row.item;
		const int top = r * ROW_H, d = row.depth * INDENT;
		pl_hline(c, 0, std::max(listWidth(), right), top + ROW_H - 1, C_WHITE);

		if (item->isDir) {
			paintTriangle(c, TRI_X + d, top + 3, m_expanded.contains(item->path));
		}
		pl_icon_paint(c, ICON_X + d, top + 1, item->kind, PL_ICON_SMALL,
			item->selected || item->dropTarget);

		const int nameW = COLUMNS[0].width - NAME_X - d - 6;
		Text name(item->name, std::max(8, nameW), PL_FONT_VIEWS);
		uint32_t ink = C_BLACK;
		if (item->selected) {
			pl_fill(c, NAME_X + d - 2, top + 2, NAME_X + d + name.inkWidth() + 1, top + 14, C_BLACK);
			ink = C_WHITE;
		}
		pl_text(c, name.t, NAME_X + d, top + ROW_BASELINE, ink);

		const QString cells[] = {
			finderDate(item->modified),
			item->isDir ? QStringLiteral("--") : finderSize(item->size),
			item->kindName(),
		};
		int cx = COLUMNS[0].width;
		for (int i = 1; i < N_COLUMNS; i++) {
			Text t(cells[i - 1], COLUMNS[i].width - 2 * COL_TEXT_X, PL_FONT_VIEWS);
			pl_text(c, t.t, cx + COL_TEXT_X, top + ROW_BASELINE, C_BLACK);
			cx += COLUMNS[i].width;
		}
	}
}

/* ---- painting ---------------------------------------------------------------- */

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

	/* The scrolling area, through a canvas whose origin follows the scroll
	 * position (so drawing is in content coordinates and clips itself). */
	const int top = contentTop();
	pl_canvas content = {
		c->px + top * c->stride, c->stride, m_scrollX, m_scrollY,
		viewWidth(), std::max(0, viewHeight()),
	};
	if (m_mode == ViewMode::Icons) {
		pl_hline(c, 0, W - 1, HEADER_H, C_BLACK);
		paintIcons(&content);
	} else {
		pl_canvas band = { c->px + HEADER_H * c->stride, c->stride, m_scrollX, 0, W, BAND_H };
		paintColumnHeaders(&band);
		paintList(&content);
	}

	/* Scroll bars share their outer lines with the window frame. The
	 * compositor draws the resize box in the corner. */
	pl_scrollbar v = verticalBar();
	pl_scrollbar_paint(c, W - SB_WIDTH + 1, top - 1, &v, PL_ACCENT_DEFAULT);
	pl_scrollbar h = horizontalBar();
	pl_scrollbar_paint(c, -1, H - SB_WIDTH + 1, &h, PL_ACCENT_DEFAULT);

	QPainter p(this);
	px.blit(p);
}

/* ---- hit testing ---------------------------------------------------------------- */

Item *FolderWindow::itemAt(QPoint pos, bool *onTriangle) {
	if (onTriangle) {
		*onTriangle = false;
	}
	if (pos.y() < contentTop() || pos.x() >= viewWidth()) {
		return nullptr;
	}
	const QPoint p = toContent(pos);
	if (m_mode == ViewMode::Icons) {
		for (auto it = m_items.rbegin(); it != m_items.rend(); ++it) {
			if (iconItemContains(**it, (*it)->pos.x(), (*it)->pos.y(), p)) {
				return it->get();
			}
		}
		return nullptr;
	}
	const int r = p.y() / ROW_H;
	if (p.y() < 0 || r >= static_cast<int>(m_rows.size()) || p.x() >= listWidth()) {
		return nullptr;
	}
	const Row &row = m_rows[r];
	const int tx = TRI_X + row.depth * INDENT;
	if (onTriangle && row.item->isDir && p.x() >= tx - 2 && p.x() <= tx + 8) {
		*onTriangle = true;
	}
	return row.item;
}

Item *FolderWindow::dropTargetAt(QPoint pos, const QStringList &dragged) {
	Item *item = itemAt(pos);
	return item && acceptsDrops(*item) && !dragged.contains(item->path) ? item : nullptr;
}

void FolderWindow::clearDropTarget() {
	forEachItem([](Item *item) { item->dropTarget = false; });
	update();
}

/* ---- mouse ---------------------------------------------------------------------- */

void FolderWindow::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const QPoint pos = e->position().toPoint();
	const int barX = width() - SB_WIDTH + 1, barY = height() - SB_WIDTH + 1;

	/* Scroll bars. */
	if (pos.x() >= barX && pos.y() >= contentTop() - 1 && pos.y() < barY) {
		pl_scrollbar v = verticalBar();
		const int along = pos.y() - (contentTop() - 1);
		m_sbVertical = true;
		m_sbPart = sb_hit(&v, along);
		if (m_sbPart == SB_THUMB_PART) {
			m_thumbGrab = along - (SB_ARROW + 1 + v.thumb);
		} else if (m_sbPart != SB_NONE) {
			scrollStep();
			m_repeat.start(REPEAT_FIRST_MS);
		}
		return;
	}
	if (pos.y() >= barY && pos.x() < barX) {
		pl_scrollbar h = horizontalBar();
		const int along = pos.x() + 1;
		m_sbVertical = false;
		m_sbPart = sb_hit(&h, along);
		if (m_sbPart == SB_THUMB_PART) {
			m_thumbGrab = along - (SB_ARROW + 1 + h.thumb);
		} else if (m_sbPart != SB_NONE) {
			scrollStep();
			m_repeat.start(REPEAT_FIRST_MS);
		}
		return;
	}

	/* List view: clicking a column header sorts by it. */
	if (m_mode == ViewMode::List && pos.y() >= HEADER_H && pos.y() < LIST_TOP) {
		int col = columnAt(pos.x() + m_scrollX);
		if (col >= 0 && col != m_sortColumn) {
			m_sortColumn = col;
			m_state.sortColumn = col;
			saveStateSoon();
			relayout();
			update();
		}
		return;
	}
	if (pos.y() < contentTop() || pos.x() >= barX) {
		return;
	}

	const QPoint content = toContent(pos);
	if (Item *edited = m_editor.item()) {
		if (m_editor.contains(edited->pos.x(), edited->pos.y(), content)) {
			return;
		}
		m_editor.commit();
	}
	bool onTriangle = false;
	Item *hit = itemAt(pos, &onTriangle);
	if (onTriangle) {
		toggleExpanded(hit);
		return;
	}
	const bool extend = e->modifiers() & Qt::ShiftModifier;
	/* Icon view: a click on the name of the one selected icon starts a
	 * rename, unless it turns into a double-click or a drag first. */
	m_renameTimer.stop();
	if (m_mode == ViewMode::Icons && hit && hit->selected && !extend &&
			selectedItems().size() == 1 &&
			iconLabelContains(*hit, hit->pos.x(), hit->pos.y(), content)) {
		m_renameItem = hit;
		m_renameTimer.start(QApplication::doubleClickInterval());
	}
	if (extend) {
		if (hit) {
			hit->selected = !hit->selected;
		}
	} else if (!hit || !hit->selected) {
		/* Pressing a selected icon keeps the selection, to drag them all. */
		forEachItem([&](Item *item) { item->selected = item == hit; });
	}
	m_pressItem = hit && hit->selected ? hit : nullptr;
	m_pressPos = pos;
	update();
	Finder::instance().setFront(this);
	Finder::instance().notifyState();
}

void FolderWindow::mouseMoveEvent(QMouseEvent *e) {
	if (m_pressItem && (e->buttons() & Qt::LeftButton) &&
			(e->position().toPoint() - m_pressPos).manhattanLength() >=
				QApplication::startDragDistance()) {
		std::vector<Item *> items = selectedItems();
		std::vector<QPoint> origins;
		for (Item *item : items) {
			if (m_mode == ViewMode::Icons) {
				origins.push_back(toWindow(item->pos));
				continue;
			}
			/* List rows: centre a large outline on the small icon. */
			for (size_t r = 0; r < m_rows.size(); r++) {
				if (m_rows[r].item == item) {
					QPoint icon(ICON_X + m_rows[r].depth * INDENT, static_cast<int>(r) * ROW_H + 1);
					origins.push_back(toWindow(icon) - QPoint(8, 8));
				}
			}
		}
		m_pressItem = nullptr;
		m_renameTimer.stop();
		m_dragStart = m_pressPos;
		if (origins.size() == items.size()) {
			startItemDrag(this, items, origins, m_pressPos);
			Finder::instance().dragEnded();
		}
		return;
	}
	if (m_sbPart != SB_THUMB_PART) {
		return;
	}
	pl_scrollbar bar = m_sbVertical ? verticalBar() : horizontalBar();
	int range = sb_thumb_range(bar.length);
	if (range <= 0) {
		return;
	}
	const int along = m_sbVertical ? static_cast<int>(e->position().y()) - (contentTop() - 1)
		: static_cast<int>(e->position().x()) + 1;
	int thumb = std::clamp(along - (SB_ARROW + 1) - m_thumbGrab, 0, range);
	if (m_sbVertical) {
		scrollTo(m_scrollX, thumb * scrollMaxY() / range);
	} else {
		scrollTo(thumb * scrollMaxX() / range, m_scrollY);
	}
}

void FolderWindow::mouseReleaseEvent(QMouseEvent *) {
	m_pressItem = nullptr;
	m_sbPart = SB_NONE;
	m_repeat.stop();
}

void FolderWindow::mouseDoubleClickEvent(QMouseEvent *e) {
	m_renameTimer.stop();
	bool onTriangle = false;
	if (itemAt(e->position().toPoint(), &onTriangle) && !onTriangle) {
		Finder::instance().openSelection();
	}
}

void FolderWindow::wheelEvent(QWheelEvent *e) {
	const QPoint d = e->angleDelta() / 120 * ARROW_STEP * 3;
	if (e->modifiers() & Qt::ShiftModifier) {
		scrollTo(m_scrollX - d.y(), m_scrollY);
	} else {
		scrollTo(m_scrollX - d.x(), m_scrollY - d.y());
	}
}

/* ---- keyboard ----------------------------------------------------------------- */

void FolderWindow::beginRename(Item *item) {
	if (m_mode == ViewMode::Icons && item && item->selected) {
		m_editor.begin(item);
	}
}

void FolderWindow::keyPressEvent(QKeyEvent *e) {
	if (m_editor.key(e)) {
		return;
	}
	/* Return on the one selected icon: rename it (the Finder's way). */
	if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) &&
			selectedItems().size() == 1) {
		beginRename(selectedItems().front());
		return;
	}
	if (!finderShortcut(e)) {
		QWidget::keyPressEvent(e);
	}
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
	case Qt::Key_I: f.getInfo(); return true;
	case Qt::Key_D: f.duplicate(); return true;
	case Qt::Key_M: f.makeAlias(); return true;
	case Qt::Key_Y: f.putAway(); return true;
	case Qt::Key_R: f.showOriginal(); return true;
	default: return false;
	}
}

/* ---- drag and drop --------------------------------------------------------------- */

void FolderWindow::dragEnterEvent(QDragEnterEvent *e) {
	if (e->mimeData()->hasUrls()) {
		e->acceptProposedAction();
	}
}

void FolderWindow::dragMoveEvent(QDragMoveEvent *e) {
	Item *target = dropTargetAt(e->position().toPoint(), draggedPaths(e->mimeData()));
	forEachItem([&](Item *item) { item->dropTarget = item == target; });
	update();
	e->acceptProposedAction();
	Finder::instance().springHover(target && target->kind == PL_ICON_FOLDER ? target->path
		: QString());
}

void FolderWindow::dragLeaveEvent(QDragLeaveEvent *) {
	clearDropTarget();
	Finder::instance().springHover(QString());
}

void FolderWindow::dropEvent(QDropEvent *e) {
	Item *target = dropTargetAt(e->position().toPoint(), draggedPaths(e->mimeData()));
	clearDropTarget();
	if (!target && e->source() == this && m_mode == ViewMode::Icons &&
			!(e->modifiers() & Qt::AltModifier)) {
		/* Icons dragged within their own window: they move there, and stay. */
		const QPoint delta = e->position().toPoint() - m_dragStart;
		for (Item *item : selectedItems()) {
			QPoint p = item->pos + delta;
			p.setX(std::max(0, p.x()));
			p.setY(std::max(0, p.y()));
			m_state.icons.insert(item->name, p);
		}
		saveStateSoon();
		relayout();
		update();
		e->setDropAction(Qt::MoveAction);
		e->accept();
		return;
	}
	dropItems(e, target, m_path);
	Finder::instance().dragEnded();
}
