#include "store.h"

#include <QCloseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include "alert.h"
#include "appdb.h"
#include "pixels.h"
#include "settings.h"

static constexpr int W = 520, H = 400;
static constexpr int MARGIN = 10;
static constexpr uint32_t FACE = GRAY(0xD), PANE = GRAY(0xE);
static constexpr int CAT_W = 130;
static constexpr int GAP = 10;
static constexpr int DETAIL_H = 150;
static constexpr int LIST_BOTTOM = H - MARGIN - DETAIL_H - GAP;
static constexpr int ICON_SIZE = 32;
static constexpr int SWEEP_MS = 30; /* the busy sweep's frame interval */

/* The real icon of an installed item, matched by the package dpkg says
 * it is (see appdb.h's AppEntry::origin): empty if it isn't installed,
 * has no GUI entry of its own, or nothing resolved. Never looked up for
 * an item that isn't installed: nothing on disk to find it from yet. */
static std::vector<uint32_t> installedIcon(const StoreItem &item) {
	if (item.packages.isEmpty()) {
		return {};
	}
	const QString origin = "dpkg:" + item.packages.first();
	for (const AppEntry &app : appList()) {
		if (app.origin == origin && !app.icon32.empty()) {
			return app.icon32;
		}
	}
	return {};
}

StoreWindow::StoreWindow() {
	setWindowTitle("Software");
	setFixedSize(W, H);

	m_categories = storeCategories();
	m_allItems = storeItems();
	m_categoryList.frame = QRect(MARGIN, MARGIN, CAT_W, LIST_BOTTOM - MARGIN);
	m_itemList.frame =
		QRect(MARGIN + CAT_W + GAP, MARGIN, W - MARGIN - (MARGIN + CAT_W + GAP), LIST_BOTTOM - MARGIN);
	m_categoryList.setItems(m_categories);
	m_categoryList.picked = [this](int row) { selectCategory(row); };
	m_itemList.picked = [this](int row) { selectItem(row); };

	m_actionButton = PanelButton("Install", QRect(W - MARGIN - 90, H - MARGIN - 20, 90, 20));
	m_actionButton.clicked = [this] { act(); };
	m_host.buttons = { &m_actionButton };

	m_sweep.setInterval(SWEEP_MS);
	m_sweep.callOnTimeout([this] {
		m_sweepPos += 0.03;
		if (m_sweepPos > 1.3) {
			m_sweepPos = -0.3;
		}
		update();
	});

	if (m_categories.isEmpty() || m_allItems.empty()) {
		m_status = "The software catalog could not be read.";
		m_actionButton.enabled = false;
	} else {
		m_categoryList.select(0, true); /* "Featured" */
	}
}

void StoreWindow::selectCategory(int row) {
	if (row < 0 || row >= m_categories.size()) {
		return;
	}
	m_shown = storeItemsIn({ m_categories[row] }, m_allItems);
	QStringList names;
	for (const StoreItem *item : m_shown) {
		names << item->name;
	}
	m_itemList.setItems(names);
	if (!m_shown.empty()) {
		m_itemList.select(0, true);
	} else {
		m_itemList.state.selected = -1;
		m_status.clear();
		m_actionButton.enabled = false;
	}
	update();
}

const StoreItem *StoreWindow::currentItem() const {
	const int row = m_itemList.state.selected;
	return row >= 0 && row < static_cast<int>(m_shown.size()) ? m_shown[row] : nullptr;
}

void StoreWindow::selectItem(int row) {
	(void)row;
	refreshInstalled();
	update();
}

void StoreWindow::refreshInstalled() {
	const StoreItem *item = currentItem();
	if (!item) {
		return;
	}
	m_installed = packagesInstalled(item->packages);
	m_actionButton.label = std::make_unique<Text>(m_installed ? "Remove" : "Install",
		m_actionButton.rect.width() - 8, PL_FONT_SYSTEM);
	m_actionButton.enabled = true;
	m_status.clear();
}

void StoreWindow::act() {
	const StoreItem *item = currentItem();
	if (!item || m_busy) {
		return;
	}
	if (m_installed) {
		if (!Alert::ask(QStringLiteral("Remove “%1”? This removes it from your "
				"computer; your documents are not touched.").arg(item->name),
				"Remove", "Cancel")) {
			return;
		}
	}
	const QString name = item->name;
	const QStringList packages = item->packages;
	const bool wasInstalled = m_installed;
	m_busy = true;
	m_actionButton.enabled = false;
	m_status = (wasInstalled ? "Removing " : "Installing ") + name + "…";
	m_sweepPos = 0;
	m_sweep.start();
	update();

	bool ok = false;
	QString err;
	runAppstoreHelper(QStringList{ wasInstalled ? "remove" : "install" } + packages, &ok, &err);

	m_sweep.stop();
	m_busy = false;
	if (!ok) {
		m_status = err.isEmpty() ? "That didn't work." : err;
	} else {
		m_status.clear();
	}
	/* The catalog list may be showing a different item by the time a
	 * slow install finishes; only touch its button if it's still this
	 * one, so a quick click elsewhere isn't stomped on. */
	if (currentItem() && currentItem()->id == item->id) {
		refreshInstalled();
	}
	update();
}

void StoreWindow::paintDetail(pl_canvas *c, uint32_t bg) const {
	const int top = LIST_BOTTOM + GAP;
	pl_hline(c, MARGIN, W - MARGIN - 1, top, GRAY(0x8));
	pl_hline(c, MARGIN, W - MARGIN - 1, top + 1, C_WHITE);
	const int inside = top + 14;
	const StoreItem *item = currentItem();
	if (!item) {
		if (!m_status.isEmpty()) {
			panelText(c, m_status, MARGIN, inside + 9, PL_FONT_VIEWS, GRAY(0x5));
		}
		return;
	}
	const std::vector<uint32_t> icon = m_installed ? installedIcon(*item) : std::vector<uint32_t>{};
	if (!icon.empty()) {
		pl_image_blend(c, MARGIN, inside, icon.data(), ICON_SIZE, ICON_SIZE, false);
	} else {
		pl_icon_paint(c, MARGIN, inside, PL_ICON_APPLICATION, ICON_SIZE, false);
	}
	const int textX = MARGIN + ICON_SIZE + 10;
	const int textW = W - MARGIN - 90 - 10 - textX;
	panelText(c, item->name, textX, inside + 11, PL_FONT_SYSTEM, C_BLACK, textW);
	int y = inside + 11 + 16;
	for (const QString &line : panelWrap(item->blurb, textW, PL_FONT_VIEWS)) {
		panelText(c, line, textX, y, PL_FONT_VIEWS, GRAY(0x5));
		y += 12;
	}

	const int barY = H - MARGIN - 20 - 8 - PL_PROGRESS_H;
	if (m_busy) {
		pl_progress_paint(c, textX, barY, W - MARGIN - 90 - 10 - textX, m_sweepPos,
			pl_accent_current());
	} else if (!m_status.isEmpty()) {
		panelText(c, m_status, textX, barY + PL_PROGRESS_H - 1, PL_FONT_VIEWS, GRAY(0x5));
	} else {
		panelText(c, m_installed ? "Installed." : "Not installed.", textX, barY + PL_PROGRESS_H - 1,
			PL_FONT_VIEWS, GRAY(0x5));
	}
	(void)bg;
}

void StoreWindow::paintEvent(QPaintEvent *) {
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, FACE);
	m_categoryList.paint(c, true);
	m_itemList.paint(c, true);
	paintDetail(c, PANE);
	m_host.paintControls(c, FACE);
	QPainter p(this);
	px.blit(p);
}

void StoreWindow::mousePressEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (m_categoryList.press(pos) || m_itemList.press(pos)) {
		update();
		return;
	}
	m_host.hostPress(e);
}

void StoreWindow::mouseMoveEvent(QMouseEvent *e) {
	if (m_host.hostMove(e)) {
		update();
	}
}

void StoreWindow::mouseReleaseEvent(QMouseEvent *e) {
	if (m_host.hostRelease(e)) {
		update();
	}
}

void StoreWindow::keyPressEvent(QKeyEvent *e) {
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
		close();
		return;
	}
	if (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) {
		if (m_itemList.key(e->key(), QString())) {
			update();
			return;
		}
	}
	if (m_host.hostKey(e)) {
		update();
	}
}

void StoreWindow::closeEvent(QCloseEvent *e) {
	if (m_busy) {
		/* apt is mid-transaction through the helper (a separate,
		 * detached process by the time pkexec has forked it): closing
		 * the window only stops watching it, never interrupts it, so a
		 * half-finished install can't leave dpkg's database locked. */
		e->accept();
		return;
	}
	e->accept();
}
