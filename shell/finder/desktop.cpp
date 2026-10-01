#include "desktop.h"

#include <LayerShellQt/window.h>
#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSet>
#include <QStandardPaths>
#include <QWindow>

#include "fileops.h"
#include "folderwindow.h"
#include "menudraw.h"

/* Desktop icon placement (not in the HIG; TODO: measure). */
static constexpr int ICON_MARGIN_RIGHT = 24;
static constexpr int ICON_MARGIN_TOP = 14;    /* below the menu bar */
static constexpr int ICON_MARGIN_BOTTOM = 32;
static constexpr int CELL_W = 80, CELL_H = 64;

/* An original 8x8 desktop pattern: blue-violet with a sparse darker
 * weave. (Mac OS 8's own patterns are Apple's; this one is ours.) */
static const char *const pattern[8] = {
	"d.......",
	"........",
	"....d...",
	"........",
	"..d.....",
	"........",
	"......d.",
	"........",
};
static constexpr uint32_t PATTERN_BASE = RGB(0x66, 0x66, 0xCC);
static constexpr uint32_t PATTERN_DOT = RGB(0x55, 0x55, 0xAA);

Desktop::Desktop() {
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAcceptDrops(true);
	m_renameTimer.setSingleShot(true);
	m_renameTimer.callOnTimeout([this] { beginRename(m_renameItem); });
	m_disk = std::make_unique<Item>();
	m_disk->name = displayName("/");
	m_disk->path = "/";
	m_disk->kind = PL_ICON_DISK;

	m_trash = std::make_unique<Item>();
	m_trash->name = "Trash";
	m_trash->path = trashFilesPath();
	updateTrashIcon();

	QDir().mkpath(folderPath());
	m_watcher.addPath(folderPath());
	QDir().mkpath(trashFilesPath());
	m_watcher.addPath(trashFilesPath());
	QObject::connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
		[this](const QString &dir) { Finder::instance().folderChanged(dir); });
	reload();
}

QString Desktop::folderPath() const {
	return QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
}

void Desktop::becomeLayerSurface() {
	winId(); /* create the native window */
	if (auto *lw = LayerShellQt::Window::get(windowHandle())) {
		lw->setLayer(LayerShellQt::Window::LayerBottom);
		lw->setAnchors(LayerShellQt::Window::Anchors(
			LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom |
			LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
		lw->setExclusiveZone(-1); /* full screen, under the menu bar too */
		lw->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityOnDemand);
		lw->setScope("platinum-desktop");
	}
}

void Desktop::updateTrashIcon() {
	QDir trash(trashFilesPath());
	m_trash->kind = trash.exists() && !trash.isEmpty() ? PL_ICON_TRASH_FULL
		: PL_ICON_TRASH_EMPTY;
}

void Desktop::folderChanged(const QString &folder) {
	if (QDir(folder) == QDir(trashFilesPath())) {
		updateTrashIcon();
		update();
	}
	if (QDir(folder) == QDir(folderPath())) {
		reload();
	}
}

void Desktop::reload() {
	QSet<QString> selected;
	for (auto &item : m_files) {
		if (item->selected) {
			selected.insert(item->name);
		}
	}
	m_pressItem = nullptr;
	auto fresh = listFolder(folderPath());
	m_editor.retarget(fresh);
	m_renameTimer.stop();
	m_renameItem = nullptr;
	m_files = std::move(fresh);
	for (auto &item : m_files) {
		item->selected = selected.contains(item->name);
	}
	placeIcons();
	update();
}

/* The disk first at top right, Desktop items in columns below it (right
 * to left), the Trash at bottom right. */
void Desktop::placeIcons() {
	const int right = width() - ICON_MARGIN_RIGHT - PL_ICON_LARGE;
	const int top = MBAR_HEIGHT + ICON_MARGIN_TOP;
	m_disk->pos = QPoint(right, top);
	m_trash->pos = QPoint(right, height() - ICON_MARGIN_BOTTOM - PL_ICON_LARGE - 16);
	const int rows = std::max(1, (m_trash->pos.y() - top) / CELL_H - 1);
	int i = 1; /* slot 0 is the disk */
	for (auto &item : m_files) {
		int col = i / rows, row = i % rows;
		item->pos = QPoint(right - col * CELL_W, top + row * CELL_H);
		i++;
	}
}

void Desktop::resizeEvent(QResizeEvent *) {
	placeIcons();
}

void Desktop::paintEvent(QPaintEvent *) {
	Pixels px(width(), height());
	pl_canvas *c = &px.c;
	for (int y = 0; y < height(); y++) {
		const char *row = pattern[y % 8];
		for (int x = 0; x < width(); x++) {
			pl_put(c, x, y, row[x % 8] == 'd' ? PATTERN_DOT : PATTERN_BASE);
		}
	}
	paintIconItem(c, *m_disk, m_disk->pos.x(), m_disk->pos.y(), true);
	for (auto &item : m_files) {
		const bool editing = m_editor.editing(item.get());
		paintIconItem(c, *item, item->pos.x(), item->pos.y(), true, !editing);
		if (editing) {
			m_editor.paint(c, item->pos.x(), item->pos.y());
		}
	}
	paintIconItem(c, *m_trash, m_trash->pos.x(), m_trash->pos.y(), true);
	QPainter p(this);
	px.blit(p);
}

Item *Desktop::itemAt(QPoint pos) {
	for (Item *item : { m_trash.get(), m_disk.get() }) {
		if (iconItemContains(*item, item->pos.x(), item->pos.y(), pos)) {
			return item;
		}
	}
	for (auto it = m_files.rbegin(); it != m_files.rend(); ++it) {
		if (iconItemContains(**it, (*it)->pos.x(), (*it)->pos.y(), pos)) {
			return it->get();
		}
	}
	return nullptr;
}

std::vector<Item *> Desktop::selectedItems() {
	std::vector<Item *> out;
	for (Item *item : { m_disk.get(), m_trash.get() }) {
		if (item->selected) {
			out.push_back(item);
		}
	}
	for (auto &item : m_files) {
		if (item->selected) {
			out.push_back(item.get());
		}
	}
	return out;
}

void Desktop::selectByName(const QString &name) {
	m_disk->selected = m_trash->selected = false;
	for (auto &item : m_files) {
		item->selected = item->name == name;
	}
	update();
}

std::vector<Item *> Desktop::allItems() {
	std::vector<Item *> all = { m_disk.get(), m_trash.get() };
	for (auto &item : m_files) {
		all.push_back(item.get());
	}
	return all;
}

bool Desktop::renamable(const Item *item) const {
	return item && item != m_disk.get() && item != m_trash.get();
}

void Desktop::beginRename(Item *item) {
	if (renamable(item) && item->selected) {
		m_editor.begin(item);
	}
}

void Desktop::mousePressEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (Item *edited = m_editor.item()) {
		if (m_editor.contains(edited->pos.x(), edited->pos.y(), pos)) {
			return;
		}
		m_editor.commit();
	}
	Item *hit = itemAt(pos);
	const bool extend = e->modifiers() & Qt::ShiftModifier;
	m_renameTimer.stop();
	if (renamable(hit) && hit->selected && !extend && selectedItems().size() == 1 &&
			iconLabelContains(*hit, hit->pos.x(), hit->pos.y(), pos)) {
		m_renameItem = hit;
		m_renameTimer.start(QApplication::doubleClickInterval());
	}
	for (Item *item : allItems()) {
		if (extend) {
			if (item == hit) {
				item->selected = !item->selected;
			}
		} else if (!hit || !hit->selected) {
			/* Pressing a selected icon keeps the selection, to drag them all. */
			item->selected = item == hit;
		}
	}
	m_pressItem = hit && hit->selected ? hit : nullptr;
	m_pressPos = e->position().toPoint();
	update();
	/* Clicking the desktop brings the Finder forward. */
	Finder::instance().setFront(this);
	Finder::instance().notifyState();
}

void Desktop::mouseDoubleClickEvent(QMouseEvent *e) {
	m_renameTimer.stop();
	if (itemAt(e->position().toPoint())) {
		Finder::instance().openSelection();
	}
}

void Desktop::mouseMoveEvent(QMouseEvent *e) {
	if (!m_pressItem || !(e->buttons() & Qt::LeftButton) ||
			(e->position().toPoint() - m_pressPos).manhattanLength() <
				QApplication::startDragDistance()) {
		return;
	}
	std::vector<Item *> items;
	std::vector<QPoint> origins;
	for (Item *item : selectedItems()) {
		/* The disk and the Trash stay put. TODO: dragging the disk to the
		 * Trash ejects it on a Mac. */
		if (item != m_disk.get() && item != m_trash.get()) {
			items.push_back(item);
			origins.push_back(item->pos);
		}
	}
	m_pressItem = nullptr;
	m_renameTimer.stop();
	startItemDrag(this, items, origins, m_pressPos);
}

void Desktop::mouseReleaseEvent(QMouseEvent *) {
	m_pressItem = nullptr;
}

Item *Desktop::dropTargetAt(QPoint pos, const QStringList &dragged) {
	Item *item = itemAt(pos);
	return item && acceptsDrops(*item) && !dragged.contains(item->path) ? item : nullptr;
}

void Desktop::clearDropTarget() {
	for (Item *item : allItems()) {
		item->dropTarget = false;
	}
	update();
}

void Desktop::dragEnterEvent(QDragEnterEvent *e) {
	if (e->mimeData()->hasUrls()) {
		e->acceptProposedAction();
	}
}

void Desktop::dragMoveEvent(QDragMoveEvent *e) {
	Item *target = dropTargetAt(e->position().toPoint(), draggedPaths(e->mimeData()));
	for (Item *item : allItems()) {
		item->dropTarget = item == target;
	}
	update();
	e->acceptProposedAction();
}

void Desktop::dragLeaveEvent(QDragLeaveEvent *) {
	clearDropTarget();
}

void Desktop::dropEvent(QDropEvent *e) {
	Item *target = dropTargetAt(e->position().toPoint(), draggedPaths(e->mimeData()));
	clearDropTarget();
	dropItems(e, target, folderPath());
}

void Desktop::keyPressEvent(QKeyEvent *e) {
	if (m_editor.key(e)) {
		return;
	}
	if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) &&
			selectedItems().size() == 1) {
		beginRename(selectedItems().front());
		return;
	}
	if (!finderShortcut(e)) {
		QWidget::keyPressEvent(e);
	}
}
