#include "desktop.h"

#include <LayerShellQt/window.h>
#include <QDir>
#include <QMouseEvent>
#include <QWindow>

#include "folderwindow.h"
#include "menudraw.h"

/* Desktop icon placement (not in the HIG; TODO: measure). */
static constexpr int ICON_MARGIN_RIGHT = 24;
static constexpr int ICON_MARGIN_TOP = 14;    /* below the menu bar */
static constexpr int ICON_MARGIN_BOTTOM = 32;

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
	auto disk = std::make_unique<Item>();
	disk->name = displayName("/");
	disk->path = "/";
	disk->kind = PL_ICON_DISK;
	m_items.push_back(std::move(disk));

	auto trash = std::make_unique<Item>();
	trash->name = "Trash";
	trash->path = trashFilesPath();
	QDir trashDir(trash->path);
	trash->kind = trashDir.exists() && !trashDir.isEmpty() ? PL_ICON_TRASH_FULL
		: PL_ICON_TRASH_EMPTY;
	m_items.push_back(std::move(trash));
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

void Desktop::placeIcons() {
	const int x = width() - ICON_MARGIN_RIGHT - PL_ICON_LARGE;
	m_items[0]->pos = QPoint(x, MBAR_HEIGHT + ICON_MARGIN_TOP);
	m_items[1]->pos = QPoint(x, height() - ICON_MARGIN_BOTTOM - PL_ICON_LARGE - 16);
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
	for (auto &item : m_items) {
		paintIconItem(c, *item, item->pos.x(), item->pos.y(), true);
	}
	QPainter p(this);
	px.blit(p);
}

Item *Desktop::itemAt(QPoint pos) {
	for (auto it = m_items.rbegin(); it != m_items.rend(); ++it) {
		if (iconItemContains(**it, (*it)->pos.x(), (*it)->pos.y(), pos)) {
			return it->get();
		}
	}
	return nullptr;
}

void Desktop::mousePressEvent(QMouseEvent *e) {
	Item *hit = itemAt(e->position().toPoint());
	for (auto &item : m_items) {
		item->selected = item.get() == hit;
	}
	update();
}

void Desktop::mouseDoubleClickEvent(QMouseEvent *e) {
	if (Item *item = itemAt(e->position().toPoint())) {
		QDir().mkpath(item->path);
		FolderWindow::open(item->path);
	}
}
