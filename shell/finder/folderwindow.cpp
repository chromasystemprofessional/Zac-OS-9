#include "folderwindow.h"

#include "clippings.h"
#include "tinyicons.h"
#include "thumbnails.h"
#include "fileassoc.h"

#include "vfs.h"

#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStorageInfo>
#include <QDirIterator>
#include <QThreadPool>
#include <algorithm>
#include <cstdlib>
#include <limits>

#include "fileops.h"
#include "infowindow.h"
#include "settings.h"
#include "platinumshell.h"
#include "panelkit.h"

/* Item-count header: HIG figure 2-24. */
static constexpr int HEADER_H = 21;       /* white row, #DDD, #AAA shade */
static constexpr int HEADER_BASELINE = 13;

/* Icon view. The grid is not in the HIG (TODO: measure). Preferences'
 * grid spacing makes it Wide (as it always was) or Tight; small icons
 * (View Options) sit in short, wide cells with their names beside them. */
static constexpr int ICONS_TOP = HEADER_H + 1; /* below a black separator */
static constexpr int MARGIN = 8;
static constexpr int SMALL_LABEL_X = PL_ICON_SMALL + 4; /* small icon's name */

/* Button view (Mac OS 8): a grid of small-bevel buttons, names below.
 * Not in the HIG; sizes are estimates. TODO: measure. Small buttons
 * (View Options) hold small icons. */
static constexpr int BUTTON = 48, SMALL_BUTTON = 26; /* bevel button, square */

/* List view, HIG figure 2-24: a 21 px column-header band, 19 px rows
 * (small icons; standard ones need taller rows, tiny ones fewer). */
static constexpr int BAND_H = 21;
static constexpr int LIST_TOP = HEADER_H + BAND_H;
static constexpr int TRI_X = 12;
static constexpr int INDENT = 20;         /* per expanded level (TODO: measure) */
static constexpr int COL_TEXT_X = 6;      /* text inset in non-name columns */


static constexpr int ARROW_STEP = 16;
static constexpr int REPEAT_FIRST_MS = 300, REPEAT_MS = 50;

/* ---- windows --------------------------------------------------------------- */

static QHash<QString, FolderWindow *> &openWindows() {
	static QHash<QString, FolderWindow *> windows;
	return windows;
}

/* A virtual path names a node by its stable id and is already absolute;
 * QDir would read it as relative to the working directory. */
static QString windowKey(const QString &path) {
	return vfsIsVirtual(path) ? path : QDir(path).absolutePath();
}

/* Where a path's free space comes from: everything in the Macintosh view
 * lives on the startup disk, which is the disk holding the home folder. */
static QString spacePath(const QString &path) {
	return vfsIsVirtual(path) ? QDir::homePath() : path;
}

/* Has this folder gone away? A virtual one goes when its node does. */
static bool folderExists(const QString &path) {
	return vfsIsVirtual(path) ? vfsNode(path) != nullptr : QDir(path).exists();
}

FolderWindow *FolderWindow::open(const QString &path) {
	const QString key = windowKey(path);
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
		pl_sound_event("window-open");
		if (w->m_state.popup && platinumHasPopupWindows()) {
			/* Before it first draws: a tab, never a window for a moment. */
			platinumSetPopupTab(w, w->m_state.tabX);
			platinumSetFrameStyle(w, FrameStyle::Popup);
		} else if (w->m_state.hasPosition) {
			platinumSetWindowPosition(w, w->m_state.position);
		}
		platinumOnPopup(w, [w](int x) {
			w->m_state.tabX = x;
			w->saveStateSoon();
		}, [w] {
			/* Its tab dragged up: an ordinary window again, where it lands. */
			w->m_state.popup = false;
			w->saveStateSoon();
			Finder::instance().notifyState();
		});
		w->updateStandardSize();
		platinumOnTitleMenu(w, [w](bool option) { Finder::instance().showPathMenu(w, option); });
		w->setUpProxyIcon();
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

void FolderWindow::closeAll() {
	const QList<FolderWindow *> windows = openWindows().values();
	for (FolderWindow *window : windows) {
		if (window) {
			window->close();
		}
	}
}

void FolderWindow::reloadAll(const QString &folder) {
	const QString key = windowKey(folder);
	for (FolderWindow *w : openWindows()) {
		if (w->m_path == key || w->m_expanded.contains(key)) {
			w->reload();
		}
	}
}

FolderWindow::FolderWindow(const QString &path) : m_path(path) {
	watchThumbnails(this);
	watchFileAssociations(this);
	m_state = FolderState::load(path);
	m_mode = m_state.viewMode == 1 ? ViewMode::List
		: m_state.viewMode == 2 ? ViewMode::Buttons : ViewMode::Icons;
	m_sortColumn = m_state.options.list.columns & columnBit(m_state.sortColumn) ?
		m_state.sortColumn : COL_NAME;
	m_editor.setBeside(smallIcons());
	m_saveTimer.setSingleShot(true);
	m_saveTimer.callOnTimeout([this] { m_state.save(m_path); });
	setAttribute(Qt::WA_DeleteOnClose);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setWindowTitle(displayName(path));
	setAcceptDrops(true);
	m_repeat.callOnTimeout([this] { scrollStep(); });
	m_renameTimer.setSingleShot(true);
	m_renameTimer.callOnTimeout([this] { beginRename(m_renameItem); });
	m_typeSelectTimer.setSingleShot(true);
	m_typeSelectTimer.setInterval(1000);
	m_typeSelectTimer.callOnTimeout([this] { m_typeSelect.clear(); });
	m_marqueeScroll.setInterval(REPEAT_MS);
	m_marqueeScroll.callOnTimeout([this] { updateMarquee(m_marqueePointer); });
	/* Other programs change folders too; follow them. */
	if (vfsIsVirtual(path)) {
		/* Applications come and go as packages are installed. */
		m_vfsToken = vfsOnChange([this] { reload(); });
	} else {
		m_watcher.addPath(path);
	}
	QObject::connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
		[this](const QString &) { reload(); });
	reload();
}

FolderWindow::~FolderWindow() {
	if (m_sizingCancel) {
		*m_sizingCancel = true;
	}
	if (m_vfsToken) {
		vfsOffChange(m_vfsToken);
	}
	if (m_saveTimer.isActive()) {
		m_state.save(m_path);
	}
	openWindows().remove(m_path);
	Finder::instance().viewClosed(this);
}

void FolderWindow::closeEvent(QCloseEvent *e) {
	pl_sound_event("window-close");
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
	m_state.viewMode = mode == ViewMode::List ? 1 : mode == ViewMode::Buttons ? 2 : 0;
	saveStateSoon();
	m_editor.cancel();
	m_mode = mode;
	m_scrollX = m_scrollY = 0;
	relayout();
	update();
	Finder::instance().notifyState();
}

void FolderWindow::setPopup(bool popup) {
	if (popup == m_state.popup || (popup && !platinumHasPopupWindows())) {
		return;
	}
	m_state.popup = popup;
	saveStateSoon();
	if (popup) {
		platinumSetPopupTab(this, m_state.tabX);
		platinumSetFrameStyle(this, FrameStyle::Popup);
	} else {
		platinumSetFrameStyle(this, FrameStyle::Document);
		if (m_state.hasPosition) {
			platinumSetWindowPosition(this, m_state.position);
		}
	}
	Finder::instance().notifyState();
}

/* The folder's small icon in the title bar (Mac OS 9's proxy icon).
 * Dragging it moves or copies the folder, as dragging its icon does; so
 * only an ordinary folder can be dragged, not Home, a disk, the Trash or
 * the Macintosh view's own folders, which keep just the icon. */
void FolderWindow::setUpProxyIcon() {
	const QString key = m_path;
	const bool trash = QDir(key) == QDir(trashFilesPath());
	const bool virtualFolder = vfsIsVirtual(key);
	const bool mountRoot = !virtualFolder && QStorageInfo(key).rootPath() == QDir(key).absolutePath();
	const bool home = !virtualFolder && QDir(key) == QDir::home();
	const pl_icon_kind kind = trash ? PL_ICON_TRASH_FULL : mountRoot ? PL_ICON_DISK : PL_ICON_FOLDER;
	std::function<void()> dragged;
	if (!trash && !virtualFolder && !mountRoot && !home) {
		dragged = [this] {
			auto item = makeItem(QFileInfo(m_path));
			if (!item) {
				return;
			}
			/* The icon is above the window, in its title bar. */
			const QPoint origin(width() / 2 - PL_ICON_LARGE / 2, -PL_ICON_LARGE);
			startItemDrag(this, { item.get() }, { origin }, origin + QPoint(16, 24));
			Finder::instance().dragEnded();
			/* Moved away (into another folder, or the Trash): its window
			 * goes, as for any folder that disappears from under it. */
			if (!QFileInfo::exists(m_path)) {
				close();
			}
		};
	}
	platinumSetProxyIcon(this, kind, dragged);
}

void FolderWindow::setViewOptions(const ViewOptions &options) {
	if (options == m_state.options) {
		return;
	}
	m_editor.cancel();
	/* Icons placed for one size don't fit the other's grid: they flow
	 * again. */
	if (options.icons.size != m_state.options.icons.size) {
		m_state.icons.clear();
	}
	const bool snapNow = options.icons.arrangement == IconArrangement::SnapToGrid &&
		m_state.options.icons.arrangement != IconArrangement::SnapToGrid;
	m_state.options = options;
	if (!(options.list.columns & columnBit(m_sortColumn))) {
		m_sortColumn = m_state.sortColumn = COL_NAME;
	}
	m_editor.setBeside(smallIcons());
	saveStateSoon();
	relayout();
	if (snapNow && m_mode == ViewMode::Icons) {
		arrange(Arrange::CleanUp); /* onto the grid, now */
	}
	update();
	Finder::instance().notifyState();
}

void FolderWindow::sortBy(int column) {
	if (column < 0 || column >= COL_COUNT || column == m_sortColumn ||
			!(m_state.options.list.columns & columnBit(column))) {
		return;
	}
	m_sortColumn = m_state.sortColumn = column;
	saveStateSoon();
	relayout();
	update();
	Finder::instance().notifyState();
}

std::vector<int> FolderWindow::columns() const {
	/* Name first, always; the rest as the user put them, any the order
	 * doesn't mention where the Mac has them. */
	std::vector<int> order = { COL_NAME };
	for (int c : m_state.columnOrder) {
		order.push_back(c);
	}
	for (int c : COLUMN_ORDER) {
		if (std::find(order.begin(), order.end(), c) == order.end()) {
			order.push_back(c);
		}
	}
	std::vector<int> shown;
	for (int c : order) {
		if (c == COL_NAME || (m_state.options.list.columns & columnBit(c))) {
			shown.push_back(c);
		}
	}
	return shown;
}

int FolderWindow::columnWidth(int column) const {
	return m_state.columnWidths.value(column, columnInfo(column).width);
}

bool FolderWindow::columnsCustomized() const {
	return !m_state.columnOrder.isEmpty() || !m_state.columnWidths.isEmpty();
}

void FolderWindow::resetColumns() {
	if (!columnsCustomized()) {
		return;
	}
	m_state.columnOrder.clear();
	m_state.columnWidths.clear();
	saveStateSoon();
	relayout();
	update();
	Finder::instance().notifyState();
}

void FolderWindow::setSortReversed(bool reversed) {
	if (reversed == m_state.sortReversed) {
		return;
	}
	m_state.sortReversed = reversed;
	saveStateSoon();
	relayout();
	update();
	Finder::instance().notifyState();
}

/* Drops `column` where the pointer is: before the column under it, or
 * after the last. The full order (hidden columns too) is what's kept. */
void FolderWindow::moveColumn(int column, int contentX) {
	std::vector<int> order;
	for (int c : columns()) {
		if (c != COL_NAME && c != column) {
			order.push_back(c);
		}
	}
	size_t at = order.size();
	int x = columnWidth(COL_NAME);
	for (size_t i = 0; i < order.size(); i++) {
		if (contentX < x + columnWidth(order[i]) / 2) {
			at = i;
			break;
		}
		x += columnWidth(order[i]);
	}
	order.insert(order.begin() + static_cast<long>(at), column);
	/* Hidden columns keep their places after the shown ones. */
	QList<int> full(order.begin(), order.end());
	for (int c : COLUMN_ORDER) {
		if (c != COL_NAME && !full.contains(c)) {
			full << c;
		}
	}
	for (int c : m_state.columnOrder) {
		if (!full.contains(c)) {
			full << c;
		}
	}
	if (full == m_state.columnOrder) {
		return;
	}
	m_state.columnOrder = full;
	saveStateSoon();
	relayout();
	update();
	Finder::instance().notifyState();
}

void FolderWindow::preferencesChanged() {
	for (FolderWindow *w : openWindows()) {
		w->relayout();
		w->update();
	}
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
	if (!folderExists(m_path)) {
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
	m_buttonDown = nullptr;
	m_marquee = false;
	m_marqueeBase.clear();
	m_marqueeScroll.stop();
	auto fresh = listFolder(m_path);
	m_editor.retarget(fresh);
	/* Contents may have changed anywhere below: sizes are added up again. */
	if (m_sizingCancel) {
		*m_sizingCancel = true;
		m_sizingCancel.reset();
	}
	m_folderSizes.clear();
	m_sizing.clear();
	m_renameTimer.stop();
	m_renameItem = nullptr;
	m_items = std::move(fresh);

	m_children.clear();
	for (const QString &dir : QSet<QString>(m_expanded)) {
		if (folderExists(dir)) {
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

/* What the current view shows: list rows include expanded contents. */
std::vector<Item *> FolderWindow::visibleItems() {
	std::vector<Item *> items;
	if (m_mode == ViewMode::List) {
		for (const Row &row : m_rows) {
			items.push_back(row.item);
		}
	} else {
		for (auto &item : m_items) {
			items.push_back(item.get());
		}
	}
	return items;
}

void FolderWindow::selectAll() {
	m_editor.commit();
	forEachItem([](Item *item) { item->selected = false; });
	for (Item *item : visibleItems()) {
		item->selected = true;
	}
	update();
	Finder::instance().notifyState();
}

/* An item's icon and name, in content coordinates. */
QRect FolderWindow::selectionRect(Item *item) {
	if (m_mode == ViewMode::Icons) {
		return itemRect(*item);
	}
	const ListMetrics m = metrics();
	for (size_t r = 0; r < m_rows.size(); r++) {
		if (m_rows[r].item != item) {
			continue;
		}
		const int d = m_rows[r].depth * INDENT, top = static_cast<int>(r) * m.rowH;
		const int nameW = columnWidth(COL_NAME) - m.nameX - d - 6;
		Text name(item->visibleName(), std::max(8, nameW), item->nameFont());
		return QRect(QPoint(m.iconX + d, top + m.iconY),
			QPoint(m.nameX + d + name.inkWidth() + 1,
				std::max(top + m.iconY + m.icon - 1, top + m.baseline + 3)));
	}
	return {};
}

/* List view, like the Finder's: a row's icon and name select it; its
 * blank columns start a selection rectangle instead. */
bool FolderWindow::listSelectableAt(QPoint windowPos, Item *item) {
	return m_mode != ViewMode::List || !item ||
		selectionRect(item).contains(toContent(windowPos));
}

void FolderWindow::updateMarquee(QPoint windowPos) {
	if (!m_marquee) {
		return;
	}
	m_marqueePointer = windowPos;
	const int top = contentTop(), bottom = top + viewHeight() - 1, right = viewWidth() - 1;
	int dx = windowPos.x() < 0 ? windowPos.x() : windowPos.x() > right ? windowPos.x() - right : 0;
	int dy = windowPos.y() < top ? windowPos.y() - top
		: windowPos.y() > bottom ? windowPos.y() - bottom : 0;
	dx = std::clamp(dx, -ARROW_STEP, ARROW_STEP);
	dy = std::clamp(dy, -ARROW_STEP, ARROW_STEP);
	if (dx || dy) {
		scrollTo(m_scrollX + dx, m_scrollY + dy);
		if (!m_marqueeScroll.isActive()) {
			m_marqueeScroll.start();
		}
	} else {
		m_marqueeScroll.stop();
	}
	const QPoint clamped(std::clamp(windowPos.x(), 0, right), std::clamp(windowPos.y(), top, bottom));
	m_marqueeEnd = toContent(clamped);
	const QRect area = QRect(m_marqueeStart, m_marqueeEnd).normalized();
	forEachItem([](Item *item) { item->selected = false; });
	for (Item *item : m_marqueeBase) {
		item->selected = true;
	}
	for (Item *item : visibleItems()) {
		if (selectionRect(item).intersects(area)) {
			item->selected = m_marqueeExtend ? !m_marqueeBase.contains(item) : true;
		}
	}
	update();
	Finder::instance().notifyState();
}

void FolderWindow::endMarquee() {
	m_marquee = false;
	m_marqueeBase.clear();
	m_marqueeScroll.stop();
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
	for (int c : columns()) {
		w += columnWidth(c);
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
	} else if (m_mode == ViewMode::Buttons) {
		layoutButtons();
	} else {
		buildRows();
		m_contentHeight = static_cast<int>(m_rows.size()) * metrics().rowH;
		calculateFolderSizes();
	}
	scrollTo(m_scrollX, m_scrollY);
	updateStandardSize();
}

void FolderWindow::updateStandardSize() {
	if (!isVisible()) {
		return; /* no surface yet: open() sends it once shown */
	}
	int w = m_contentWidth;
	if (m_mode == ViewMode::List) {
		w = listWidth();
	} else if (m_mode == ViewMode::Buttons) {
		const int cellW = buttonCell().width();
		const int cols = std::max(1, (viewWidth() - 2 * MARGIN) / cellW);
		const int used = std::min(cols, std::max(1, static_cast<int>(m_items.size())));
		w = 2 * MARGIN + used * cellW;
	}
	/* Scroll bars stay; an empty folder still gets a usable window. */
	const QSize size(std::max(w, 200) + SB_WIDTH - 1,
		std::max(contentTop() + m_contentHeight, contentTop() + 38) + SB_WIDTH - 1);
	if (size != m_standardSize) {
		m_standardSize = size;
		platinumSetStandardSize(this, size);
	}
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
	sb_smart(&sb, viewHeight(), viewHeight() + scrollMaxY());
	int range = sb_thumb_span(&sb);
	sb.thumb = sb.enabled ? m_scrollY * range / scrollMaxY() : 0;
	return sb;
}

pl_scrollbar FolderWindow::horizontalBar() const {
	pl_scrollbar sb{};
	sb.length = width() - SB_WIDTH + 3;
	sb.enabled = scrollMaxX() > 0;
	sb_smart(&sb, viewWidth(), viewWidth() + scrollMaxX());
	int range = sb_thumb_span(&sb);
	sb.thumb = sb.enabled ? m_scrollX * range / scrollMaxX() : 0;
	return sb;
}

QString FolderWindow::headerText() const {
	const int n = static_cast<int>(m_items.size());
	QString items = n == 1 ? QStringLiteral("1 item") : QStringLiteral("%1 items").arg(n);
	const qint64 free = QStorageInfo(spacePath(m_path)).bytesAvailable();
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

FolderWindow::IconGrid FolderWindow::iconGrid() const {
	const bool tight = tightGrid();
	if (smallIcons()) {
		return tight ? IconGrid{ 104, 20, 0 } : IconGrid{ 128, 24, 0 };
	}
	return tight ? IconGrid{ 64, 56, (64 - PL_ICON_LARGE) / 2 } :
		IconGrid{ 80, 64, (80 - PL_ICON_LARGE) / 2 };
}

bool FolderWindow::smallIcons() const {
	return m_state.options.icons.size == IconSize::Small;
}

int FolderWindow::iconColumns() const {
	return std::max(1, (viewWidth() - 2 * MARGIN) / iconGrid().cellW);
}

QPoint FolderWindow::iconSlot(int i) const {
	const IconGrid g = iconGrid();
	const int cols = iconColumns();
	return QPoint(MARGIN + (i % cols) * g.cellW + g.inset, MARGIN + (i / cols) * g.cellH);
}

/* Placed icons stay put (spatial); the rest flow into free grid cells.
 * Keep Arranged puts them all in order instead. */
void FolderWindow::layoutIcons() {
	const IconGrid g = iconGrid();
	std::vector<Item *> items;
	for (auto &item : m_items) {
		items.push_back(item.get());
	}
	const auto slot = [this](int i) { return iconSlot(i); };
	const IconViewOptions &o = m_state.options.icons;
	if (o.arrangement == IconArrangement::KeepArranged) {
		sortIcons(items, o.keepBy);
		for (size_t i = 0; i < items.size(); i++) {
			items[i]->pos = slot(static_cast<int>(i));
		}
	} else {
		placeIcons(items, m_state.icons, slot, g.cellW, g.cellH, g.inset);
	}
	m_contentHeight = 0;
	m_contentWidth = 0;
	for (Item *item : items) {
		m_contentHeight = std::max(m_contentHeight, item->pos.y() + g.cellH + MARGIN);
		m_contentWidth = std::max(m_contentWidth, item->pos.x() - g.inset + g.cellW + MARGIN);
	}
}

/* Clean Up / Arrange: every icon gets a grid position, remembered like
 * one the user dragged there. Kept arranged, Arrange changes the order
 * kept. */
void FolderWindow::arrange(Arrange how) {
	if (m_mode != ViewMode::Icons || m_items.empty()) {
		return;
	}
	IconViewOptions &o = m_state.options.icons;
	if (o.arrangement == IconArrangement::KeepArranged) {
		if (how != Arrange::CleanUp && how != o.keepBy) {
			o.keepBy = how;
			saveStateSoon();
			relayout();
			update();
		}
		return;
	}
	const IconGrid g = iconGrid();
	const int cols = iconColumns();
	std::vector<Item *> items;
	int lowest = 0;
	for (auto &item : m_items) {
		items.push_back(item.get());
		lowest = std::max(lowest, item->pos.y());
	}
	/* Enough rows for every icon, and for Clean Up to reach the lowest one. */
	const int rows = std::max(static_cast<int>(items.size()) / cols + 1, lowest / g.cellH + 1) + 1;
	arrangeIcons(items, how, [this](int i) { return iconSlot(i); }, cols * rows);
	m_state.icons.clear();
	for (Item *item : items) {
		m_state.icons.insert(item->key(), item->pos);
	}
	saveStateSoon();
	relayout();
	update();
}

void FolderWindow::snapToGrid(const std::vector<Item *> &moved) {
	const IconGrid g = iconGrid();
	const int cols = iconColumns();
	int lowest = 0;
	for (auto &item : m_items) {
		lowest = std::max(lowest, item->pos.y());
	}
	const int count = cols * (lowest / g.cellH + static_cast<int>(m_items.size()) + 2);
	/* Slots the icons left alone sit on. */
	QSet<int> taken;
	for (auto &item : m_items) {
		if (std::find(moved.begin(), moved.end(), item.get()) != moved.end()) {
			continue;
		}
		const int col = (item->pos.x() - MARGIN - g.inset) / g.cellW;
		const int row = (item->pos.y() - MARGIN) / g.cellH;
		const int s = row * cols + col;
		if (col >= 0 && col < cols && row >= 0 && iconSlot(s) == item->pos) {
			taken.insert(s);
		}
	}
	for (Item *item : moved) {
		const int s = nearestFreeSlot(item->pos, [this](int i) { return iconSlot(i); }, count, taken);
		if (s >= 0) {
			taken.insert(s);
			item->pos = iconSlot(s);
			m_state.icons.insert(item->key(), item->pos);
		}
	}
}

QRect FolderWindow::smallLabelBox(Item &item) {
	const int maxW = item.selected ? 1000 : iconGrid().cellW - SMALL_LABEL_X - 4;
	Text t(item.visibleName(), std::max(8, maxW), item.nameFont());
	return QRect(item.pos.x() + SMALL_LABEL_X - 2, item.pos.y() + 1, t.inkWidth() + 4, 14);
}

QRect FolderWindow::itemRect(Item &item) {
	if (smallIcons()) {
		return QRect(item.pos, QSize(PL_ICON_SMALL, PL_ICON_SMALL)).united(smallLabelBox(item));
	}
	return iconItemRect(item, item.pos.x(), item.pos.y());
}

bool FolderWindow::itemHit(Item &item, QPoint p) {
	if (smallIcons()) {
		return QRect(item.pos, QSize(PL_ICON_SMALL, PL_ICON_SMALL)).contains(p) ||
			smallLabelBox(item).contains(p);
	}
	return iconItemContains(item, item.pos.x(), item.pos.y(), p);
}

bool FolderWindow::labelHit(Item &item, QPoint p) {
	return smallIcons() ? smallLabelBox(item).contains(p) :
		iconLabelContains(item, item.pos.x(), item.pos.y(), p);
}

void FolderWindow::paintItem(pl_canvas *c, Item &item, bool showLabel) {
	if (!smallIcons()) {
		paintIconItem(c, item, item.pos.x(), item.pos.y(), false, showLabel);
		return;
	}
	paintIcon(c, item, item.pos.x(), item.pos.y(), PL_ICON_SMALL, item.selected || item.dropTarget);
	if (showLabel) {
		paintItemLabel(c, item);
	}
}

void FolderWindow::paintItemLabel(pl_canvas *c, Item &item) {
	if (!smallIcons()) {
		paintIconLabel(c, item, item.pos.x(), item.pos.y(), false);
		return;
	}
	const QRect box = smallLabelBox(item);
	const int maxW = item.selected ? 1000 : iconGrid().cellW - SMALL_LABEL_X - 4;
	Text t(item.visibleName(), std::max(8, maxW), item.nameFont());
	uint32_t ink = C_BLACK;
	if (item.selected) {
		pl_fill(c, box.left(), box.top(), box.right(), box.bottom() - 1, C_BLACK);
		ink = C_WHITE;
	}
	pl_text(c, t.t, box.left() + 2, box.top() + 10, ink);
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
	const int viewH = viewHeight(), cellH = iconGrid().cellH;
	pl_fill(content, 0, m_scrollY, viewWidth() - 1, m_scrollY + viewH - 1, C_WHITE);
	for (auto &item : m_items) {
		if (item->pos.y() + cellH >= m_scrollY && item->pos.y() <= m_scrollY + viewH) {
			const bool editing = m_editor.editing(item.get());
			paintItem(content, *item, !editing);
			if (editing) {
				m_editor.paint(content, item->pos.x(), item->pos.y());
			}
		}
	}
	/* Selected icons' whole names, over their neighbours. */
	for (auto &item : m_items) {
		if (item->selected && !m_editor.editing(item.get())) {
			paintItemLabel(content, *item);
		}
	}
}

/* ---- button view --------------------------------------------------------------- */

int FolderWindow::buttonSide() const {
	return m_state.options.buttons.size == IconSize::Small ? SMALL_BUTTON : BUTTON;
}

QSize FolderWindow::buttonCell() const {
	const bool tight = tightGrid();
	if (m_state.options.buttons.size == IconSize::Small) {
		return tight ? QSize(64, 42) : QSize(72, 46);
	}
	return tight ? QSize(76, 74) : QSize(84, 80);
}

static int buttonIcon(int side) {
	return side == BUTTON ? PL_ICON_LARGE : PL_ICON_SMALL;
}

/* item->pos is the icon's top-left; its button surrounds it. The buttons
 * are kept in order (View Options' "Keep arranged", by Name unless set). */
void FolderWindow::layoutButtons() {
	const QSize cell = buttonCell();
	const int side = buttonSide(), icon = buttonIcon(side);
	const int cols = std::max(1, (viewWidth() - 2 * MARGIN) / cell.width());
	std::vector<Item *> items;
	for (auto &item : m_items) {
		items.push_back(item.get());
	}
	sortIcons(items, m_state.options.buttons.keepBy);
	int i = 0;
	for (Item *item : items) {
		item->pos = QPoint(MARGIN + (i % cols) * cell.width() + (cell.width() - icon) / 2,
			MARGIN + (i / cols) * cell.height() + (side - icon) / 2);
		i++;
	}
	const int rows = (static_cast<int>(m_items.size()) + cols - 1) / cols;
	m_contentHeight = 2 * MARGIN + rows * cell.height();
	m_contentWidth = 0;
}

QRect FolderWindow::buttonRect(const Item *item) const {
	const int side = buttonSide();
	const int inset = (side - buttonIcon(side)) / 2;
	return QRect(item->pos.x() - inset, item->pos.y() - inset, side, side);
}

void FolderWindow::paintButtons(pl_canvas *content) {
	pl_fill(content, 0, m_scrollY, viewWidth() - 1, m_scrollY + viewHeight() - 1, C_WHITE);
	for (auto &item : m_items) {
		const QRect b = buttonRect(item.get());
		const bool pressed = item.get() == m_buttonDown && m_buttonInside;
		pl_bevel_button_paint(content, b.x(), b.y(), b.width(), b.height(), pressed);
		const int shift = pressed ? 1 : 0;
		pl_icon_paint_label(content, item->pos.x() + shift, item->pos.y() + shift, item->iconKind(),
			buttonIcon(buttonSide()), item->dropTarget, item->labelColor());

		const Text &label = item->shownLabel();
		const int lx = b.center().x() - label.inkWidth() / 2, top = b.bottom() + 3;
		uint32_t ink = C_BLACK;
		if (item->selected) {
			pl_fill(content, lx - 2, top, lx + label.inkWidth() + 1, top + 12, C_BLACK);
			ink = C_WHITE;
		}
		pl_text(content, label.t, lx, top + 10, ink);
	}
}

/* The button itself, or its name below it. */
Item *FolderWindow::buttonAt(QPoint p, bool *onButton) {
	for (auto &item : m_items) {
		const QRect b = buttonRect(item.get());
		if (b.contains(p)) {
			*onButton = true;
			return item.get();
		}
		const int w = item->shownLabel().inkWidth();
		if (QRect(b.center().x() - w / 2 - 2, b.bottom() + 3, w + 4, 13).contains(p)) {
			*onButton = false;
			return item.get();
		}
	}
	return nullptr;
}

/* ---- list view ----------------------------------------------------------------- */

FolderWindow::ListMetrics FolderWindow::metrics() const {
	switch (m_state.options.list.size) {
	case IconSize::Standard: return { 36, PL_ICON_LARGE, 28, 2, 66, 22 };
	case IconSize::Tiny: return { 17, TINY_ICON, 30, 2, 46, 12 };
	default: return { 19, PL_ICON_SMALL, 28, 1, 50, 11 };
	}
}

void FolderWindow::sortItems(std::vector<std::unique_ptr<Item>> &items) const {
	std::vector<Item *> order;
	for (auto &item : items) {
		order.push_back(item.get());
	}
	sortByColumn(order, m_sortColumn);
	if (m_state.sortReversed) {
		std::reverse(order.begin(), order.end());
	}
	QHash<Item *, int> rank;
	for (size_t i = 0; i < order.size(); i++) {
		rank.insert(order[i], static_cast<int>(i));
	}
	std::sort(items.begin(), items.end(), [&](const auto &a, const auto &b) {
		return rank.value(a.get()) < rank.value(b.get());
	});
}

/* Calculate folder sizes: each folder in the list added up on another
 * thread (it can take a while), filled in as it finishes. */
void FolderWindow::calculateFolderSizes() {
	if (m_mode != ViewMode::List || !m_state.options.list.folderSizes) {
		return;
	}
	if (!m_sizingCancel) {
		m_sizingCancel = std::make_shared<std::atomic<bool>>(false);
	}
	auto cancel = m_sizingCancel;
	FolderWindow *self = this;
	forEachItem([&](Item *item) {
		if (!item->isDir || item->isVirtual || item->isAlias || item->folderSize >= 0) {
			return;
		}
		const auto known = m_folderSizes.constFind(item->path);
		if (known != m_folderSizes.constEnd()) {
			item->folderSize = *known;
			return;
		}
		if (m_sizing.contains(item->path)) {
			return;
		}
		m_sizing.insert(item->path);
		const QString path = item->path;
		QThreadPool::globalInstance()->start([self, cancel, path] {
			qint64 total = 0;
			/* Not into linked folders: an alias isn't its original's size. */
			QDirIterator it(path, QDir::AllEntries | QDir::Hidden | QDir::System |
				QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
			while (it.hasNext() && !*cancel) {
				it.next();
				const QFileInfo info = it.fileInfo();
				if (info.isFile() && !info.isSymLink()) {
					total += info.size();
				}
			}
			QMetaObject::invokeMethod(qApp, [self, cancel, path, total] {
				if (!*cancel) { /* else the window reloaded or went */
					self->folderSized(path, total);
				}
			}, Qt::QueuedConnection);
		});
	});
}

void FolderWindow::folderSized(const QString &path, qint64 bytes) {
	m_sizing.remove(path);
	m_folderSizes.insert(path, bytes);
	forEachItem([&](Item *item) {
		if (item->path == path) {
			item->folderSize = bytes;
		}
	});
	if (m_sortColumn == COL_SIZE) {
		relayout();
	}
	update();
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
		if (!vfsIsVirtual(path)) {
			m_watcher.removePath(path);
		}
	} else {
		m_expanded.insert(path);
		m_children[path] = listFolder(path);
		if (!vfsIsVirtual(path)) {
			m_watcher.addPath(path);
		}
	}
	relayout();
	update();
}

int FolderWindow::columnAt(int contentX) const {
	int x = 0;
	for (int c : columns()) {
		if (contentX < x + columnWidth(c)) {
			return c;
		}
		x += columnWidth(c);
	}
	return -1;
}

QRect FolderWindow::sortDirectionButton() const {
	return QRect(viewWidth(), HEADER_H, width() - viewWidth(), BAND_H);
}

int FolderWindow::columnX(int column) const {
	int x = 0;
	for (int c : columns()) {
		if (c == column) {
			return x;
		}
		x += columnWidth(c);
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
	for (int c : columns()) {
		const int x1 = x + columnWidth(c) - 1;
		paintColumn(x, x1, c == m_sortColumn, columnInfo(c).title,
			x + (c == COL_NAME ? metrics().nameX : COL_TEXT_X));
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
	const ListMetrics m = metrics();
	const std::vector<int> shown = columns();
	const QDateTime now = QDateTime::currentDateTime();

	/* Column backgrounds: the sorted column a shade darker. */
	int x = 0;
	for (int col : shown) {
		pl_fill(c, x, y0, x + columnWidth(col) - 1, y1,
			col == m_sortColumn ? GRAY(0xD) : GRAY(0xE));
		x += columnWidth(col);
	}
	pl_fill(c, x, y0, std::max(x, right), y1, GRAY(0xE));

	const int first = std::max(0, m_scrollY / m.rowH);
	const int last = std::min<int>(m_rows.size() - 1, (m_scrollY + viewH) / m.rowH);
	for (int r = first; r <= last; r++) {
		const Row &row = m_rows[r];
		Item *item = row.item;
		const int top = r * m.rowH, d = row.depth * INDENT;
		pl_hline(c, 0, std::max(listWidth(), right), top + m.rowH - 1, C_WHITE);

		if (item->isDir) {
			panelDisclosure(c, TRI_X + d, top + (m.rowH - 13) / 2, m_expanded.contains(item->path));
		}
		const bool highlight = item->selected || item->dropTarget;
		if (m.icon == TINY_ICON) {
			paintTinyIcon(c, *item, m.iconX + d, top + m.iconY, highlight);
		} else {
			paintIcon(c, *item, m.iconX + d, top + m.iconY, m.icon, highlight);
		}

		const int nameW = columnWidth(COL_NAME) - m.nameX - d - 6;
		Text name(item->visibleName(), std::max(8, nameW), item->nameFont());
		uint32_t ink = C_BLACK;
		if (item->selected) {
			pl_fill(c, m.nameX + d - 2, top + m.baseline - 9, m.nameX + d + name.inkWidth() + 1,
				top + m.baseline + 3, C_BLACK);
			ink = C_WHITE;
		}
		pl_text(c, name.t, m.nameX + d, top + m.baseline, ink);

		int cx = 0;
		for (int col : shown) {
			const int w = columnWidth(col);
			if (col != COL_NAME) {
				Text t(columnText(*item, col, m_state.options.list, w - 2 * COL_TEXT_X, now),
					w - 2 * COL_TEXT_X, PL_FONT_VIEWS);
				pl_text(c, t.t, cx + COL_TEXT_X, top + m.baseline, C_BLACK);
			}
			cx += w;
		}
	}
}

/* ---- painting ---------------------------------------------------------------- */

QImage FolderWindow::printImage() {
	const int viewW = viewWidth(), viewH = viewHeight();
	if (viewW <= 0 || viewH <= 0) {
		return {};
	}
	const bool list = m_mode == ViewMode::List;
	const int W = std::max(64, list ? listWidth() : std::max(m_contentWidth, viewW));
	const int H = std::max(1, m_contentHeight);
	const int band = list ? BAND_H : 0;
	Pixels px(W, band + H);
	px.img.fill(Qt::white);

	/* Printed as it would look with nothing selected or being dropped on. */
	std::vector<std::pair<Item *, std::pair<bool, bool>>> flags;
	const auto unmark = [&](Item *item) {
		flags.push_back({ item, { item->selected, item->dropTarget } });
		item->selected = item->dropTarget = false;
	};
	for (auto &item : m_items) {
		unmark(item.get());
	}
	for (const Row &row : m_rows) {
		unmark(row.item);
	}
	const int scrollX = m_scrollX, scrollY = m_scrollY;
	const bool marquee = m_marquee;
	m_marquee = false;

	/* The painters draw what's in view: tile the whole by scrolling. */
	for (int sx = 0; sx < W; sx += viewW) {
		m_scrollX = sx;
		const int w = std::min(viewW, W - sx);
		if (list) {
			pl_canvas header = { px.c.px + sx, px.c.stride, sx, 0, w, BAND_H };
			paintColumnHeaders(&header);
		}
		for (int sy = 0; sy < H; sy += viewH) {
			m_scrollY = sy;
			pl_canvas tile = { px.c.px + (band + sy) * px.c.stride + sx, px.c.stride, sx, sy,
				w, std::min(viewH, H - sy) };
			if (list) {
				paintList(&tile);
			} else if (m_mode == ViewMode::Buttons) {
				paintButtons(&tile);
			} else {
				paintIcons(&tile);
			}
		}
	}

	m_scrollX = scrollX;
	m_scrollY = scrollY;
	m_marquee = marquee;
	/* Backwards: an item both listed and a row was saved twice, the
	 * second time already unmarked. */
	for (auto it = flags.rbegin(); it != flags.rend(); ++it) {
		it->first->selected = it->second.first;
		it->first->dropTarget = it->second.second;
	}
	return px.img;
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
	} else if (m_mode == ViewMode::Buttons) {
		pl_hline(c, 0, W - 1, HEADER_H, C_BLACK);
		paintButtons(&content);
	} else {
		pl_canvas band = { c->px + HEADER_H * c->stride, c->stride, m_scrollX, 0, W, BAND_H };
		paintColumnHeaders(&band);
		if (m_headerDrag == HeaderDrag::Move) {
			/* The header being moved, following the pointer. */
			const int x0 = columnX(m_headerColumn) - m_scrollX + m_headerPointerX - m_headerPressX;
			const int x1 = x0 + columnWidth(m_headerColumn) - 1;
			pl_outline(c, x0, HEADER_H, x1, LIST_TOP - 1, C_BLACK);
			pl_outline(c, x0 + 1, HEADER_H + 1, x1 - 1, LIST_TOP - 2, C_BLACK);
		}
		/* The sort-direction button: a pyramid, upside down when reversed. */
		const QRect b = sortDirectionButton();
		pl_fill(c, b.left(), b.top(), b.right(), b.bottom(), GRAY(0xC));
		pl_hline(c, b.left(), b.right(), b.top(), C_WHITE);
		pl_vline(c, b.left(), b.top(), b.bottom(), C_WHITE);
		pl_hline(c, b.left(), b.right(), b.bottom(), GRAY(0x3));
		pl_vline(c, b.right(), b.top(), b.bottom(), GRAY(0x8));
		const int cx = b.center().x(), cy = b.center().y();
		for (int row = 0; row < 4; row++) {
			const int y = m_state.sortReversed ? cy + 2 - 2 * row : cy - 4 + 2 * row;
			pl_hline(c, cx - row, cx + row, y, C_BLACK);
		}
		paintList(&content);
	}
	if (m_marquee) {
		paintMarquee(&content, QRect(m_marqueeStart, m_marqueeEnd).normalized());
	}

	/* Scroll bars share their outer lines with the window frame. The
	 * compositor draws the resize box in the corner. */
	pl_scrollbar v = verticalBar();
	pl_scrollbar_paint(c, W - SB_WIDTH + 1, top - 1, &v, pl_accent_current());
	pl_scrollbar h = horizontalBar();
	pl_scrollbar_paint(c, -1, H - SB_WIDTH + 1, &h, pl_accent_current());

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
	if (m_mode == ViewMode::Buttons) {
		bool onButton;
		return buttonAt(p, &onButton);
	}
	if (m_mode == ViewMode::Icons) {
		for (auto it = m_items.rbegin(); it != m_items.rend(); ++it) {
			if (itemHit(**it, p)) {
				return it->get();
			}
		}
		return nullptr;
	}
	const int r = p.y() / metrics().rowH;
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

void FolderWindow::showContextMenuAt(QPoint pos) {
	const int barX = width() - SB_WIDTH + 1, barY = height() - SB_WIDTH + 1;
	if (pos.y() < contentTop() || pos.x() >= barX || pos.y() >= barY) {
		return;
	}
	if (m_editor.item()) {
		m_editor.commit();
	}
	m_renameTimer.stop();
	bool onTriangle = false;
	Item *hit = itemAt(pos, &onTriangle);
	if (!listSelectableAt(pos, hit)) {
		hit = nullptr;
	}
	if (!hit || !hit->selected) {
		forEachItem([&](Item *item) { item->selected = item == hit; });
	}
	update();
	Finder::instance().showContextMenu(this, hit);
}

void FolderWindow::mousePressEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	const int barX = width() - SB_WIDTH + 1, barY = height() - SB_WIDTH + 1;
	if (isContextClick(e)) {
		/* In the icons' area: as a plain click selects, then the menu. */
		if (pos.y() < contentTop() || pos.x() >= barX || pos.y() >= barY) {
			return;
		}
		if (e->button() == Qt::LeftButton && (scrollMaxX() > 0 || scrollMaxY() > 0)) {
			m_grabPending = true;
			m_grabPress = pos;
			m_grabScroll = QPoint(m_scrollX, m_scrollY);
			return;
		}
		contextClick(pos);
		return;
	}
	if (e->button() != Qt::LeftButton) {
		return;
	}

	/* Scroll bars. */
	if (pos.x() >= barX && pos.y() >= contentTop() - 1 && pos.y() < barY) {
		pl_scrollbar v = verticalBar();
		const int along = pos.y() - (contentTop() - 1);
		m_sbVertical = true;
		m_sbPart = sb_hit(&v, along);
		if (m_sbPart == SB_THUMB_PART) {
			m_thumbGrab = along - (sb_track_start(&v) + v.thumb);
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
			m_thumbGrab = along - (sb_track_start(&h) + h.thumb);
		} else if (m_sbPart != SB_NONE) {
			scrollStep();
			m_repeat.start(REPEAT_FIRST_MS);
		}
		return;
	}
	/* List view's column headers: the direction button reverses the
	 * order; a divider resizes the column to its left; a header sorts
	 * when clicked, and moves when dragged (Name stays first). */
	if (m_mode == ViewMode::List && pos.y() >= HEADER_H && pos.y() < LIST_TOP) {
		if (sortDirectionButton().contains(pos)) {
			setSortReversed(!sortReversed());
			return;
		}
		const int cx = pos.x() + m_scrollX;
		int x = 0;
		for (int c : columns()) {
			x += columnWidth(c);
			if (std::abs(cx - x) <= 3) {
				m_headerDrag = HeaderDrag::Resize;
				m_headerColumn = c;
				m_headerPressX = pos.x();
				m_headerStartWidth = columnWidth(c);
				return;
			}
		}
		const int col = columnAt(cx);
		if (col <= COL_NAME) {
			sortBy(col);
			return;
		}
		m_headerDrag = HeaderDrag::Press;
		m_headerColumn = col;
		m_headerPressX = m_headerPointerX = pos.x();
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
	if (m_mode == ViewMode::Buttons) {
		/* Buttons open on release, tracked like any push button. */
		bool onButton = false;
		Item *b = buttonAt(content, &onButton);
		if (b && onButton) {
			m_buttonDown = b;
			m_buttonInside = true;
			update();
			Finder::instance().setFront(this);
			return;
		}
	}
	bool onTriangle = false;
	Item *hit = itemAt(pos, &onTriangle);
	if (onTriangle) {
		toggleExpanded(hit);
		return;
	}
	if (!listSelectableAt(pos, hit)) {
		hit = nullptr;
	}
	const bool extend = e->modifiers() & Qt::ShiftModifier;
	/* Icon view: a click on the name of the one selected icon starts a
	 * rename, unless it turns into a double-click or a drag first. */
	m_renameTimer.stop();
	if (m_mode == ViewMode::Icons && hit && hit->selected && !extend &&
			selectedItems().size() == 1 &&
			labelHit(*hit, content)) {
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
	if (!hit && m_mode != ViewMode::Buttons) {
		m_marquee = true;
		m_marqueeExtend = extend;
		m_marqueeBase.clear();
		for (Item *item : visibleItems()) {
			if (item->selected) {
				m_marqueeBase.insert(item);
			}
		}
		m_marqueeStart = m_marqueeEnd = content;
		m_marqueePointer = pos;
	}
	update();
	Finder::instance().setFront(this);
	Finder::instance().notifyState();
}

void FolderWindow::contextClick(QPoint pos) {
	if (m_editor.item()) {
		m_editor.commit();
	}
	m_renameTimer.stop();
	bool onTriangle = false;
	Item *hit = itemAt(pos, &onTriangle);
	if (!listSelectableAt(pos, hit)) {
		hit = nullptr;
	}
	if (!hit || !hit->selected) {
		forEachItem([&](Item *item) { item->selected = item == hit; });
	}
	update();
	Finder::instance().showContextMenu(this, hit);
}

void FolderWindow::mouseMoveEvent(QMouseEvent *e) {
	if (m_grabPending) {
		const QPoint pos = e->position().toPoint();
		if (!m_grabbing && (pos - m_grabPress).manhattanLength() >= QApplication::startDragDistance()) {
			m_grabbing = true;
			setCursor(Qt::ClosedHandCursor);
		}
		if (m_grabbing) {
			/* The content follows the hand. */
			const QPoint d = pos - m_grabPress;
			scrollTo(m_grabScroll.x() - d.x(), m_grabScroll.y() - d.y());
		}
		return;
	}
	if (m_headerDrag != HeaderDrag::None) {
		const int x = e->position().toPoint().x();
		if (m_headerDrag == HeaderDrag::Resize) {
			const int w = std::clamp(m_headerStartWidth + x - m_headerPressX,
				m_headerColumn == COL_NAME ? 100 : 40, 800);
			if (w != columnWidth(m_headerColumn)) {
				m_state.columnWidths.insert(m_headerColumn, w);
				relayout();
				update();
			}
		} else {
			if (m_headerDrag == HeaderDrag::Press &&
					std::abs(x - m_headerPressX) >= QApplication::startDragDistance()) {
				m_headerDrag = HeaderDrag::Move;
			}
			m_headerPointerX = x;
			update();
		}
		return;
	}
	if (m_buttonDown) {
		const bool inside = buttonRect(m_buttonDown).contains(toContent(e->position().toPoint()));
		if (inside != m_buttonInside) {
			m_buttonInside = inside;
			update();
		}
		return;
	}
	if (m_marquee && (e->buttons() & Qt::LeftButton)) {
		updateMarquee(e->position().toPoint());
		return;
	}
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
			/* List rows: centre a large outline on the row's icon. */
			const ListMetrics m = metrics();
			for (size_t r = 0; r < m_rows.size(); r++) {
				if (m_rows[r].item == item) {
					QPoint icon(m.iconX + m_rows[r].depth * INDENT, static_cast<int>(r) * m.rowH + m.iconY);
					const int off = (PL_ICON_LARGE - m.icon) / 2;
					origins.push_back(toWindow(icon) - QPoint(off, off));
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
	int range = sb_thumb_span(&bar);
	if (range <= 0) {
		return;
	}
	const int along = m_sbVertical ? static_cast<int>(e->position().y()) - (contentTop() - 1)
		: static_cast<int>(e->position().x()) + 1;
	int thumb = std::clamp(along - sb_track_start(&bar) - m_thumbGrab, 0, range);
	if (m_sbVertical) {
		scrollTo(m_scrollX, thumb * scrollMaxY() / range);
	} else {
		scrollTo(thumb * scrollMaxX() / range, m_scrollY);
	}
}

void FolderWindow::mouseReleaseEvent(QMouseEvent *) {
	if (m_grabPending) {
		m_grabPending = false;
		if (m_grabbing) {
			m_grabbing = false;
			unsetCursor();
		} else {
			contextClick(m_grabPress);
		}
		return;
	}
	if (m_headerDrag != HeaderDrag::None) {
		const HeaderDrag drag = m_headerDrag;
		m_headerDrag = HeaderDrag::None;
		if (drag == HeaderDrag::Press) {
			sortBy(m_headerColumn);
		} else if (drag == HeaderDrag::Move) {
			moveColumn(m_headerColumn, columnX(m_headerColumn) + m_headerPointerX - m_headerPressX);
		} else {
			saveStateSoon();
			Finder::instance().notifyState();
		}
		update();
		return;
	}
	if (Item *b = m_buttonDown) {
		const bool open = m_buttonInside;
		m_buttonDown = nullptr;
		m_buttonInside = false;
		update();
		if (open) {
			forEachItem([&](Item *item) { item->selected = item == b; });
			Finder::instance().notifyState();
			Finder::instance().openSelection();
		}
		return;
	}
	m_pressItem = nullptr;
	if (m_marquee) {
		endMarquee();
	}
	m_sbPart = SB_NONE;
	m_repeat.stop();
}

void FolderWindow::mouseDoubleClickEvent(QMouseEvent *e) {
	m_renameTimer.stop();
	bool onTriangle = false;
	if (Item *hit = itemAt(e->position().toPoint(), &onTriangle);
			hit && !onTriangle && listSelectableAt(e->position().toPoint(), hit)) {
		forEachItem([&](Item *item) { item->selected = item == hit; });
		update();
		Finder::instance().notifyState();
		if ((e->modifiers() & Qt::AltModifier) &&
				Finder::instance().closeApplicationWindow(hit)) {
			return;
		}
		Finder::instance().openItem(hit);
		/* Option-double-click: the window it was in closes behind it. */
		if (e->modifiers() & Qt::AltModifier) {
			close();
		}
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
	if (e->key() == Qt::Key_Tab || e->key() == Qt::Key_Backtab) {
		std::vector<Item *> items = visibleItems();
		std::sort(items.begin(), items.end(), [](const Item *a, const Item *b) {
			const int order = a->visibleName().compare(b->visibleName(), Qt::CaseInsensitive);
			return order == 0 ? a->visibleName() < b->visibleName() : order < 0;
		});
		if (!items.empty()) {
			const std::vector<Item *> selected = selectedItems();
			auto current = selected.empty() ? items.end()
				: std::find(items.begin(), items.end(), selected.front());
			const bool backwards = e->key() == Qt::Key_Backtab ||
				(e->modifiers() & Qt::ShiftModifier);
			int index = current == items.end() ? (backwards ? 0 : -1)
				: static_cast<int>(std::distance(items.begin(), current));
			index = (index + (backwards ? static_cast<int>(items.size()) - 1 : 1)) %
				static_cast<int>(items.size());
			forEachItem([](Item *item) { item->selected = false; });
			items[index]->selected = true;
			update();
			Finder::instance().notifyState();
		}
		e->accept();
		return;
	}
	if (!(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
			!e->text().isEmpty() && e->text().at(0).isPrint()) {
		std::vector<Item *> items = visibleItems();
		std::sort(items.begin(), items.end(), [](const Item *a, const Item *b) {
			const int order = a->visibleName().compare(b->visibleName(), Qt::CaseInsensitive);
			return order == 0 ? a->visibleName() < b->visibleName() : order < 0;
		});
		const QString typed = e->text();
		const QString repeated = m_typeSelect + typed;
		const QString prefix = !m_typeSelect.isEmpty() &&
				m_typeSelect.compare(typed, Qt::CaseInsensitive) == 0 ? typed : repeated;
		auto match = std::find_if(items.begin(), items.end(), [&](const Item *item) {
			return item->visibleName().startsWith(prefix, Qt::CaseInsensitive);
		});
		if (match == items.end() && prefix != typed) {
			match = std::find_if(items.begin(), items.end(), [&](const Item *item) {
				return item->visibleName().startsWith(typed, Qt::CaseInsensitive);
			});
			m_typeSelect = typed;
		} else {
			m_typeSelect = prefix;
		}
		m_typeSelectTimer.start();
		if (match != items.end()) {
			forEachItem([](Item *item) { item->selected = false; });
			(*match)->selected = true;
			update();
			Finder::instance().notifyState();
			e->accept();
			return;
		}
		m_typeSelect.clear();
	}
	if (!(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
			(e->key() == Qt::Key_Up || e->key() == Qt::Key_Down ||
			e->key() == Qt::Key_Left || e->key() == Qt::Key_Right)) {
		std::vector<Item *> items = visibleItems();
		if (items.empty()) {
			e->accept();
			return;
		}
		const std::vector<Item *> selected = selectedItems();
		auto current = selected.empty() ? items.end()
			: std::find(items.begin(), items.end(), selected.front());
		Item *next = nullptr;
		if (current == items.end()) {
			next = *std::min_element(items.begin(), items.end(), [](const Item *a, const Item *b) {
				return a->pos.y() == b->pos.y() ? a->pos.x() < b->pos.x() : a->pos.y() < b->pos.y();
			});
		} else if (m_mode == ViewMode::List) {
			if (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) {
				const int index = static_cast<int>(std::distance(items.begin(), current));
				const int direction = e->key() == Qt::Key_Up ? -1 : 1;
				const int target = std::clamp(index + direction, 0, static_cast<int>(items.size()) - 1);
				next = items[target];
			}
		} else {
			const QPoint origin = (*current)->pos;
			int bestScore = std::numeric_limits<int>::max();
			for (Item *candidate : items) {
				if (candidate == *current) {
					continue;
				}
				const QPoint delta = candidate->pos - origin;
				const int primary = e->key() == Qt::Key_Right ? delta.x()
					: e->key() == Qt::Key_Left ? -delta.x()
					: e->key() == Qt::Key_Down ? delta.y() : -delta.y();
				if (primary <= 0) {
					continue;
				}
				const int secondary = e->key() == Qt::Key_Left || e->key() == Qt::Key_Right
					? std::abs(delta.y()) : std::abs(delta.x());
				const int score = primary * 2 + secondary;
				if (score < bestScore) {
					bestScore = score;
					next = candidate;
				}
			}
		}
		if (next) {
			if (!(e->modifiers() & Qt::ShiftModifier)) {
				forEachItem([](Item *item) { item->selected = false; });
			}
			next->selected = true;
			update();
			Finder::instance().notifyState();
		}
		e->accept();
		return;
	}
	if (m_mode == ViewMode::List && (e->modifiers() & Qt::ControlModifier) &&
			(e->key() == Qt::Key_Right || e->key() == Qt::Key_Left)) {
		const std::vector<Item *> selected = selectedItems();
		if (selected.size() == 1 && selected.front()->isDir) {
			const QString path = selected.front()->path;
			const bool expanded = m_expanded.contains(path);
			if ((e->key() == Qt::Key_Right && !expanded) ||
					(e->key() == Qt::Key_Left && expanded)) {
				toggleExpanded(selected.front());
			}
			e->accept();
			return;
		}
	}
	if (e->key() == Qt::Key_Space && Finder::instance().springOpenNow()) {
		e->accept();
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
	if (e->key() == Qt::Key_Q && (e->modifiers() & Qt::ShiftModifier)) {
		f.logOut(); /* ⇧⌘Q */
		return true;
	}
	if (e->key() == Qt::Key_Up) {
		f.openEnclosing(e->modifiers() & Qt::AltModifier); /* ⌘-↑, Option-⌘-↑ */
		return true;
	}
	if (e->key() == Qt::Key_Question || (e->key() == Qt::Key_Slash && (e->modifiers() & Qt::ShiftModifier))) {
		platinumStartApplication(siblingProgram("zacos9-help"), { "zacos9" }); /* ⌘-?: ZacOS Help */
		return true;
	}
	switch (e->key()) {
	case Qt::Key_N: f.newFolder(); return true;
	case Qt::Key_A: f.command("select-all"); return true;
	case Qt::Key_C: f.copySelectionNames(); return true;
	case Qt::Key_O: f.openSelection(); return true;
	case Qt::Key_W:
		if (e->modifiers() & Qt::AltModifier) {
			f.closeAllWindows();
		} else {
			f.closeWindow();
		}
		return true;
	case Qt::Key_Backspace: f.moveSelectionToTrash(); return true;
	case Qt::Key_I: f.getInfo(); return true;
	case Qt::Key_D: f.duplicate(); return true;
	case Qt::Key_M: f.makeAlias(); return true;
	case Qt::Key_Y: f.putAway(); return true;
	case Qt::Key_E: f.ejectSelection(); return true;
	case Qt::Key_R: f.showOriginal(); return true;
	case Qt::Key_F: f.command("find"); return true;
	case Qt::Key_P: f.printSelection(); return true;
	default: return false;
	}
}

/* ---- drag and drop --------------------------------------------------------------- */

void FolderWindow::dragEnterEvent(QDragEnterEvent *e) {
	/* Icons that can only move about their own window are taken back
	 * there and nowhere else. */
	const QMimeData *mime = e->mimeData();
	if (((mime->hasFormat(APPLICATION_ITEMS_MIME) || mime->hasFormat(STARTUP_ITEMS_MIME)) &&
			QDir(m_path) == QDir(trashFilesPath())) ||
			/* Applications dragged to Startup Items, or to its folder. */
			(e->source() != this && acceptsStartupDrops() &&
				(mime->hasFormat(APPLICATION_ITEMS_MIME) || mime->hasFormat(ALIAS_ITEMS_MIME))) ||
			e->mimeData()->hasUrls() ||
			(e->source() == nullptr && canMakeClipping(mime) && !vfsIsVirtual(m_path)) ||
			(e->mimeData()->hasFormat(ALIAS_ITEMS_MIME) &&
			!vfsIsVirtual(m_path)) ||
			(e->mimeData()->hasFormat(ICON_MOVE_MIME) && e->source() == this)) {
		e->acceptProposedAction();
	}
}

/* Startup Items itself, or a window showing its folder. */
bool FolderWindow::acceptsStartupDrops() {
	if (vfsIsStartupItems(m_path)) {
		return true;
	}
	bool found = false;
	forEachItem([&](Item *item) { found = found || vfsIsStartupItems(item->path); });
	return found;
}

void FolderWindow::dragMoveEvent(QDragMoveEvent *e) {
	if (e->mimeData()->hasFormat(ICON_MOVE_MIME) &&
			(e->source() == this || !e->mimeData()->hasFormat(ALIAS_ITEMS_MIME))) {
		e->acceptProposedAction(); /* nothing to drop them into */
		return;
	}
	Item *target = dropTargetAt(e->position().toPoint(), draggedPaths(e->mimeData()));
	if (target && vfsIsVirtual(m_path) && e->mimeData()->hasFormat(ICON_MOVE_MIME) &&
			!vfsIsStartupItems(target->path)) {
		target = nullptr; /* only Startup Items takes these here */
	}
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
	const bool iconMove = e->mimeData()->hasFormat(ICON_MOVE_MIME);
	Item *target = iconMove ? nullptr
		: dropTargetAt(e->position().toPoint(), draggedPaths(e->mimeData()));
	clearDropTarget();
	if (e->source() != this && !e->mimeData()->hasFormat(STARTUP_ITEMS_MIME)) {
		Item *on = dropTargetAt(e->position().toPoint(), {});
		if (vfsIsStartupItems(on ? on->path : m_path)) {
			dropItems(e, on, m_path);
			Finder::instance().dragEnded();
			return;
		}
	}
	if (iconMove && e->source() != this && e->mimeData()->hasFormat(ALIAS_ITEMS_MIME) &&
			!vfsIsVirtual(m_path)) {
		dropItems(e, dropTargetAt(e->position().toPoint(), {}), m_path);
		Finder::instance().dragEnded();
		return;
	}
	if (iconMove && (e->source() != this || m_mode != ViewMode::Icons)) {
		/* Back in its own list or button view: nothing moves, but it
		 * isn't a drop elsewhere either. */
		e->setDropAction(e->source() == this ? Qt::MoveAction : Qt::IgnoreAction);
		e->setAccepted(e->source() == this);
		return;
	}
	if (!target && e->source() == this && m_mode == ViewMode::Icons &&
			!(e->modifiers() & Qt::AltModifier)) {
		/* Icons dragged within their own window: they move there, and stay
		 * (on the grid, if it snaps; kept arranged, they go back in line). */
		const IconArrangement arrangement = m_state.options.icons.arrangement;
		const QPoint delta = e->position().toPoint() - m_dragStart;
		const std::vector<Item *> moved = selectedItems();
		for (Item *item : moved) {
			if (arrangement == IconArrangement::KeepArranged) {
				break;
			}
			QPoint p = item->pos + delta;
			p.setX(std::max(0, p.x()));
			p.setY(std::max(0, p.y()));
			item->pos = p;
			m_state.icons.insert(item->key(), p);
		}
		/* ⌘-dragged (Ctrl, as zacos9-wm hands ⌘ on), they snap too. */
		if (arrangement == IconArrangement::SnapToGrid || (e->modifiers() & Qt::ControlModifier)) {
			snapToGrid(moved);
		}
		saveStateSoon();
		relayout();
		update();
		e->setDropAction(Qt::MoveAction);
		e->accept();
		return;
	}
	if (isClippingDrop(e)) {
		/* A clipping lands where it was dropped (in icon view). */
		QPoint p = toContent(e->position().toPoint()) - QPoint(PL_ICON_LARGE / 2, PL_ICON_LARGE / 2);
		dropClippings(e, target, m_path, [this, &p](const QString &into, const QStringList &made) {
			if (QDir(into) != QDir(m_path) || m_mode != ViewMode::Icons) {
				return;
			}
			for (const QString &path : made) {
				m_state.icons.insert(QFileInfo(path).fileName(), p);
				p += QPoint(0, PL_ICON_LARGE + 24);
			}
			saveStateSoon();
		});
		return;
	}
	dropItems(e, target, m_path);
	Finder::instance().dragEnded();
}
