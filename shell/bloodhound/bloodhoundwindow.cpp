#include "bloodhoundwindow.h"

#include "icons.h"
#include "programs.h"
#include "search.h"
#include "settings.h"
#include "tr.h"

#include <QApplication>
#include <QClipboard>
#include <QDate>
#include <QDir>
#include <QDrag>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLocale>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QThread>
#include <QUrl>
#include <QWheelEvent>
#include <algorithm>

static constexpr int W = 680, H = 460, MARGIN = 12;
static constexpr int CHANNEL_H = 28;
static constexpr int SEARCH_Y = 38;
static constexpr int FIELD_X = MARGIN, FIELD_W = 360;
static constexpr int SCOPE_X = 380;
static constexpr int RESULTS_Y = 72;
static constexpr int HEADER_H = 21, BAND_H = 21, ROW_H = 19;
static constexpr int STATUS_H = 22, SB_W = 15;
static constexpr int NAME_X = 30, NAME_W = 245;
static constexpr int WHERE_X = 245, WHERE_W = 180;
static constexpr int DATE_X = 425, DATE_W = 100;
static constexpr int SIZE_X = 525, SIZE_W = 55;
static constexpr int KIND_X = 580, KIND_W = 85;
static constexpr uint32_t FACE = GRAY(0xD);

static QString formatDate(const QDateTime &t) {
	return t.isValid() ? QLocale().toString(t.date(), QLocale::ShortFormat) : QString();
}

static QString formatSize(qint64 bytes) {
	if (bytes < 1024) {
		return QString::number(bytes) + " B";
	}
	double value = static_cast<double>(bytes);
	static const char *const units[] = { "K", "MB", "GB", "TB" };
	int unit = 0;
	while (value >= 1024 && unit < 3) {
		value /= 1024;
		unit++;
	}
	return QString::number(value, 'f', value < 10 ? 1 : 0) + " " + units[unit];
}

static QString folderName(const QString &folder) {
	const QString name = QDir(folder).dirName();
	return name.isEmpty() ? folder : name;
}

/* A button wide enough for its label in the current language. */
static int buttonWidth(const QString &label, int minimum) {
	return std::max(minimum, Text(label, 400, PL_FONT_SYSTEM).inkWidth() + 24);
}

BloodhoundWindow::BloodhoundWindow(const QString &folder) : m_folder(folder) {
	setWindowTitle(tr9("Bloodhound"));
	setFixedSize(W, H);
	setFocusPolicy(Qt::StrongFocus);
	setAttribute(Qt::WA_DeleteOnClose);

	m_search.rect = QRect(FIELD_X, SEARCH_Y, FIELD_W, PL_EDIT_H);
	m_search.hint = tr9("Search for");
	m_search.edited = [this] {
		m_problem.clear();
		update();
	};

	const int stopW = buttonWidth(tr9("Stop"), 44);
	const int findW = buttonWidth(tr9("Search"), 62);
	const int stopX = W - MARGIN - stopW;
	const int findX = stopX - 6 - findW;
	m_scope.rect = QRect(SCOPE_X, SEARCH_Y, std::max(80, findX - 6 - SCOPE_X), PL_POPUP_H);
	m_scope.hint = tr9("Where to search.");
	m_scope.items = { PopupItem(tr9("on all disks")), PopupItem(tr9("in my Home folder")) };
	if (!m_folder.isEmpty() && QDir(m_folder) != QDir::home() && QDir(m_folder) != QDir::root()) {
		m_scope.items.push_back(PopupItem(tr9("in “%1”").arg(folderName(m_folder))));
		m_scope.selected = 2;
	}
	m_scope.chosen = [this](int i) {
		m_scope.selected = i;
		update();
	};

	m_find = PanelButton(tr9("Search"), QRect(findX, SEARCH_Y, findW, PL_BUTTON_H), true);
	m_find.hint = tr9("Search the chosen place.");
	m_find.clicked = [this] { search(); };
	m_stop = PanelButton(tr9("Stop"), QRect(stopX, SEARCH_Y, stopW, PL_BUTTON_H), false);
	m_stop.hint = tr9("Stop this search.");
	m_stop.enabled = false;
	m_stop.clicked = [this] { stop(); };

	m_host.edits = { &m_search };
	m_host.popups = { &m_scope };
	m_host.buttons = { &m_find, &m_stop };
	m_host.defaultButton = &m_find;
	m_host.setFocus(&m_search);

	m_worker = new BloodhoundSearch(this);
	connect(m_worker, &BloodhoundSearch::batch, this, &BloodhoundWindow::addResults);
	connect(m_worker, &BloodhoundSearch::finished, this, &BloodhoundWindow::finished);
	setFocus();
}

BloodhoundWindow::~BloodhoundWindow() = default;

FindQuery BloodhoundWindow::query() const {
	FindQuery q;
	if (m_scope.selected == 1) {
		q.scope = FindScope::Home;
	} else if (m_scope.selected >= 2) {
		q.scope = FindScope::Folder;
		q.folder = m_folder;
	} else {
		q.scope = FindScope::AllDisks;
	}
	FindCriterion criterion;
	criterion.field = FindField::Name;
	criterion.op = FindOp::Contains;
	criterion.text = m_search.text.trimmed();
	q.criteria << criterion;
	return q;
}

void BloodhoundWindow::search() {
	FindQuery q = query();
	QString why;
	if (!q.ready(&why)) {
		m_problem = why;
		m_searching = false;
		update();
		return;
	}
	m_items.clear();
	m_scrollY = 0;
	m_problem.clear();
	m_searching = true;
	m_find.enabled = false;
	m_stop.enabled = true;
	m_listFocus = false;
	m_host.setFocus(&m_search);
	update();
	m_worker->start(q);
}

void BloodhoundWindow::stop() {
	m_worker->stop();
	m_searching = false;
	m_find.enabled = true;
	m_stop.enabled = false;
	update();
}

void BloodhoundWindow::addResults(const QStringList &paths) {
	for (const QString &path : paths) {
		const QFileInfo info(path);
		if (info.exists() || info.isSymLink()) {
			m_items.push_back(makeItem(info));
		}
	}
	update();
}

void BloodhoundWindow::finished(const QString &problem) {
	m_searching = false;
	m_find.enabled = true;
	m_stop.enabled = false;
	if (!problem.isEmpty()) {
		m_problem = problem;
	}
	std::sort(m_items.begin(), m_items.end(), [](const auto &a, const auto &b) {
		const int order = a->visibleName().compare(b->visibleName(), Qt::CaseInsensitive);
		return order == 0 ? a->path < b->path : order < 0;
	});
	m_pressItem = nullptr;
	update();
}

std::vector<Item *> BloodhoundWindow::selectedItems() {
	std::vector<Item *> out;
	for (auto &item : m_items) {
		if (item->selected) {
			out.push_back(item.get());
		}
	}
	return out;
}

void BloodhoundWindow::selectOnly(Item *item) {
	for (auto &candidate : m_items) {
		candidate->selected = candidate.get() == item;
	}
	if (item) {
		int index = 0;
		for (size_t i = 0; i < m_items.size(); i++) {
			if (m_items[i].get() == item) {
				index = static_cast<int>(i);
			}
		}
		if (index * ROW_H < m_scrollY) {
			scrollTo(index * ROW_H);
		} else if ((index + 1) * ROW_H > m_scrollY + listHeight()) {
			scrollTo((index + 1) * ROW_H - listHeight());
		}
	}
	update();
}

void BloodhoundWindow::moveSelection(int step) {
	const int n = static_cast<int>(m_items.size());
	if (n == 0) {
		return;
	}
	int index = -1;
	for (int i = 0; i < n; i++) {
		if (m_items[i]->selected) {
			index = i;
			break;
		}
	}
	index = index < 0 ? (step < 0 ? n - 1 : 0) : std::clamp(index + step, 0, n - 1);
	selectOnly(m_items[index].get());
}

void BloodhoundWindow::runFinderCtl(const QStringList &args) {
	QString ctl = qEnvironmentVariable("ZACOS9_FINDER_CTL");
	if (ctl.isEmpty()) {
		ctl = siblingProgram("zacos9-finder-ctl");
	}
	if (ctl.isEmpty() || !QProcess::startDetached(ctl, args)) {
		pl_beep();
	}
}

void BloodhoundWindow::openSelection() {
	for (Item *item : selectedItems()) {
		runFinderCtl({ "open", item->path });
	}
}

void BloodhoundWindow::revealSelection() {
	for (Item *item : selectedItems()) {
		runFinderCtl({ "reveal", item->path });
	}
}

void BloodhoundWindow::getInfo() {
	const std::vector<Item *> selected = selectedItems();
	if (!selected.empty()) {
		runFinderCtl({ "command", "info " + selected.front()->path });
	}
}

void BloodhoundWindow::copyPaths() {
	QStringList paths;
	for (Item *item : selectedItems()) {
		paths << item->path;
	}
	if (!paths.isEmpty()) {
		QApplication::clipboard()->setText(paths.join('\n'));
	}
}

int BloodhoundWindow::listTop() const {
	return RESULTS_Y + HEADER_H + BAND_H;
}

int BloodhoundWindow::paneTop() const {
	return H - STATUS_H;
}

int BloodhoundWindow::listHeight() const {
	return std::max(0, paneTop() - listTop());
}

pl_scrollbar BloodhoundWindow::bar() const {
	const int content = static_cast<int>(m_items.size()) * ROW_H;
	const int view = listHeight();
	pl_scrollbar v{};
	v.vertical = true;
	v.length = view + 2;
	v.enabled = content > view;
	sb_smart(&v, view, content);
	if (v.enabled) {
		v.thumb = static_cast<int>(static_cast<long long>(m_scrollY) *
			sb_thumb_span(&v) / (content - view));
	}
	return v;
}

void BloodhoundWindow::scrollTo(int y) {
	const int max = std::max(0, static_cast<int>(m_items.size()) * ROW_H - listHeight());
	m_scrollY = std::clamp(y, 0, max);
	update();
}

Item *BloodhoundWindow::itemAt(QPoint pos) const {
	if (pos.y() < listTop() || pos.y() >= paneTop() || pos.x() >= W - SB_W + 1) {
		return nullptr;
	}
	const int row = (pos.y() - listTop() + m_scrollY) / ROW_H;
	return row >= 0 && row < static_cast<int>(m_items.size()) ? m_items[row].get() : nullptr;
}

void BloodhoundWindow::paintEvent(QPaintEvent *) {
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, FACE);

	/* The channel bar: Files is the only channel in 1.0. */
	pl_fill(c, 0, 0, W - 1, CHANNEL_H - 1, GRAY(0xC));
	pl_hline(c, 0, W - 1, CHANNEL_H - 1, GRAY(0x8));
	pl_icon_paint(c, 10, 6, PL_ICON_FOLDER, PL_ICON_SMALL, false);
	Text files(tr9("Files"), 200, PL_FONT_SYSTEM);
	pl_text(c, files.t, 30, 19, C_BLACK);

	/* Search row. */
	m_host.paintControls(c, FACE);

	/* Results header. */
	pl_fill(c, 0, RESULTS_Y, W - 1, RESULTS_Y + HEADER_H - 1, FACE);
	pl_hline(c, 0, W - 1, RESULTS_Y + HEADER_H - 1, GRAY(0xA));
	const int count = static_cast<int>(m_items.size());
	QString header;
	if (m_searching) {
		header = tr9("Searching…  %1 found").arg(count);
	} else if (!m_problem.isEmpty()) {
		header = m_problem;
	} else if (count == 0) {
		header = tr9("No items found");
	} else if (count == 1) {
		header = tr9("1 item found");
	} else {
		header = tr9("%1 items found").arg(count);
	}
	Text headerText(header, W - 20, PL_FONT_VIEWS);
	pl_text(c, headerText.t, (W - headerText.inkWidth()) / 2, RESULTS_Y + 14,
		m_problem.isEmpty() ? C_BLACK : RGB(0xAA, 0x22, 0x22));

	/* Column titles, drawn like a folder window's unsorted columns. */
	pl_canvas band = { c->px + (RESULTS_Y + HEADER_H) * c->stride, c->stride, 0, 0, W, BAND_H, c->log };
	auto column = [&](int x0, int x1, const QString &title, int textX) {
		pl_hline(&band, x0, x1, 0, GRAY(0x6));
		pl_hline(&band, x0 + 1, x1, 1, C_WHITE);
		pl_fill(&band, x0 + 2, 2, x1, 18, GRAY(0xC));
		pl_vline(&band, x0, 0, 19, GRAY(0x6));
		pl_vline(&band, x0 + 1, 1, 18, C_WHITE);
		pl_hline(&band, x0 + 1, x1, 19, GRAY(0x8));
		pl_hline(&band, x0, x1, 20, GRAY(0x3));
		pl_vline(&band, x1, 1, 19, GRAY(0x8));
		Text t(title, x1 - textX - 2, PL_FONT_VIEWS);
		pl_text(&band, t.t, textX, 13, C_BLACK);
	};
	column(0, NAME_W - 1, tr9("Name"), NAME_X);
	column(NAME_W, WHERE_X + WHERE_W - 1, tr9("Where"), WHERE_X + 6);
	column(DATE_X, DATE_X + DATE_W - 1, tr9("Date Modified"), DATE_X + 6);
	column(SIZE_X, SIZE_X + SIZE_W - 1, tr9("Size"), SIZE_X + 6);
	column(KIND_X, KIND_X + KIND_W - 1, tr9("Kind"), KIND_X + 6);

	/* Rows, clipped to the list area. */
	const int top = listTop(), viewW = W - SB_W + 1;
	pl_fill(c, 0, top, viewW - 1, paneTop() - 1, C_WHITE);
	pl_canvas list = { c->px + top * c->stride, c->stride, 0, m_scrollY, viewW, listHeight(), c->log };
	const int first = m_scrollY / ROW_H;
	const int last = std::min<int>(static_cast<int>(m_items.size()) - 1,
		(m_scrollY + listHeight()) / ROW_H);
	for (int r = first; r <= last; r++) {
		Item *item = m_items[r].get();
		const int y = r * ROW_H;
		pl_hline(&list, 0, viewW - 1, y + ROW_H - 1, GRAY(0xE));
		paintIcon(&list, *item, 8, y + 1, PL_ICON_SMALL, item->selected);
		Text name(item->visibleName(), NAME_W - NAME_X - 6, item->nameFont());
		uint32_t ink = C_BLACK;
		if (item->selected) {
			pl_fill(&list, NAME_X - 2, y + 2, NAME_X + name.inkWidth() + 1, y + 14, C_BLACK);
			ink = C_WHITE;
		}
		pl_text(&list, name.t, NAME_X, y + 13, ink);
		Text where(QDir::toNativeSeparators(QFileInfo(item->path).absolutePath()),
			WHERE_W - 10, PL_FONT_VIEWS);
		pl_text(&list, where.t, WHERE_X + 6, y + 13, C_BLACK);
		Text date(formatDate(item->modified), DATE_W - 10, PL_FONT_VIEWS);
		pl_text(&list, date.t, DATE_X + 6, y + 13, C_BLACK);
		const QString size = item->isDir ? QString::fromUtf8("—") : formatSize(item->size);
		Text sizeText(size, SIZE_W - 10, PL_FONT_VIEWS);
		pl_text(&list, sizeText.t, SIZE_X + 6, y + 13, C_BLACK);
		Text kind(item->kindName(), KIND_W - 8, PL_FONT_VIEWS);
		pl_text(&list, kind.t, KIND_X + 6, y + 13, C_BLACK);
	}

	/* Bottom pane: the selected path, or how many are listed. */
	const int pt = paneTop();
	pl_hline(c, 0, W - 1, pt, C_BLACK);
	pl_fill(c, 0, pt + 1, W - 1, H - 1, GRAY(0xD));
	pl_hline(c, 0, W - 1, pt + 1, C_WHITE);
	const std::vector<Item *> selected = selectedItems();
	if (selected.size() == 1) {
		Text where(selected.front()->path, W - 2 * MARGIN - SB_W, PL_FONT_VIEWS);
		pl_text(c, where.t, MARGIN, pt + 15, C_BLACK);
	} else if (count > 0) {
		Text items(count == 1 ? tr9("1 item") : tr9("%1 items").arg(count),
			W - 2 * MARGIN - SB_W, PL_FONT_VIEWS);
		pl_text(c, items.t, MARGIN, pt + 15, C_BLACK);
	}

	pl_scrollbar v = bar();
	pl_scrollbar_paint(c, W - SB_W + 1, top - 1, &v, pl_accent_current());
	if (m_listFocus) {
		pl_focus_ring_paint(c, 0, top - 1, W - SB_W, pt, pl_accent_current(), FACE);
	}

	QPainter p(this);
	px.blit(p);
}

void BloodhoundWindow::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	if (m_host.hostPress(e)) {
		m_listFocus = false;
		update();
		return;
	}
	const QPoint pos = e->position().toPoint();
	if (pos.x() >= W - SB_W + 1 && pos.y() >= listTop() - 1 && pos.y() < paneTop()) {
		pl_scrollbar v = bar();
		const int along = pos.y() - (listTop() - 1);
		m_sbPart = sb_hit(&v, along);
		const int page = listHeight() - ROW_H;
		switch (m_sbPart) {
		case SB_DEC_ARROW: scrollTo(m_scrollY - ROW_H); break;
		case SB_INC_ARROW: scrollTo(m_scrollY + ROW_H); break;
		case SB_DEC_PAGE: scrollTo(m_scrollY - page); break;
		case SB_INC_PAGE: scrollTo(m_scrollY + page); break;
		case SB_THUMB_PART: m_thumbGrab = along - (sb_track_start(&v) + v.thumb); break;
		default: break;
		}
		return;
	}
	if (pos.y() >= listTop() && pos.y() < paneTop() && pos.x() < W - SB_W) {
		m_listFocus = true;
		m_host.setFocus(nullptr);
		Item *hit = itemAt(pos);
		const bool toggle = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier);
		if (toggle && hit) {
			hit->selected = !hit->selected;
		} else {
			for (auto &item : m_items) {
				item->selected = item.get() == hit;
			}
		}
		m_pressItem = hit;
		m_pressPos = pos;
		m_dragging = false;
		update();
	}
}

void BloodhoundWindow::mouseMoveEvent(QMouseEvent *e) {
	m_host.hostMove(e);
	const QPoint pos = e->position().toPoint();
	if (m_sbPart == SB_THUMB_PART && (e->buttons() & Qt::LeftButton)) {
		pl_scrollbar v = bar();
		const int range = sb_thumb_span(&v);
		const int content = static_cast<int>(m_items.size()) * ROW_H - listHeight();
		if (range > 0 && content > 0) {
			const int thumb = pos.y() - (listTop() - 1) - m_thumbGrab - sb_track_start(&v);
			scrollTo(static_cast<int>(static_cast<long long>(thumb) * content / range));
		}
		return;
	}
	if (m_pressItem && !m_dragging && (e->buttons() & Qt::LeftButton) &&
			(pos - m_pressPos).manhattanLength() >= 4) {
		m_dragging = true;
		QList<QUrl> urls;
		for (Item *item : selectedItems()) {
			urls << QUrl::fromLocalFile(item->path);
		}
		if (!urls.isEmpty()) {
			auto *mime = new QMimeData;
			mime->setUrls(urls);
			auto *drag = new QDrag(this);
			drag->setMimeData(mime);
			drag->exec(Qt::CopyAction | Qt::MoveAction);
		}
		m_pressItem = nullptr;
	}
}

void BloodhoundWindow::mouseReleaseEvent(QMouseEvent *e) {
	m_host.hostRelease(e);
	m_sbPart = SB_NONE;
	m_pressItem = nullptr;
	m_dragging = false;
	update();
}

void BloodhoundWindow::mouseDoubleClickEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		Item *hit = itemAt(e->position().toPoint());
		if (hit) {
			selectOnly(hit);
			openSelection();
		}
	}
}

void BloodhoundWindow::wheelEvent(QWheelEvent *e) {
	scrollTo(m_scrollY - e->angleDelta().y() / 120 * ROW_H * 3);
}

void BloodhoundWindow::keyPressEvent(QKeyEvent *e) {
	const bool cmd = e->modifiers() & Qt::ControlModifier;
	const int key = e->key();
	if (cmd && key == Qt::Key_F) {
		m_listFocus = false;
		m_host.setFocus(&m_search);
		m_search.selectAll();
		update();
		return;
	}
	if (cmd && key == Qt::Key_W) {
		close();
		return;
	}
	if (cmd && key == Qt::Key_Q) {
		QApplication::quit();
		return;
	}
	if (m_listFocus) {
		if (cmd && key == Qt::Key_R) {
			revealSelection();
			return;
		}
		if (cmd && key == Qt::Key_I) {
			getInfo();
			return;
		}
		if (cmd && key == Qt::Key_C) {
			copyPaths();
			return;
		}
		if (cmd && key == Qt::Key_A) {
			for (auto &item : m_items) {
				item->selected = true;
			}
			update();
			return;
		}
		if (key == Qt::Key_Up) {
			moveSelection(-1);
			return;
		}
		if (key == Qt::Key_Down) {
			moveSelection(1);
			return;
		}
		if (key == Qt::Key_Home) {
			moveSelection(-static_cast<int>(m_items.size()));
			return;
		}
		if (key == Qt::Key_End) {
			moveSelection(static_cast<int>(m_items.size()));
			return;
		}
		if (key == Qt::Key_Return || key == Qt::Key_Enter) {
			openSelection();
			return;
		}
	}
	if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
		m_listFocus = !m_listFocus;
		if (m_listFocus) {
			m_host.setFocus(nullptr);
		} else {
			m_host.setFocus(&m_search);
		}
		update();
		return;
	}
	if (key == Qt::Key_Escape) {
		if (m_worker->running()) {
			stop();
		} else {
			m_search.setText(QString());
			m_items.clear();
			m_problem.clear();
			m_scrollY = 0;
			m_search.selectAll();
			update();
		}
		return;
	}
	if (m_host.hostKey(e)) {
		update();
		return;
	}
	QWidget::keyPressEvent(e);
}
