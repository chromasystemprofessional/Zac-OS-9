#include "toybox.h"

#include <QApplication>
#include <QDir>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QStandardPaths>
#include <QWheelEvent>
#include <algorithm>

#include "fileops.h"
#include "finder.h"
#include "panelkit.h"
#include "platinumshell.h"
#include "settings.h"

static constexpr int MARGIN = 8;
static constexpr int TOPIC_H = 22, TOPIC_GAP = 6; /* the topic buttons' row */
static constexpr int ARROW_STEP = 16;
static const QChar BULLET(0x2022);

static QPointer<ToyBox> &instance() {
	static QPointer<ToyBox> toyBox;
	return toyBox;
}

static QString settingsFile() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
		"/zacos9/finder/toybox.ini";
}

ToyBox *ToyBox::open() {
	ToyBox *t = instance();
	if (!t) {
		t = new ToyBox;
		instance() = t;
		t->show();
		if (t->m_settings.contains("x")) {
			platinumSetWindowPosition(t, QPoint(t->m_settings.value("x").toInt(),
				t->m_settings.value("y").toInt()));
		}
		platinumOnWindowPosition(t, [t](QPoint p) {
			t->m_settings.setValue("x", p.x());
			t->m_settings.setValue("y", p.y());
		});
		pl_sound_event("window-open");
	}
	t->show();
	t->raise();
	t->activateWindow();
	return t;
}

ToyBox::ToyBox() : m_settings(settingsFile(), QSettings::IniFormat) {
	setWindowTitle("Toy Box");
	setAttribute(Qt::WA_DeleteOnClose);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAcceptDrops(true);
	setMinimumSize(160, 100);
	resize(m_settings.value("w", 420).toInt(), m_settings.value("h", 200).toInt());
	m_size = static_cast<ButtonSize>(std::clamp(m_settings.value("size", 1).toInt(), 0, 2));
	QDir().mkpath(toyBoxPath());
	QObject::connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { reload(); });
	reload();
	const QString page = m_settings.value("page").toString();
	for (size_t i = 0; i < m_pages.size(); i++) {
		if (m_pages[i].name == page) {
			showPage(static_cast<int>(i));
		}
	}
}

ToyBox::~ToyBox() {
	m_settings.sync();
}

void ToyBox::closeEvent(QCloseEvent *e) {
	pl_sound_event("window-close");
	QWidget::closeEvent(e);
}

/* ---- pages and items ---------------------------------------------------------- */

std::vector<ToyBox::Page> ToyBox::pagesIn(const QString &root) {
	std::vector<Page> topics;
	bool loose = false;
	const auto entries = QDir(root).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System,
		QDir::Name | QDir::IgnoreCase);
	for (const QFileInfo &entry : entries) {
		if (entry.isDir() && !entry.isSymLink() && entry.fileName().startsWith(BULLET)) {
			const QString name = entry.fileName().mid(1).trimmed();
			topics.push_back({ name.isEmpty() ? entry.fileName() : name, entry.absoluteFilePath() });
		} else {
			loose = true;
		}
	}
	std::stable_sort(topics.begin(), topics.end(), [](const Page &a, const Page &b) {
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});
	std::vector<Page> pages;
	if (loose || topics.empty()) {
		pages.push_back({ QStringLiteral("Applications"), root });
	}
	for (const Page &p : topics) {
		if (pages.size() < 8) {
			pages.push_back(p);
		}
	}
	return pages;
}

void ToyBox::addAliases(const std::vector<std::pair<QString, QString>> &targets,
		const QString &folder) {
	QDir().mkpath(folder);
	for (const auto &[name, target] : targets) {
		if (!target.isEmpty()) {
			createAlias(target, folder, name, QString());
		}
	}
}

void ToyBox::reload() {
	const QString current = m_page < static_cast<int>(m_pages.size()) ? m_pages[m_page].name : QString();
	m_pages = pagesIn(toyBoxPath());
	m_page = 0;
	for (size_t i = 0; i < m_pages.size(); i++) {
		if (m_pages[i].name == current) {
			m_page = static_cast<int>(i);
		}
	}
	m_down = m_dragItem = nullptr;
	m_items.clear();
	const QString folder = m_pages[m_page].folder;
	for (auto &item : listFolder(folder)) {
		/* The Applications page is the loose items, not the pages' folders. */
		if (folder == toyBoxPath() && item->isDir && !item->isAlias && item->name.startsWith(BULLET)) {
			continue;
		}
		m_items.push_back(std::move(item));
	}
	if (!m_watcher.directories().isEmpty()) {
		m_watcher.removePaths(m_watcher.directories());
	}
	m_watcher.addPath(toyBoxPath());
	if (folder != toyBoxPath()) {
		m_watcher.addPath(folder);
	}
	layout();
	update();
}

void ToyBox::showPage(int page) {
	if (page < 0 || page >= static_cast<int>(m_pages.size()) || page == m_page) {
		return;
	}
	m_page = page;
	m_settings.setValue("page", m_pages[page].name);
	m_scroll = 0;
	reload();
}

void ToyBox::setButtonSize(ButtonSize size) {
	m_size = size;
	m_settings.setValue("size", static_cast<int>(size));
	layout();
	update();
}

/* ---- layout -------------------------------------------------------------------- */

/* Small buttons fit many in, Medium are the Mac's own, Large have twice-size
 * icons and larger names ("seen from outer space"). */
ToyBox::Metrics ToyBox::metrics() const {
	switch (m_size) {
	case ButtonSize::Small: return { 26, PL_ICON_SMALL, 74, 46, PL_FONT_VIEWS };
	case ButtonSize::Large: return { 76, 2 * PL_ICON_LARGE, 112, 108, PL_FONT_SYSTEM };
	default: return { 48, PL_ICON_LARGE, 84, 78, PL_FONT_VIEWS };
	}
}

int ToyBox::topicsHeight() const {
	return m_pages.size() > 1 ? TOPIC_GAP + TOPIC_H + TOPIC_GAP + 1 : 0;
}

QRect ToyBox::topicRect(int page) const {
	const int n = static_cast<int>(m_pages.size());
	const int w = std::max(60, (width() - SB_WIDTH - 2 * MARGIN - (n - 1) * 4) / std::max(1, n));
	return QRect(MARGIN + page * (w + 4), TOPIC_GAP, w, TOPIC_H);
}

int ToyBox::topicAt(QPoint p) const {
	for (int i = 0; m_pages.size() > 1 && i < static_cast<int>(m_pages.size()); i++) {
		if (topicRect(i).contains(p)) {
			return i;
		}
	}
	return -1;
}

int ToyBox::viewHeight() const {
	return height() - topicsHeight();
}

void ToyBox::layout() {
	const Metrics m = metrics();
	m_columns = std::max(1, (width() - SB_WIDTH - 2 * MARGIN) / m.cellW);
	for (size_t i = 0; i < m_items.size(); i++) {
		const int col = static_cast<int>(i) % m_columns, row = static_cast<int>(i) / m_columns;
		m_items[i]->pos = QPoint(MARGIN + col * m.cellW, MARGIN + row * m.cellH);
	}
	scrollTo(m_scroll);
}

int ToyBox::contentHeight() const {
	const int rows = (static_cast<int>(m_items.size()) + m_columns - 1) / m_columns;
	return 2 * MARGIN + rows * metrics().cellH;
}

void ToyBox::scrollTo(int y) {
	m_scroll = std::clamp(y, 0, std::max(0, contentHeight() - viewHeight()));
	update();
}

QRect ToyBox::buttonRect(const Item *item) const {
	const Metrics m = metrics();
	return QRect(item->pos.x() + (m.cellW - m.side) / 2, item->pos.y() + topicsHeight() - m_scroll,
		m.side, m.side);
}

Item *ToyBox::itemAt(QPoint p) const {
	if (p.y() < topicsHeight() || p.x() >= width() - SB_WIDTH) {
		return nullptr;
	}
	const Metrics m = metrics();
	for (const auto &item : m_items) {
		const QRect b = buttonRect(item.get());
		/* The button, or its name below it. */
		if (b.contains(p) || QRect(item->pos.x(), b.bottom(), m.cellW, m.cellH - m.side).contains(p)) {
			return item.get();
		}
	}
	return nullptr;
}

pl_scrollbar ToyBox::scrollBar() const {
	pl_scrollbar sb{};
	sb.vertical = true;
	sb.length = height() - topicsHeight() + 1;
	const int max = std::max(0, contentHeight() - viewHeight());
	sb.enabled = max > 0;
	sb_smart(&sb, viewHeight(), viewHeight() + max);
	sb.thumb = sb.enabled ? m_scroll * sb_thumb_span(&sb) / max : 0;
	return sb;
}

void ToyBox::resizeEvent(QResizeEvent *) {
	m_settings.setValue("w", width());
	m_settings.setValue("h", height());
	layout();
}

/* ---- painting ------------------------------------------------------------------ */

void ToyBox::paintEvent(QPaintEvent *) {
	const int W = width(), H = height(), top = topicsHeight();
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, C_WHITE);

	/* The topic buttons: one per page, the page shown pressed in. */
	if (top) {
		pl_fill(c, 0, 0, W - 1, top - 2, GRAY(0xD));
		pl_hline(c, 0, W - 1, top - 1, C_BLACK);
		for (int i = 0; i < static_cast<int>(m_pages.size()); i++) {
			const QRect r = topicRect(i);
			const bool in = i == m_page || i == m_dropTopic;
			pl_bevel_button_paint(c, r.x(), r.y(), r.width(), r.height(), in);
			pl_icon_paint(c, r.x() + 4 + in, r.y() + 3 + in, PL_ICON_FOLDER, PL_ICON_SMALL, false);
			Text t(m_pages[i].name, r.width() - 28, PL_FONT_VIEWS);
			pl_text(c, t.t, r.x() + 24 + in, r.y() + 15 + in, C_BLACK);
		}
	}

	pl_canvas content = { c->px + top * c->stride, c->stride, 0, 0, W - SB_WIDTH + 1,
		std::max(0, H - top) };
	const Metrics m = metrics();
	for (const auto &item : m_items) {
		QRect b = buttonRect(item.get());
		b.translate(0, -top);
		if (b.bottom() + m.cellH < 0 || b.top() > H) {
			continue;
		}
		const bool pressed = item.get() == m_down && m_inside;
		pl_bevel_button_paint(&content, b.x(), b.y(), b.width(), b.height(), pressed);
		const int ix = b.x() + (m.side - m.icon) / 2 + pressed, iy = b.y() + (m.side - m.icon) / 2 + pressed;
		if (m.icon > PL_ICON_LARGE) {
			/* Large: the 32-pixel icon doubled, pixel for pixel. */
			Pixels icon(PL_ICON_LARGE, PL_ICON_LARGE);
			paintIcon(&icon.c, *item, 0, 0, PL_ICON_LARGE, false);
			const QImage big = icon.img.scaled(m.icon, m.icon, Qt::IgnoreAspectRatio,
				Qt::FastTransformation).convertToFormat(QImage::Format_ARGB32);
			pl_image(&content, ix, iy, reinterpret_cast<const uint32_t *>(big.constBits()), m.icon, m.icon);
		} else {
			paintIcon(&content, *item, ix, iy, m.icon, false);
		}
		Text name(item->visibleName(), m.cellW - 4, m.font);
		pl_text(&content, name.t, item->pos.x() + (m.cellW - name.inkWidth()) / 2,
			b.bottom() + (m.font == PL_FONT_SYSTEM ? 16 : 13), C_BLACK);
	}
	if (m_items.empty()) {
		for (int i = 0; i < 2; i++) {
			Text t(i == 0 ? "Drag icons here to add them" : "to the Toy Box.", W - 40, PL_FONT_VIEWS);
			pl_text(&content, t.t, (W - SB_WIDTH - t.inkWidth()) / 2, (H - top) / 2 - 4 + 13 * i,
				GRAY(0x6));
		}
	}

	pl_scrollbar sb = scrollBar();
	pl_scrollbar_paint(c, W - SB_WIDTH + 1, top - 1, &sb, pl_accent_current());
	QPainter p(this);
	px.blit(p);
}

/* ---- mouse and keys -------------------------------------------------------------- */

void ToyBox::sizeMenu(QPoint at, Item *item) {
	std::vector<PopupItem> items;
	if (item) {
		items = { PopupItem("Open"), PopupItem("Remove From Toy Box"), PopupItem("", true, true) };
	}
	const int first = static_cast<int>(items.size());
	items.insert(items.end(), { PopupItem("Small Buttons"), PopupItem("Medium Buttons"),
		PopupItem("Large Buttons") });
	const QString path = item ? item->path : QString();
	panelMenuNear(this, items, first + static_cast<int>(m_size), QRect(at, QSize(1, 1)), at.y(),
		PL_FONT_SYSTEM, [this, first, path](int i) {
			if (i >= first) {
				setButtonSize(static_cast<ButtonSize>(i - first));
				return;
			}
			if (i == 0) {
				Finder::instance().openPath(path);
			} else if (i == 1) {
				QFile::moveToTrash(path); /* the alias, not its original */
				reload();
			}
		});
}

void ToyBox::mousePressEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (isContextClick(e)) {
		sizeMenu(pos, itemAt(pos));
		return;
	}
	if (e->button() != Qt::LeftButton) {
		return;
	}
	if (pos.x() >= width() - SB_WIDTH + 1 && pos.y() >= topicsHeight()) {
		pl_scrollbar sb = scrollBar();
		const int along = pos.y() - (topicsHeight() - 1);
		const int view = viewHeight();
		switch (sb_hit(&sb, along)) {
		case SB_DEC_ARROW: scrollTo(m_scroll - ARROW_STEP); break;
		case SB_INC_ARROW: scrollTo(m_scroll + ARROW_STEP); break;
		case SB_DEC_PAGE: scrollTo(m_scroll - view); break;
		case SB_INC_PAGE: scrollTo(m_scroll + view); break;
		case SB_THUMB_PART: m_thumbGrab = along - (sb_track_start(&sb) + sb.thumb); break;
		default: break;
		}
		return;
	}
	const int topic = topicAt(pos);
	if (topic >= 0) {
		showPage(topic);
		return;
	}
	Item *item = itemAt(pos);
	if (!item) {
		return;
	}
	m_pressPos = pos;
	if (e->modifiers() & Qt::AltModifier) {
		/* Option: the button can be dragged out, instead of clicked. */
		m_dragItem = item;
		return;
	}
	m_down = item;
	m_inside = true;
	update();
}

void ToyBox::mouseMoveEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (m_thumbGrab >= 0) {
		pl_scrollbar sb = scrollBar();
		const int range = sb_thumb_span(&sb);
		const int along = pos.y() - (topicsHeight() - 1);
		const int thumb = std::clamp(along - sb_track_start(&sb) - m_thumbGrab, 0, std::max(1, range));
		scrollTo(range > 0 ? thumb * std::max(0, contentHeight() - viewHeight()) / range : 0);
		return;
	}
	if (m_dragItem && (pos - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
		Item *item = m_dragItem;
		m_dragItem = nullptr;
		const QRect b = buttonRect(item);
		const Metrics m = metrics();
		startItemDrag(this, { item }, { b.topLeft() + QPoint((m.side - PL_ICON_LARGE) / 2,
			(m.side - PL_ICON_LARGE) / 2) }, m_pressPos);
		Finder::instance().dragEnded();
		reload();
		return;
	}
	if (m_down) {
		const bool inside = buttonRect(m_down).contains(pos);
		if (inside != m_inside) {
			m_inside = inside;
			update();
		}
	}
}

void ToyBox::mouseReleaseEvent(QMouseEvent *) {
	m_thumbGrab = -1;
	m_dragItem = nullptr;
	Item *item = m_down;
	const bool open = m_inside;
	m_down = nullptr;
	m_inside = false;
	update();
	if (item && open) {
		/* One click opens, as the Launcher's buttons did. */
		Finder::instance().openPath(item->path);
	}
}

void ToyBox::wheelEvent(QWheelEvent *e) {
	scrollTo(m_scroll - e->angleDelta().y() / 120 * ARROW_STEP * 3);
}

void ToyBox::keyPressEvent(QKeyEvent *e) {
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
		close();
		return;
	}
	QWidget::keyPressEvent(e);
}

/* ---- dropping icons on it ------------------------------------------------------------- */

QString ToyBox::dropFolder(QPoint p) const {
	const int topic = topicAt(p);
	return m_pages[topic >= 0 ? topic : m_page].folder;
}

void ToyBox::dragEnterEvent(QDragEnterEvent *e) {
	if (e->source() != this && (e->mimeData()->hasUrls() || e->mimeData()->hasFormat(ALIAS_ITEMS_MIME))) {
		e->acceptProposedAction();
	}
}

void ToyBox::dragMoveEvent(QDragMoveEvent *e) {
	const int topic = topicAt(e->position().toPoint());
	if (topic != m_dropTopic) {
		m_dropTopic = topic;
		update();
	}
	e->acceptProposedAction();
}

void ToyBox::dropEvent(QDropEvent *e) {
	m_dropTopic = -1;
	std::vector<std::pair<QString, QString>> targets;
	const QMimeData *mime = e->mimeData();
	if (mime->hasFormat(ALIAS_ITEMS_MIME)) {
		/* The Macintosh view's applications and folders: what they stand for. */
		for (const QJsonValue &v : QJsonDocument::fromJson(mime->data(ALIAS_ITEMS_MIME)).array()) {
			const QJsonObject o = v.toObject();
			if (QDir::isAbsolutePath(o["target"].toString())) {
				targets.emplace_back(o["name"].toString(), o["target"].toString());
			}
		}
	} else {
		for (const QString &path : draggedPaths(mime)) {
			const QFileInfo info(path);
			if (info.absoluteFilePath().startsWith(toyBoxPath() + "/")) {
				continue; /* already in the Toy Box */
			}
			auto item = makeItem(info);
			targets.emplace_back(item->visibleName(),
				info.isSymLink() ? info.symLinkTarget() : info.absoluteFilePath());
		}
	}
	addAliases(targets, dropFolder(e->position().toPoint()));
	/* The originals stay where they were: only aliases were made. */
	e->setDropAction(Qt::LinkAction);
	e->accept();
	reload();
}
