#include "packwindow.h"

#include <LayerShellQt/window.h>
#include <QApplication>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QStandardPaths>
#include <QUrl>
#include <QWindow>
#include <algorithm>

#include "menudraw.h"
#include "pixels.h"
#include "platinumshell.h"
#include "settings.h"
#include "toplevels.h"

/* A floating window's thin title bar (HIG: utility windows), its small
 * boxes, and the tiles. Not measured from a Mac; TODO: measure. */
static constexpr int TITLE_H = 13, BOX = 9, BORDER = 1, PAD = 3;
static constexpr int NAME_W_MIN = 80, NAME_W_MAX = 180;

/* Where the pointer holds the title bar when the palette is torn off: clear
 * of the close box, in the title bar's middle; and how far below the palette
 * it stays when the palette opens above it instead. */
static constexpr int DROP_X = BORDER + 3 + BOX + 4, DROP_Y = BORDER + TITLE_H / 2;
static constexpr int DROP_GAP = 6;

PackWindow::PackWindow()
	: m_settings(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
		"/zacos9/pack.ini", QSettings::IniFormat) {
	setWindowTitle("Applications");
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAcceptDrops(true);
	m_look.names = m_settings.value("names", true).toBool();
	m_look.nameWidth = m_settings.value("name-width", 0).toInt();
	setMouseTracking(true); /* the edge's resize pointer */
	m_look.large = m_settings.value("large", false).toBool();
	m_look.across = m_settings.value("across", false).toBool();
	m_look.collapsed = m_settings.value("collapsed", false).toBool();
	m_pos = QPoint(m_settings.value("x", 600).toInt(), m_settings.value("y", 60).toInt());
	/* Programs come and go in bursts: lay out once they settle. */
	m_refresh.setSingleShot(true);
	m_refresh.setInterval(30);
	m_refresh.callOnTimeout([this] { refresh(); });
	Toplevels::start([this] { m_refresh.start(); });
	refresh();
	becomeLayerSurface();
	/* An older version could save a place off the screen (a tear-off near
	 * the bottom); come back on it. */
	moveTo(m_pos);
}

/* Above the windows (the Mac's palettes floated), placed by its margins
 * from the screen's top left. */
void PackWindow::becomeLayerSurface() {
	winId();
	if (auto *lw = LayerShellQt::Window::get(windowHandle())) {
		lw->setLayer(LayerShellQt::Window::LayerTop);
		lw->setAnchors(LayerShellQt::Window::Anchors(
			LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft));
		lw->setExclusiveZone(0);
		/* Focus when clicked, so it knows Option, Shift and ⌘ (they come
		 * with the keyboard); a tile's program then takes it. */
		lw->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityOnDemand);
		lw->setScope("zacos9-pack");
		applyMargins();
	}
}

/* The margins are measured from what the menu bar leaves (a layer surface
 * with no exclusive zone of its own is placed in the usable area), not from
 * the screen: m_pos is where the palette is on the screen. */
void PackWindow::applyMargins() {
	if (auto *lw = LayerShellQt::Window::get(windowHandle())) {
		lw->setMargins(QMargins(m_pos.x(), m_pos.y() - MBAR_HEIGHT, 0, 0));
	}
}

void PackWindow::moveTo(QPoint topLeft) {
	/* The whole palette has to stay between the menu bar and the screen's
	 * edges: torn off near either, its own corner would otherwise be out of
	 * reach. */
	const QSize screen = QGuiApplication::primaryScreen() ?
		QGuiApplication::primaryScreen()->size() : QSize(1280, 720);
	const int w = std::max(width(), 40), h = std::max(height(), TITLE_H);
	m_pos = QPoint(std::clamp(topLeft.x(), 0, std::max(0, screen.width() - w)),
		std::clamp(topLeft.y(), MBAR_HEIGHT, std::max(MBAR_HEIGHT, screen.height() - h)));
	applyMargins();
	update(); /* the new place takes effect with the next frame */
	save();
}

/* The tear-off: the pointer let go at (x, y), and takes the title bar with
 * it, as Mac OS 9's did — right of the close box, so a click there does not
 * put the palette away; above the pointer when it would not fit below, as a
 * menu opens. */
void PackWindow::dropAt(QPoint pointer) {
	const QSize screen = QGuiApplication::primaryScreen() ?
		QGuiApplication::primaryScreen()->size() : QSize(1280, 720);
	QPoint topLeft(pointer.x() - DROP_X, pointer.y() - DROP_Y);
	if (topLeft.y() + height() > screen.height()) {
		topLeft.setY(pointer.y() - DROP_GAP - height());
	}
	moveTo(topLeft);
}

void PackWindow::save() {
	m_settings.setValue("names", m_look.names);
	m_settings.setValue("large", m_look.large);
	m_settings.setValue("across", m_look.across);
	m_settings.setValue("collapsed", m_look.collapsed);
	m_settings.setValue("name-width", m_look.nameWidth);
	m_settings.setValue("x", m_pos.x());
	m_settings.setValue("y", m_pos.y());
}

void PackWindow::refresh() {
	m_apps = packApps(Toplevels::windows(), lookUpApp);
	relayout();
	update();
}

/* ---- layout ------------------------------------------------------------------ */

QSize PackWindow::tileSize() const {
	const int icon = iconSize();
	int nameW = 0;
	if (m_look.names) {
		for (const PackApp &app : m_apps) {
			Text t(app.name, NAME_W_MAX, m_look.large ? PL_FONT_SYSTEM : PL_FONT_VIEWS);
			nameW = std::max(nameW, t.inkWidth());
		}
		nameW = std::clamp(nameW + 2 * PAD, NAME_W_MIN, NAME_W_MAX);
		if (m_look.nameWidth > 0) {
			nameW = std::clamp(m_look.nameWidth, 24, 2 * NAME_W_MAX); /* names shorten to fit */
		}
	}
	return QSize(PAD + icon + PAD + nameW, icon + 2 * PAD);
}

void PackWindow::relayout() {
	const QSize tile = tileSize();
	const int n = static_cast<int>(m_apps.size());
	int w = m_look.across ? n * tile.width() : tile.width();
	int h = m_look.collapsed ? 0 : (m_look.across ? tile.height() : n * tile.height());
	/* Room for the title bar's three boxes. */
	w = std::max(w, 3 * (BOX + 4) + 6);
	setFixedSize(w + 2 * BORDER, TITLE_H + h + 2 * BORDER);
	/* Grown past the screen's edge (across, large icons): back on it. */
	const QSize screen = QGuiApplication::primaryScreen() ?
		QGuiApplication::primaryScreen()->size() : QSize(1280, 720);
	if (windowHandle() && (m_pos.x() + width() > screen.width() ||
			m_pos.y() + height() > screen.height())) {
		moveTo(m_pos);
	}
}

QRect PackWindow::tileRect(int i) const {
	const QSize tile = tileSize();
	return m_look.across ? QRect(BORDER + i * tile.width(), BORDER + TITLE_H, tile.width(), tile.height())
		: QRect(BORDER, BORDER + TITLE_H + i * tile.height(), width() - 2 * BORDER, tile.height());
}

int PackWindow::tileAt(QPoint p) const {
	if (m_look.collapsed) {
		return -1;
	}
	for (int i = 0; i < static_cast<int>(m_apps.size()); i++) {
		if (tileRect(i).contains(p)) {
			return i;
		}
	}
	return -1;
}

QRect PackWindow::boxRect(Box box) const {
	const int y = BORDER + (TITLE_H - BOX) / 2;
	switch (box) {
	case Box::Close: return QRect(BORDER + 3, y, BOX, BOX);
	case Box::Zoom: return QRect(width() - BORDER - 3 - 2 * BOX - 3, y, BOX, BOX);
	case Box::Collapse: return QRect(width() - BORDER - 3 - BOX, y, BOX, BOX);
	default: return {};
	}
}

PackWindow::Box PackWindow::boxAt(QPoint p) const {
	for (Box b : { Box::Close, Box::Zoom, Box::Collapse }) {
		if (boxRect(b).adjusted(-1, -1, 1, 1).contains(p)) {
			return b;
		}
	}
	return Box::None;
}

/* ---- painting -------------------------------------------------------------------- */

static void paintBox(pl_canvas *c, QRect r, bool pressed) {
	pl_fill(c, r.left(), r.top(), r.right(), r.bottom(), pressed ? GRAY(0x8) : GRAY(0xD));
	pl_outline(c, r.left(), r.top(), r.right(), r.bottom(), C_BLACK);
	if (!pressed) {
		pl_hline(c, r.left() + 1, r.right() - 1, r.top() + 1, C_WHITE);
		pl_vline(c, r.left() + 1, r.top() + 1, r.bottom() - 1, C_WHITE);
	}
}

void PackWindow::paintEvent(QPaintEvent *) {
	const int W = width(), H = height();
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, GRAY(0xD));
	pl_outline(c, 0, 0, W - 1, H - 1, C_BLACK);

	/* The title bar: stripes, the boxes, "Applications" if it fits. */
	const int top = BORDER, bottom = BORDER + TITLE_H - 1;
	pl_fill(c, BORDER, top, W - 1 - BORDER, bottom, GRAY(0xC));
	const QRect close = boxRect(Box::Close), zoom = boxRect(Box::Zoom);
	for (int y = top + 2; y < bottom - 1; y += 2) {
		pl_hline(c, close.right() + 4, zoom.left() - 4, y, C_WHITE);
		pl_hline(c, close.right() + 5, zoom.left() - 3, y + 1, GRAY(0x7));
	}
	Text title("Applications", 1000, PL_FONT_VIEWS); /* shown only if it fits whole */
	if (title.t && title.t->ink_l >= 0 && title.inkWidth() < zoom.left() - close.right() - 16) {
		const int tx = (close.right() + zoom.left()) / 2 - title.inkWidth() / 2;
		pl_fill(c, tx - 3, top + 1, tx + title.inkWidth() + 2, bottom - 1, GRAY(0xC));
		pl_text(c, title.t, tx, bottom - 2, C_BLACK);
	}
	for (Box b : { Box::Close, Box::Zoom, Box::Collapse }) {
		paintBox(c, boxRect(b), b == m_box && m_boxInside);
	}
	pl_hline(c, BORDER, W - 1 - BORDER, bottom + 1, C_BLACK);

	/* The tiles: the program in front pressed in, as the Mac's. */
	for (int i = 0; !m_look.collapsed && i < static_cast<int>(m_apps.size()); i++) {
		const QRect r = tileRect(i);
		const PackApp &app = m_apps[i];
		const bool in = app.active || i == m_pressedTile || i == m_dropTile;
		pl_fill(c, r.left(), r.top(), r.right(), r.bottom(), in ? GRAY(0xA) : GRAY(0xD));
		if (in) {
			pl_hline(c, r.left(), r.right(), r.top(), GRAY(0x6));
			pl_vline(c, r.left(), r.top(), r.bottom(), GRAY(0x6));
		} else {
			pl_hline(c, r.left(), r.right(), r.top(), C_WHITE);
			pl_vline(c, r.left(), r.top(), r.bottom(), C_WHITE);
			pl_hline(c, r.left(), r.right(), r.bottom(), GRAY(0x9));
			pl_vline(c, r.right(), r.top(), r.bottom(), GRAY(0x9));
		}
		const int icon = iconSize();
		QImage img = appIcon(app, icon);
		if (app.hidden) {
			/* A hidden program's icon is dimmed. */
			QPainter dim(&img);
			dim.setCompositionMode(QPainter::CompositionMode_DestinationIn);
			dim.fillRect(img.rect(), QColor(0, 0, 0, 110));
		}
		pl_image(c, r.left() + PAD, r.top() + PAD, reinterpret_cast<const uint32_t *>(img.constBits()), icon, icon);
		if (m_look.names) {
			Text name(app.name, r.width() - icon - 3 * PAD, m_look.large ? PL_FONT_SYSTEM : PL_FONT_VIEWS);
			pl_text(c, name.t, r.left() + PAD + icon + PAD + 1, r.top() + r.height() / 2 + 4,
				app.hidden ? GRAY(0x6) : C_BLACK);
		}
	}
	QPainter p(this);
	px.blit(p);
}

/* ---- mouse ----------------------------------------------------------------------- */

/* Mac OS 9's Application Switcher: drag the right edge to set how wide
 * the names are (down the side, with names). */
bool PackWindow::onEdge(QPoint p) const {
	return m_look.names && !m_look.across && !m_look.collapsed && p.y() >= BORDER + TITLE_H &&
		p.x() >= width() - BORDER - 3;
}

void PackWindow::mousePressEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	m_pressLocal = pos;
	if (onEdge(pos)) {
		m_sizing = true;
		return;
	}
	/* ⌘-drag anywhere moves it (⌘ arrives as Control). */
	if (e->modifiers() & Qt::ControlModifier) {
		m_moving = true;
		return;
	}
	m_box = boxAt(pos);
	if (m_box != Box::None) {
		m_boxInside = true;
		update();
		return;
	}
	if (pos.y() < BORDER + TITLE_H) {
		m_moving = true;
		return;
	}
	m_pressedTile = tileAt(pos);
	update();
}

void PackWindow::mouseMoveEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (m_sizing) {
		/* The names take what's right of the icon. */
		m_look.nameWidth = std::max(24, pos.x() + BORDER - (BORDER + PAD + iconSize() + PAD) - BORDER);
		relayout();
		update();
		return;
	}
	if (!(e->buttons() & Qt::LeftButton)) {
		setCursor(onEdge(pos) ? Qt::SizeHorCursor : Qt::ArrowCursor);
	}
	if (m_moving) {
		/* The surface moves under the pointer: the press point stays put
		 * in it. */
		moveTo(m_pos + pos - m_pressLocal);
		return;
	}
	if (m_box != Box::None) {
		const bool inside = boxRect(m_box).adjusted(-1, -1, 1, 1).contains(pos);
		if (inside != m_boxInside) {
			m_boxInside = inside;
			update();
		}
		return;
	}
	if (m_pressedTile >= 0 && tileAt(pos) != m_pressedTile) {
		m_pressedTile = -1;
		update();
	}
}

void PackWindow::mouseReleaseEvent(QMouseEvent *e) {
	const Qt::KeyboardModifiers mods = e->modifiers();
	if (m_sizing) {
		m_sizing = false;
		save();
		return;
	}
	if (m_moving) {
		m_moving = false;
		return;
	}
	if (m_box != Box::None) {
		const Box box = m_box;
		const bool inside = m_boxInside;
		m_box = Box::None;
		m_boxInside = false;
		update();
		if (!inside) {
			return;
		}
		if (box == Box::Close) {
			pl_setting_set("pack-open", nullptr);
			qApp->quit();
		} else if (box == Box::Collapse) {
			m_look.collapsed = !m_look.collapsed;
		} else if ((mods & Qt::AltModifier) && (mods & Qt::ShiftModifier)) {
			m_look.across = !m_look.across; /* Shift-Option-zoom: across, or down */
		} else if (mods & Qt::AltModifier) {
			m_look.large = !m_look.large;   /* Option-zoom: large icons */
		} else {
			m_look.names = !m_look.names;   /* zoom: names or icons alone */
		}
		save();
		relayout();
		update();
		return;
	}
	const int tile = m_pressedTile;
	m_pressedTile = -1;
	update();
	if (tile < 0 || tile >= static_cast<int>(m_apps.size()) || tileAt(e->position().toPoint()) != tile) {
		return;
	}
	/* Option-click: the program you leave hides. */
	if (mods & Qt::AltModifier) {
		for (const PackApp &app : m_apps) {
			if (app.active && app.appId != m_apps[tile].appId && app.appId != QLatin1String(SNIFFER_APP_ID)) {
				Toplevels::hide(app.appId);
			}
		}
	}
	platinumActivateApp(m_apps[tile].appId);
}

/* ---- documents dropped on a tile ---------------------------------------------- */

void PackWindow::dragEnterEvent(QDragEnterEvent *e) {
	if (e->mimeData()->hasUrls()) {
		e->acceptProposedAction();
	}
}

void PackWindow::dragMoveEvent(QDragMoveEvent *e) {
	const int tile = tileAt(e->position().toPoint());
	const bool takes = tile >= 0 && !m_apps[tile].desktopFile.isEmpty();
	if ((takes ? tile : -1) != m_dropTile) {
		m_dropTile = takes ? tile : -1;
		update();
	}
	if (takes) {
		e->acceptProposedAction();
	} else {
		e->ignore();
	}
}

void PackWindow::dragLeaveEvent(QDragLeaveEvent *) {
	m_dropTile = -1;
	update();
}

void PackWindow::dropEvent(QDropEvent *e) {
	const int tile = tileAt(e->position().toPoint());
	m_dropTile = -1;
	update();
	if (tile < 0) {
		return;
	}
	QStringList files;
	for (const QUrl &url : e->mimeData()->urls()) {
		if (url.isLocalFile()) {
			files << url.toLocalFile();
		}
	}
	if (openWithApp(m_apps[tile], files)) {
		/* Opened, not moved: the documents stay where they are. */
		e->setDropAction(Qt::LinkAction);
		e->accept();
		platinumActivateApp(m_apps[tile].appId);
	} else {
		pl_beep();
	}
}
