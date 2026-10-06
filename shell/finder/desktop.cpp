#include "desktop.h"
#include "customthemes.h"

#include "vfs.h"

#include <LayerShellQt/window.h>
#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QWindow>

#include <QHash>

#include "fileops.h"
#include "folderwindow.h"
#include "menudraw.h"
#include "localvolumes.h"
#include "netvolumes.h"
#include "patterns.h"
#include "custompatterns.h"
#include "settings.h"

/* How often the desktop checks what's mounted from the Network Browser:
 * nothing posts an event when a terminal runs fusermount3 -u or gio
 * mount -u by hand, so this is a poll, the same trade FileSharingPanel
 * already makes for who's connected (there, every 5 s). */
static constexpr int NET_VOLUMES_POLL_MS = 3000;

/* Desktop icon placement (not in the HIG; TODO: measure). */
static constexpr int ICON_MARGIN_RIGHT = 24;
static constexpr int ICON_MARGIN_TOP = 14;    /* below the menu bar */
static constexpr int ICON_MARGIN_BOTTOM = 32;
static constexpr int CELL_W = 80, CELL_H = 64;

/* The desktop pattern is chosen by id in ~/.config/zacos9/desktop.conf
 * ("pattern=ocean-ripple"); the Appearance control panel will write it.
 * Changes apply at once. */
static QString settingsDir() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/zacos9";
}

void Desktop::loadPattern() {
	QSettings settings(settingsDir() + "/desktop.conf", QSettings::IniFormat);
	const QByteArray id = settings.value("pattern").toString().toUtf8();
	const int pattern = desktopPatternFind(QString::fromUtf8(id));
	if (pattern != m_pattern) {
		m_pattern = pattern;
	}
	update();
}

Desktop::Desktop() {
	watchDesktopPatterns(this, [this] { loadPattern(); update(); });
	watchDesktopWallpaper(this, [this] { update(); });
	watchCustomThemes(this, [this] { update(); });
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAcceptDrops(true);
	m_renameTimer.setSingleShot(true);
	m_renameTimer.callOnTimeout([this] { beginRename(m_renameItem); });
	m_state = FolderState::load(folderPath());
	/* The startup disk shows the Macintosh view of this computer: a
	 * System Folder, Applications and Documents. Debian's own hierarchy
	 * is untouched underneath, and appears as a second disk when the
	 * registry asks for it (see vfs.h). */
	m_disk = std::make_unique<Item>();
	m_disk->stateKey = "::disk";
	m_disk->name = vfsVolumeName();
	m_disk->path = vfsRoot();
	m_disk->kind = PL_ICON_DISK;
	m_disk->isVirtual = true;
	m_disk->isDir = true;
	buildUnixDisk();
	m_vfsToken = vfsOnChange([this] {
		m_disk->name = vfsVolumeName();
		m_disk->resetLabels();
		buildUnixDisk();
		placeIcons();
		update();
	});

	m_trash = std::make_unique<Item>();
	m_trash->stateKey = "::trash";
	m_trash->name = "Trash";
	m_trash->path = trashFilesPath();
	updateTrashIcon();

	QDir().mkpath(settingsDir());
	m_settingsWatcher.addPath(settingsDir());
	QObject::connect(&m_settingsWatcher, &QFileSystemWatcher::directoryChanged, [this] {
		loadPattern();
		QApplication::setDoubleClickInterval(pl_double_click_ms());
		/* Accent colour and highlight are read at paint time. */
		for (QWidget *w : QApplication::topLevelWidgets()) {
			w->update();
		}
	});
	loadPattern();

	QDir().mkpath(folderPath());
	m_watcher.addPath(folderPath());
	QDir().mkpath(trashFilesPath());
	m_watcher.addPath(trashFilesPath());
	QObject::connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
		[this](const QString &dir) { Finder::instance().folderChanged(dir); });
	reload();

	refreshNetVolumes();
	m_netVolumesTimer.callOnTimeout([this] { refreshNetVolumes(); });
	m_netVolumesTimer.start(NET_VOLUMES_POLL_MS);

	refreshLocalVolumes();
	localVolumesOnChange([this] { refreshLocalVolumes(); });
	localVolumesMountAll();
}

Desktop::~Desktop() {
	if (m_vfsToken) {
		vfsOffChange(m_vfsToken);
	}
}

/* Debian's filesystem as a disk of its own, for looking at what the
 * Macintosh view leaves out. Off unless the registry turns it on. */
void Desktop::buildUnixDisk() {
	const bool want = vfsUnixVolumeShown();
	if (want == (m_unix != nullptr)) {
		return;
	}
	if (!want) {
		m_unix.reset();
		return;
	}
	m_unix = std::make_unique<Item>();
	m_unix->stateKey = "::unix";
	m_unix->name = displayName("/");
	m_unix->path = "/";
	m_unix->kind = PL_ICON_DISK;
	m_unix->isDir = true;
}

/* What's mounted from the Network Browser, as desktop disk icons. Skips
 * rebuilding (and so repainting, and so flickering a selection) when
 * nothing actually changed since the last poll. */
void Desktop::refreshNetVolumes() {
	const std::vector<NetVolume> found = netVolumes();
	if (found.size() == m_netVolumes.size()) {
		bool same = true;
		for (size_t i = 0; i < found.size() && same; i++) {
			same = m_netVolumes[i]->path == found[i].path;
		}
		if (same) {
			return;
		}
	}
	QSet<QString> selected;
	for (auto &item : m_netVolumes) {
		if (item->selected) {
			selected.insert(item->path);
		}
	}
	std::vector<std::unique_ptr<Item>> fresh;
	for (const NetVolume &v : found) {
		auto item = std::make_unique<Item>();
		item->stateKey = "::net:" + v.path;
		item->name = v.name;
		item->path = v.path;
		item->kind = PL_ICON_DISK;
		item->isDir = true;
		item->isNetworkVolume = true;
		item->selected = selected.contains(v.path);
		fresh.push_back(std::move(item));
	}
	m_netVolumes = std::move(fresh);
	placeIcons();
	update();
}

void Desktop::refreshLocalVolumes() {
	const std::vector<LocalVolume> found = localVolumes();
	/* Skip rebuild when nothing changed. */
	if (found.size() == m_localVolumes.size()) {
		bool same = true;
		for (size_t i = 0; i < found.size() && same; i++) {
			same = m_localVolumes[i]->path == found[i].path;
		}
		if (same) {
			return;
		}
	}
	QSet<QString> selected;
	for (auto &item : m_localVolumes) {
		if (item->selected) {
			selected.insert(item->path);
		}
	}
	std::vector<std::unique_ptr<Item>> fresh;
	for (const LocalVolume &v : found) {
		auto item = std::make_unique<Item>();
		item->stateKey = "::local:" + v.path;
		item->name = v.name;
		item->path = v.path;
		item->kind = PL_ICON_DISK;
		item->isDir = true;
		item->isLocalVolume = true;
		item->ejectable = v.ejectable;
		item->selected = selected.contains(v.path);
		fresh.push_back(std::move(item));
	}
	m_localVolumes = std::move(fresh);
	placeIcons();
	update();
	Finder::instance().notifyState();
}

std::vector<Item *> Desktop::fixedItems() const {
	std::vector<Item *> out = { m_disk.get() };
	if (m_unix) {
		out.push_back(m_unix.get());
	}
	for (auto &item : m_localVolumes) {
		out.push_back(item.get());
	}
	for (auto &item : m_netVolumes) {
		out.push_back(item.get());
	}
	out.push_back(m_trash.get());
	return out;
}

bool Desktop::isFixed(const Item *item) const {
	if (item == m_disk.get() || item == m_trash.get() || item == m_unix.get()) {
		return true;
	}
	for (auto &v : m_localVolumes) {
		if (v.get() == item) {
			return true;
		}
	}
	for (auto &v : m_netVolumes) {
		if (v.get() == item) {
			return true;
		}
	}
	return false;
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
		lw->setScope("zacos9-desktop");
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
/* Placed icons stay where the user put them. Otherwise: the disk at top
 * right, Desktop items in columns below it (right to left), the Trash at
 * bottom right. */
void Desktop::placeIcons() {
	const int right = width() - ICON_MARGIN_RIGHT - PL_ICON_LARGE;
	const int top = MBAR_HEIGHT + ICON_MARGIN_TOP;
	const QPoint trashHome(right, height() - ICON_MARGIN_BOTTOM - PL_ICON_LARGE - 16);
	const int rows = std::max(1, (trashHome.y() - top) / CELL_H - 1);

	QHash<QString, QPoint> placed = m_state.icons;
	if (!placed.contains(m_trash->key())) {
		placed.insert(m_trash->key(), trashHome);
	}
	std::vector<Item *> items = fixedItems();
	for (auto &item : m_files) {
		items.push_back(item.get());
	}
	::placeIcons(items, placed, [=](int i) {
		return QPoint(right - (i / rows) * CELL_W, top + (i % rows) * CELL_H);
	}, CELL_W, CELL_H);
}

/* Clean Up snaps the disks and files to the nearest free spot; Arrange
 * leaves the disks where they are and lays the desktop's files out in
 * order in the free spots, from the top right. The Trash keeps its
 * corner either way, and the grid (as placeIcons') stops short of it. */
void Desktop::arrange(Arrange how) {
	const int right = width() - ICON_MARGIN_RIGHT - PL_ICON_LARGE;
	const int top = MBAR_HEIGHT + ICON_MARGIN_TOP;
	const int trashY = height() - ICON_MARGIN_BOTTOM - PL_ICON_LARGE - 16;
	const int rows = std::max(1, (trashY - top) / CELL_H - 1);
	const int cols = std::max(1, right / CELL_W + 1);
	const auto slot = [=](int i) {
		return QPoint(right - (i / rows) * CELL_W, top + (i % rows) * CELL_H);
	};
	std::vector<Item *> fixed = fixedItems();
	std::vector<Item *> files;
	for (auto &item : m_files) {
		files.push_back(item.get());
	}
	if (how == Arrange::CleanUp) {
		std::vector<Item *> all;
		for (Item *item : fixed) {
			if (item != m_trash.get()) {
				all.push_back(item);
			}
		}
		all.insert(all.end(), files.begin(), files.end());
		arrangeIcons(all, how, slot, rows * cols);
	} else {
		sortIcons(files, how);
		QHash<QString, QPoint> placed;
		for (Item *item : fixed) {
			placed.insert(item->key(), item->pos);
		}
		std::vector<Item *> all = fixed;
		all.insert(all.end(), files.begin(), files.end());
		::placeIcons(all, placed, slot, CELL_W, CELL_H);
	}
	m_state.icons.clear();
	for (Item *item : fixedItems()) {
		m_state.icons.insert(item->key(), item->pos);
	}
	for (Item *item : files) {
		m_state.icons.insert(item->key(), item->pos);
	}
	m_state.save(folderPath());
	update();
}

void Desktop::itemRenamed(const QString &from, const QString &to) {
	auto it = m_state.icons.find(from);
	if (it != m_state.icons.end()) {
		QPoint pos = *it;
		m_state.icons.erase(it);
		m_state.icons.insert(to, pos);
		m_state.save(folderPath());
	}
}

void Desktop::resizeEvent(QResizeEvent *) {
	placeIcons();
}

void Desktop::paintEvent(QPaintEvent *) {
	Pixels px(width(), height());
	pl_canvas *c = &px.c;
	desktopBackgroundFill(c, width(), height());
	paintIconItem(c, *m_disk, m_disk->pos.x(), m_disk->pos.y(), true);
	if (m_unix) {
		paintIconItem(c, *m_unix, m_unix->pos.x(), m_unix->pos.y(), true);
	}
	for (auto &item : m_localVolumes) {
		paintIconItem(c, *item, item->pos.x(), item->pos.y(), true);
	}
	for (auto &item : m_netVolumes) {
		paintIconItem(c, *item, item->pos.x(), item->pos.y(), true);
	}
	for (auto &item : m_files) {
		const bool editing = m_editor.editing(item.get());
		paintIconItem(c, *item, item->pos.x(), item->pos.y(), true, !editing);
		if (editing) {
			m_editor.paint(c, item->pos.x(), item->pos.y());
		}
	}
	paintIconItem(c, *m_trash, m_trash->pos.x(), m_trash->pos.y(), true);
	/* Selected icons' whole names, over their neighbours. */
	std::vector<Item *> all = fixedItems();
	for (auto &item : m_files) {
		if (!m_editor.editing(item.get())) {
			all.push_back(item.get());
		}
	}
	for (Item *item : all) {
		if (item->selected) {
			paintIconLabel(c, *item, item->pos.x(), item->pos.y(), true);
		}
	}
	if (m_marquee) {
		paintMarquee(c, QRect(m_marqueeStart, m_marqueeEnd).normalized());
	}
	QPainter p(this);
	px.blit(p);
}

Item *Desktop::itemAt(QPoint pos) {
	for (Item *item : fixedItems()) {
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
	for (Item *item : fixedItems()) {
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
	for (Item *item : fixedItems()) {
		item->selected = false;
	}
	for (auto &item : m_files) {
		item->selected = item->name == name;
	}
	update();
}

std::vector<Item *> Desktop::allItems() {
	std::vector<Item *> all = fixedItems();
	for (auto &item : m_files) {
		all.push_back(item.get());
	}
	return all;
}

bool Desktop::renamable(const Item *item) const {
	/* The startup disk takes a name of the user's choosing, as on the Mac. */
	return item && (item == m_disk.get() || !isFixed(item));
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
	if (!hit && e->button() == Qt::LeftButton) {
		m_marquee = true;
		m_marqueeExtend = extend;
		m_marqueeBase.clear();
		for (Item *item : allItems()) {
			if (item->selected) {
				m_marqueeBase.insert(item->path);
			}
		}
		m_marqueeStart = m_marqueeEnd = pos;
	}
	update();
	/* Clicking the desktop brings the Finder forward. */
	Finder::instance().setFront(this);
	Finder::instance().notifyState();
}

void Desktop::mouseDoubleClickEvent(QMouseEvent *e) {
	m_renameTimer.stop();
	if (Item *hit = itemAt(e->position().toPoint())) {
		for (Item *item : allItems()) {
			item->selected = item == hit;
		}
		update();
		Finder::instance().notifyState();
		Finder::instance().openItem(hit);
	}
}

void Desktop::selectAll() {
	m_editor.commit();
	for (Item *item : allItems()) {
		item->selected = true;
	}
	update();
	Finder::instance().notifyState();
}

void Desktop::updateMarquee(QPoint pos) {
	m_marqueeEnd = QPoint(std::clamp(pos.x(), 0, width() - 1), std::clamp(pos.y(), 0, height() - 1));
	const QRect area = QRect(m_marqueeStart, m_marqueeEnd).normalized();
	for (Item *item : allItems()) {
		const bool base = m_marqueeBase.contains(item->path);
		item->selected = iconItemRect(*item, item->pos.x(), item->pos.y()).intersects(area)
			? (!m_marqueeExtend || !base) : base;
	}
	update();
	Finder::instance().notifyState();
}

void Desktop::mouseMoveEvent(QMouseEvent *e) {
	if (m_marquee && (e->buttons() & Qt::LeftButton)) {
		updateMarquee(e->position().toPoint());
		return;
	}
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
		if (!isFixed(item)) {
			items.push_back(item);
			origins.push_back(item->pos);
		}
	}
	m_pressItem = nullptr;
	m_renameTimer.stop();
	m_dragStart = m_pressPos;
	startItemDrag(this, items, origins, m_pressPos);
	Finder::instance().dragEnded();
}

void Desktop::mouseReleaseEvent(QMouseEvent *) {
	m_pressItem = nullptr;
	if (m_marquee) {
		m_marquee = false;
		m_marqueeBase.clear();
		update();
	}
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
	if (e->mimeData()->hasUrls() || e->mimeData()->hasFormat(ALIAS_ITEMS_MIME)) {
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
	/* Folders and the disk spring open (the Trash too, on a Mac). */
	Finder::instance().springHover(target ? target->path : QString());
}

void Desktop::dragLeaveEvent(QDragLeaveEvent *) {
	clearDropTarget();
	Finder::instance().springHover(QString());
}

void Desktop::dropEvent(QDropEvent *e) {
	Item *target = dropTargetAt(e->position().toPoint(), draggedPaths(e->mimeData()));
	clearDropTarget();
	if (!target && e->source() == this && !(e->modifiers() & Qt::AltModifier)) {
		/* Icons dragged about the desktop: they move there, and stay. */
		const QPoint delta = e->position().toPoint() - m_dragStart;
		for (Item *item : selectedItems()) {
			if (isFixed(item)) {
				continue;
			}
			QPoint p = item->pos + delta;
			p.setX(std::clamp(p.x(), 0, width() - PL_ICON_LARGE));
			p.setY(std::clamp(p.y(), MBAR_HEIGHT, height() - PL_ICON_LARGE - 16));
			m_state.icons.insert(item->key(), p);
		}
		m_state.save(folderPath());
		placeIcons();
		update();
		e->setDropAction(Qt::MoveAction);
		e->accept();
		return;
	}
	dropItems(e, target, folderPath());
	Finder::instance().dragEnded();
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
