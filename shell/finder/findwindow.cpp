#include "findwindow.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include "settings.h"
#include "infowindow.h"

static constexpr uint32_t FACE = GRAY(0xD);

/* ---- Find File ------------------------------------------------------------ */

static constexpr int DIALOG_W = 380, DIALOG_H = 100;
static constexpr int FIELD_TOP = 32, FIELD_H = 22;
static constexpr int FIELD_BASELINE = 15;

static QPointer<FindDialog> &findDialog() {
	static QPointer<FindDialog> dialog;
	return dialog;
}

void FindDialog::open() {
	if (!findDialog()) {
		findDialog() = new FindDialog;
	}
	findDialog()->show();
	findDialog()->raise();
	findDialog()->activateWindow();
}

FindDialog::FindDialog() {
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle("Find File");
	setFixedSize(DIALOG_W, DIALOG_H);
	m_caretTimer.callOnTimeout([this] {
		m_caretOn = !m_caretOn;
		update();
	});
	m_caretTimer.start(QApplication::cursorFlashTime() / 2);
}

QRect FindDialog::findButton() const {
	return QRect(width() - 12 - PL_BUTTON_MIN_W, height() - 12 - PL_BUTTON_H,
		PL_BUTTON_MIN_W, PL_BUTTON_H);
}

void FindDialog::paintEvent(QPaintEvent *) {
	const int W = width(), H = height();
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, FACE);

	Text prompt("Find items whose name contains:", W - 24, PL_FONT_SYSTEM);
	pl_text(c, prompt.t, 12, 22, C_BLACK);

	/* Edit text box: an inset frame around a white field.
	 * TODO: measure the Platinum edit-text frame. */
	const int x0 = 12, x1 = W - 13, y0 = FIELD_TOP, y1 = FIELD_TOP + FIELD_H - 1;
	pl_hline(c, x0, x1, y0, GRAY(0x8));
	pl_vline(c, x0, y0, y1, GRAY(0x8));
	pl_hline(c, x0, x1, y1, C_WHITE);
	pl_vline(c, x1, y0, y1, C_WHITE);
	pl_outline(c, x0 + 1, y0 + 1, x1 - 1, y1 - 1, C_BLACK);
	pl_fill(c, x0 + 2, y0 + 2, x1 - 2, y1 - 2, C_WHITE);
	Text field(m_text, x1 - x0 - 12, PL_FONT_VIEWS);
	int caretX = x0 + 5;
	if (field.t && field.t->ink_l >= 0) {
		pl_text(c, field.t, x0 + 5 + field.t->ink_l - 1, y0 + FIELD_BASELINE, C_BLACK);
		caretX = x0 + 5 + field.t->advance;
	}
	if (m_caretOn) {
		pl_vline(c, caretX, y0 + 4, y1 - 4, C_BLACK);
	}

	const QRect b = findButton();
	Text label("Find", 100, PL_FONT_SYSTEM);
	unsigned flags = PL_BUTTON_DEFAULT;
	if (m_tracking && m_inside) {
		flags |= PL_BUTTON_PRESSED;
	}
	if (m_text.isEmpty()) {
		flags |= PL_BUTTON_DISABLED;
	}
	pl_button_paint(c, b.x(), b.y(), b.width(), label.t, flags);

	QPainter p(this);
	px.blit(p);
}

void FindDialog::find() {
	if (m_text.isEmpty()) {
		return;
	}
	FoundWindow::search(m_text);
	close();
}

void FindDialog::keyPressEvent(QKeyEvent *e) {
	const bool cmd = e->modifiers() & Qt::ControlModifier;
	if (e->key() == Qt::Key_Escape || (cmd && (e->key() == Qt::Key_W ||
			e->key() == Qt::Key_Period))) {
		close();
		return;
	}
	if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		find();
		return;
	}
	if (e->key() == Qt::Key_Backspace) {
		m_text.chop(1);
	} else if (!cmd && !e->text().isEmpty() && e->text().at(0).isPrint()) {
		m_text += e->text();
	} else {
		return;
	}
	m_caretOn = true;
	update();
}

void FindDialog::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton && !m_text.isEmpty() &&
			findButton().contains(e->position().toPoint())) {
		m_tracking = m_inside = true;
		update();
	}
}

void FindDialog::mouseMoveEvent(QMouseEvent *e) {
	if (m_tracking) {
		const bool inside = findButton().contains(e->position().toPoint());
		if (inside != m_inside) {
			m_inside = inside;
			update();
		}
	}
}

void FindDialog::mouseReleaseEvent(QMouseEvent *) {
	if (!m_tracking) {
		return;
	}
	const bool chosen = m_inside;
	m_tracking = m_inside = false;
	update();
	if (chosen) {
		find();
	}
}

/* ---- Items Found ---------------------------------------------------------- */

static constexpr int HEADER_H = 21;  /* as a folder window's item-count header */
static constexpr int BAND_H = 21;    /* column titles */
static constexpr int ROW_H = 19, ROW_BASELINE = 13;
static constexpr int ICON_X = 8, NAME_X = 30;
static constexpr int NAME_W = 250, DATE_W = 190;
static constexpr int PANE_H = 26;    /* where the selected item is */
static constexpr int MAX_RESULTS = 5000;

/* Not worth searching: kernel and device trees, other volumes. */
static bool skipDir(const QString &path) {
	static const QStringList skip = {
		"/proc", "/sys", "/dev", "/run", "/mnt", "/media", "/tmp", "/lost+found",
	};
	return skip.contains(path);
}

static std::vector<QPointer<FoundWindow>> &foundWindows() {
	static std::vector<QPointer<FoundWindow>> windows;
	return windows;
}

void FoundWindow::search(const QString &text) {
	auto *w = new FoundWindow(text);
	foundWindows().push_back(w);
	w->show();
	Finder::instance().setFront(w);
}

void FoundWindow::reloadAll() {
	for (auto &w : foundWindows()) {
		if (w) {
			w->reload();
		}
	}
}

FoundWindow::FoundWindow(const QString &text) : m_text(text) {
	setAttribute(Qt::WA_DeleteOnClose);
	resize(560, 320);
	setMinimumSize(300, 160);
	updateTitle();

	/* Walk the disk off the main thread, handing matches over in batches. */
	QPointer<FoundWindow> self(this);
	m_worker = QThread::create([self, text] {
		QStringList batch, stack = { "/" };
		int found = 0;
		QElapsedTimer sent;
		sent.start();
		auto flush = [&] {
			if (batch.isEmpty()) {
				return;
			}
			QMetaObject::invokeMethod(qApp, [self, batch] {
				if (self) {
					self->addResults(batch);
				}
			});
			batch.clear();
			sent.restart();
		};
		while (!stack.isEmpty() && found < MAX_RESULTS &&
				!QThread::currentThread()->isInterruptionRequested()) {
			const QString dir = stack.takeLast();
			const auto entries = QDir(dir).entryInfoList(
				QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System,
				QDir::Name | QDir::IgnoreCase | QDir::Reversed);
			for (const QFileInfo &info : entries) {
				const QString path = info.absoluteFilePath();
				if (info.isDir() && !info.isSymLink() && !skipDir(path)) {
					stack.push_back(path);
				}
				if (info.fileName().contains(text, Qt::CaseInsensitive) && found < MAX_RESULTS) {
					batch << path;
					found++;
				}
			}
			if (batch.size() >= 200 || sent.elapsed() > 250) {
				flush();
			}
		}
		flush();
		QMetaObject::invokeMethod(qApp, [self] {
			if (self) {
				self->finished();
			}
		});
	});
	m_worker->start();
}

FoundWindow::~FoundWindow() {
	if (m_worker) {
		m_worker->requestInterruption();
		m_worker->wait();
		delete m_worker;
	}
}

void FoundWindow::addResults(const QStringList &paths) {
	for (const QString &path : paths) {
		m_items.push_back(makeItem(QFileInfo(path)));
	}
	updateTitle();
	update();
}

void FoundWindow::finished() {
	m_searching = false;
	updateTitle();
	update();
}

void FoundWindow::updateTitle() {
	setWindowTitle(QStringLiteral("Items Found: %1").arg(m_items.size()));
}

QString FoundWindow::folderPath() const {
	return QDir::homePath();
}

std::vector<Item *> FoundWindow::selectedItems() {
	std::vector<Item *> out;
	for (auto &item : m_items) {
		if (item->selected) {
			out.push_back(item.get());
		}
	}
	return out;
}

void FoundWindow::selectByName(const QString &name) {
	for (auto &item : m_items) {
		item->selected = item->name == name;
	}
	update();
}

void FoundWindow::reload() {
	std::vector<std::unique_ptr<Item>> kept;
	for (auto &item : m_items) {
		QFileInfo info(item->path);
		if (info.exists() || info.isSymLink()) {
			auto fresh = makeItem(info);
			fresh->selected = item->selected;
			kept.push_back(std::move(fresh));
		}
	}
	m_items = std::move(kept);
	updateTitle();
	scrollTo(m_scrollY);
	update();
}

int FoundWindow::listTop() const {
	return HEADER_H + BAND_H;
}

int FoundWindow::paneTop() const {
	return height() - PANE_H;
}

int FoundWindow::listHeight() const {
	return std::max(0, paneTop() - listTop());
}

pl_scrollbar FoundWindow::bar() const {
	const int content = static_cast<int>(m_items.size()) * ROW_H;
	const int view = listHeight();
	pl_scrollbar v = { true, view + 2, content > view, 0 };
	if (v.enabled) {
		v.thumb = static_cast<int>(static_cast<long long>(m_scrollY) *
			sb_thumb_range(v.length) / (content - view));
	}
	return v;
}

void FoundWindow::scrollTo(int y) {
	const int max = std::max(0, static_cast<int>(m_items.size()) * ROW_H - listHeight());
	m_scrollY = std::clamp(y, 0, max);
	update();
}

Item *FoundWindow::itemAt(QPoint pos) {
	if (pos.y() < listTop() || pos.y() >= paneTop() || pos.x() >= width() - SB_WIDTH + 1) {
		return nullptr;
	}
	const int r = (pos.y() - listTop() + m_scrollY) / ROW_H;
	return r < static_cast<int>(m_items.size()) ? m_items[r].get() : nullptr;
}

void FoundWindow::paintEvent(QPaintEvent *) {
	const int W = width(), H = height();
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, C_WHITE);

	/* Header, as in a folder window: the count, or progress. */
	pl_hline(c, 0, W - 1, 0, C_WHITE);
	pl_fill(c, 0, 1, W - 1, HEADER_H - 2, GRAY(0xD));
	pl_vline(c, 0, 1, HEADER_H - 2, C_WHITE);
	pl_hline(c, 0, W - 1, HEADER_H - 1, GRAY(0xA));
	const QString count = QStringLiteral("%1 %2").arg(m_items.size())
		.arg(m_items.size() == 1 ? "item" : "items");
	Text header(m_searching ? "Searching…  " + count : count + " found", W - 20, PL_FONT_VIEWS);
	pl_text(c, header.t, (W - header.inkWidth()) / 2, 13, C_BLACK);

	/* Column titles, drawn like a folder window's unsorted columns. */
	pl_canvas band = { c->px + HEADER_H * c->stride, c->stride, 0, 0, W, BAND_H };
	auto column = [&](int x0, int x1, const char *title, int textX) {
		pl_hline(&band, x0, x1, 0, GRAY(0x6));
		pl_hline(&band, x0 + 1, x1, 1, C_WHITE);
		pl_fill(&band, x0 + 2, 2, x1, 18, GRAY(0xC));
		pl_vline(&band, x0, 0, 19, GRAY(0x6));
		pl_vline(&band, x0 + 1, 1, 18, C_WHITE);
		pl_hline(&band, x0 + 1, x1, 19, GRAY(0x8));
		pl_hline(&band, x0, x1, 20, GRAY(0x3));
		pl_vline(&band, x1, 1, 19, GRAY(0x8));
		if (title) {
			Text t(title, x1 - textX - 2, PL_FONT_VIEWS);
			pl_text(&band, t.t, textX, 13, C_BLACK);
		}
	};
	column(0, NAME_W - 1, "Name", NAME_X);
	column(NAME_W, NAME_W + DATE_W - 1, "Date Modified", NAME_W + 6);
	column(NAME_W + DATE_W, W - 1, "Kind", NAME_W + DATE_W + 6);

	/* Rows, clipped to the list area. */
	const int top = listTop(), viewW = W - SB_WIDTH + 1;
	pl_canvas list = { c->px + top * c->stride, c->stride, 0, m_scrollY, viewW, listHeight() };
	const int first = m_scrollY / ROW_H;
	const int last = std::min<int>(m_items.size() - 1, (m_scrollY + listHeight()) / ROW_H);
	for (int r = first; r <= last; r++) {
		Item *item = m_items[r].get();
		const int y = r * ROW_H;
		pl_hline(&list, 0, viewW - 1, y + ROW_H - 1, GRAY(0xE));
		pl_icon_paint_label(&list, ICON_X, y + 1, item->kind, PL_ICON_SMALL, item->selected,
			item->labelColor());
		Text name(item->name, NAME_W - NAME_X - 6, item->nameFont());
		uint32_t ink = C_BLACK;
		if (item->selected) {
			pl_fill(&list, NAME_X - 2, y + 2, NAME_X + name.inkWidth() + 1, y + 14, C_BLACK);
			ink = C_WHITE;
		}
		pl_text(&list, name.t, NAME_X, y + ROW_BASELINE, ink);
		Text date(finderDate(item->modified), DATE_W - 12, PL_FONT_VIEWS);
		pl_text(&list, date.t, NAME_W + 6, y + ROW_BASELINE, C_BLACK);
		Text kind(item->kindName(), std::max(8, viewW - NAME_W - DATE_W - 12), PL_FONT_VIEWS);
		pl_text(&list, kind.t, NAME_W + DATE_W + 6, y + ROW_BASELINE, C_BLACK);
	}

	/* Bottom pane: where the selected item is. Its right end stays clear
	 * of the resize box. */
	const int pt = paneTop();
	pl_hline(c, 0, W - 1, pt, C_BLACK);
	pl_fill(c, 0, pt + 1, W - 1, H - 1, GRAY(0xD));
	pl_hline(c, 0, W - 1, pt + 1, C_WHITE);
	std::vector<Item *> sel = selectedItems();
	if (sel.size() == 1) {
		pl_icon_paint(c, ICON_X, pt + 6, PL_ICON_FOLDER, PL_ICON_SMALL, false);
		Text where(macPath(QFileInfo(sel[0]->path).absolutePath()),
			W - NAME_X - SB_WIDTH - 8, PL_FONT_VIEWS);
		pl_text(c, where.t, NAME_X, pt + 18, C_BLACK);
	}

	pl_scrollbar v = bar();
	pl_scrollbar_paint(c, W - SB_WIDTH + 1, top - 1, &v, pl_accent_current());

	QPainter p(this);
	px.blit(p);
}

void FoundWindow::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const QPoint pos = e->position().toPoint();
	Finder::instance().setFront(this);
	if (pos.x() >= width() - SB_WIDTH + 1 && pos.y() >= listTop() - 1 && pos.y() < paneTop()) {
		pl_scrollbar v = bar();
		const int along = pos.y() - (listTop() - 1);
		m_sbPart = sb_hit(&v, along);
		const int page = listHeight() - ROW_H;
		switch (m_sbPart) {
		case SB_DEC_ARROW: scrollTo(m_scrollY - ROW_H); break;
		case SB_INC_ARROW: scrollTo(m_scrollY + ROW_H); break;
		case SB_DEC_PAGE: scrollTo(m_scrollY - page); break;
		case SB_INC_PAGE: scrollTo(m_scrollY + page); break;
		case SB_THUMB_PART: m_thumbGrab = along - (SB_ARROW + 1 + v.thumb); break;
		default: break;
		}
		return;
	}
	Item *hit = itemAt(pos);
	if (e->modifiers() & Qt::ShiftModifier) {
		if (hit) {
			hit->selected = !hit->selected;
		}
	} else {
		for (auto &item : m_items) {
			item->selected = item.get() == hit;
		}
	}
	update();
	Finder::instance().notifyState();
}

void FoundWindow::mouseMoveEvent(QMouseEvent *e) {
	if (m_sbPart != SB_THUMB_PART) {
		return;
	}
	pl_scrollbar v = bar();
	const int range = sb_thumb_range(v.length);
	const int content = static_cast<int>(m_items.size()) * ROW_H - listHeight();
	if (range <= 0 || content <= 0) {
		return;
	}
	const int thumb = static_cast<int>(e->position().y()) - (listTop() - 1) - m_thumbGrab -
		(SB_ARROW + 1);
	scrollTo(static_cast<int>(static_cast<long long>(thumb) * content / range));
}

void FoundWindow::mouseReleaseEvent(QMouseEvent *) {
	m_sbPart = SB_NONE;
}

void FoundWindow::mouseDoubleClickEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton && itemAt(e->position().toPoint())) {
		Finder::instance().setFront(this);
		Finder::instance().openSelection();
	}
}

void FoundWindow::wheelEvent(QWheelEvent *e) {
	scrollTo(m_scrollY - e->angleDelta().y() / 120 * ROW_H * 3);
}

void FoundWindow::keyPressEvent(QKeyEvent *e) {
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
		close();
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		Finder::instance().openSelection();
	} else if (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) {
		/* Arrow keys move a single selection through the list. */
		int index = -1;
		for (size_t i = 0; i < m_items.size(); i++) {
			if (m_items[i]->selected) {
				index = static_cast<int>(i);
				if (e->key() == Qt::Key_Up) {
					break;
				}
			}
		}
		index += e->key() == Qt::Key_Up ? -1 : 1;
		index = std::clamp<int>(index, 0, static_cast<int>(m_items.size()) - 1);
		for (size_t i = 0; i < m_items.size(); i++) {
			m_items[i]->selected = static_cast<int>(i) == index;
		}
		if (index * ROW_H < m_scrollY) {
			scrollTo(index * ROW_H);
		} else if ((index + 1) * ROW_H > m_scrollY + listHeight()) {
			scrollTo((index + 1) * ROW_H - listHeight());
		}
		update();
		Finder::instance().notifyState();
	}
}

void FoundWindow::changeEvent(QEvent *e) {
	if (e->type() == QEvent::ActivationChange && isActiveWindow()) {
		Finder::instance().setFront(this);
	}
	QWidget::changeEvent(e);
}

void FoundWindow::closeEvent(QCloseEvent *e) {
	Finder::instance().viewClosed(this);
	QWidget::closeEvent(e);
}
