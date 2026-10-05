#include "store.h"

#include <QCloseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <algorithm>

#include "alert.h"
#include "appdb.h"
#include "pixels.h"
#include "settings.h"

static constexpr int W = 620, H = 400;
static constexpr int MARGIN = 10;
static constexpr uint32_t FACE = GRAY(0xD), PANE = GRAY(0xE);
static constexpr int CAT_W = 155;
static constexpr int GAP = 10;
static constexpr int DETAIL_H = 150;
static constexpr int LIST_BOTTOM = H - MARGIN - DETAIL_H - GAP;
static constexpr int ICON_SIZE = 32;
static constexpr int SWEEP_MS = 30; /* the busy sweep's frame interval */
static constexpr int STYLE_BTN_W = 150; /* the preset button, beside Install/Remove */

/* The real icon of an installed item, matched by the package dpkg says
 * it is (see appdb.h's AppEntry::origin): empty if it isn't installed,
 * has no GUI entry of its own, or nothing resolved. Never looked up for
 * an item that isn't installed: nothing on disk to find it from yet. */
static std::vector<uint32_t> installedIcon(const StoreItem &item) {
	if (item.packages.isEmpty()) {
		return {};
	}
	const QString origin = (item.source == "flathub" ? "flatpak:" : "dpkg:") + item.packages.first();
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
	m_categories << "All Applications" << "Flathub" << "Additional Sources";
	m_categoryList.frame = QRect(MARGIN, 42, CAT_W, LIST_BOTTOM - 42);
	m_itemList.frame =
		QRect(MARGIN + CAT_W + GAP, 42, W - MARGIN - (MARGIN + CAT_W + GAP), LIST_BOTTOM - 42);
	m_search.rect = QRect(70, MARGIN, W - 80 - MARGIN, PL_EDIT_H);
	m_search.edited = [this] { if (!m_busy) { filterItems(); } };
	m_host.edits = { &m_search };
	m_categoryList.setItems(m_categories);
	m_categoryList.picked = [this](int row) { selectCategory(row); };
	m_itemList.picked = [this](int row) { selectItem(row); };

	m_actionButton = PanelButton("Install", QRect(W - MARGIN - 90, H - MARGIN - 20, 90, 20));
	m_actionButton.clicked = [this] { act(); };
	m_styleButton = PanelButton("Preset", QRect(W - MARGIN - 90 - 8 - STYLE_BTN_W, H - MARGIN - 20,
		STYLE_BTN_W, 20));
	m_styleButton.clicked = [this] { actStyle(); };
	m_sourceButton = PanelButton("Enable Flathub", QRect(MARGIN + CAT_W + GAP, 132, 155, 20));
	m_sourceButton.clicked = [this] { configureSource(); };
	m_refreshButton = PanelButton("Refresh", QRect(W - MARGIN - 90, 132, 90, 20));
	m_refreshButton.clicked = [this] {
		bool ok;
		QString error;
		m_flathubEnabled = flathubEnabled(&ok, &error);
		if (!ok) {
			m_status = error;
			update();
			return;
		}
		m_flathubLoaded = false;
		m_debianLoaded = false;
		loadSource("debian");
	};
	m_host.buttons = { &m_actionButton };

	m_sweep.setInterval(SWEEP_MS);
	m_sweep.callOnTimeout([this] {
		m_sweepPos += 0.03;
		if (m_sweepPos > 1.3) {
			m_sweepPos = -0.3;
		}
		update();
	});
	m_catalogTimeout.setSingleShot(true);
	m_catalogTimeout.setInterval(120000);
	m_catalogTimeout.callOnTimeout([this] {
		m_catalogTimedOut = true;
		m_catalog.kill();
	});
	connect(&m_catalog, &QProcess::finished, this, [this] { finishCatalog(); });
	connect(&m_catalog, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) {
			finishCatalog();
		}
	});

	if (m_categories.isEmpty() || m_allItems.empty()) {
		m_status = "The software catalog could not be read.";
		m_actionButton.enabled = false;
	} else {
		m_categoryList.select(0, true); /* "Featured" */
	}
}

void StoreWindow::selectCategory(int row) {
	if (m_busy) {
		return;
	}
	if (row < 0 || row >= m_categories.size()) {
		return;
	}
	m_status.clear();
	m_host.buttons = sourcesPage() ? std::vector<PanelButton *>{ &m_sourceButton, &m_refreshButton }
		: std::vector<PanelButton *>{ &m_actionButton };
	if (sourcesPage() || m_categories[row] == "Flathub") {
		bool ok;
		QString error;
		m_flathubEnabled = flathubEnabled(&ok, &error);
		m_sourceButton.label = std::make_unique<Text>(
			m_flathubEnabled ? "Disable Flathub" : "Enable Flathub", 147, PL_FONT_SYSTEM);
		if (!ok) {
			m_status = error;
			filterItems();
			return;
		}
	}
	filterItems();
	if (m_categories[row] == "All Applications" && !m_debianLoaded) {
		loadSource("debian");
	} else if (m_categories[row] == "Flathub") {
		if (!m_flathubEnabled) {
			m_status = "Enable Flathub in Additional Sources to browse its applications.";
		} else if (!m_flathubLoaded) {
			loadSource("flathub");
		}
	}
	update();
}

bool StoreWindow::sourcesPage() const {
	return m_categoryList.state.selected >= 0 &&
		m_categories.value(m_categoryList.state.selected) == "Additional Sources";
}

void StoreWindow::filterItems() {
	m_shown.clear();
	const QString category = m_categories.value(m_categoryList.state.selected);
	const QString query = m_search.text.trimmed();
	for (const StoreItem &item : m_allItems) {
		const bool inCategory = category == "All Applications" ? item.source == "debian"
			: category == "Flathub" ? item.source == "flathub" && m_flathubEnabled
			: category == "Featured" ? item.featured : item.category == category;
		if (inCategory && (query.isEmpty() || item.name.contains(query, Qt::CaseInsensitive) ||
				item.blurb.contains(query, Qt::CaseInsensitive) ||
				item.packages.join(' ').contains(query, Qt::CaseInsensitive))) {
			m_shown.push_back(&item);
		}
	}
	QStringList names;
	for (const StoreItem *item : m_shown) {
		names << item->name;
	}
	m_itemList.setItems(names);
	if (!m_shown.empty()) {
		m_itemList.select(0, true);
	} else {
		m_itemList.state.selected = -1;
		m_actionButton.enabled = false;
	}
	update();
}

void StoreWindow::loadSource(const QString &source) {
	if (m_busy) {
		return;
	}
	m_loadingSource = source;
	m_busy = true;
	m_search.enabled = false;
	m_sourceButton.enabled = m_refreshButton.enabled = m_actionButton.enabled = false;
	m_catalogTimedOut = false;
	m_status = "Loading " + (source == "debian" ? QString("Debian applications") : QString("Flathub")) + "...";
	m_sweep.start();
	m_catalog.start(softwareCatalogHelper(), { source });
	m_catalogTimeout.start();
	update();
}

void StoreWindow::finishCatalog() {
	m_catalogTimeout.stop();
	m_sweep.stop();
	m_busy = false;
	m_search.enabled = true;
	m_sourceButton.enabled = m_refreshButton.enabled = true;
	QString error;
	std::vector<StoreItem> discovered;
	const bool ok = !m_catalogTimedOut && m_catalog.error() != QProcess::FailedToStart &&
		m_catalog.exitStatus() == QProcess::NormalExit && m_catalog.exitCode() == 0;
	if (!ok) {
		error = m_catalogTimedOut ? "Software discovery timed out."
			: m_catalog.error() == QProcess::FailedToStart ? m_catalog.errorString()
			: QString::fromUtf8(m_catalog.readAllStandardError()).trimmed();
		if (error.isEmpty()) {
			error = "Software discovery failed.";
		}
	} else if (parseSoftwareCatalog(m_catalog.readAllStandardOutput(), m_loadingSource, &discovered, &error)) {
		m_shown.clear();
		m_allItems.erase(std::remove_if(m_allItems.begin(), m_allItems.end(),
			[this](const StoreItem &item) {
				return item.category.isEmpty() && item.source == m_loadingSource;
			}), m_allItems.end());
		for (StoreItem &item : discovered) {
			const bool duplicate = std::any_of(m_allItems.begin(), m_allItems.end(),
				[&item](const StoreItem &existing) {
					return existing.source == item.source && existing.packages == item.packages;
				});
			if (!duplicate) {
				m_allItems.push_back(std::move(item));
			}
		}
		if (m_loadingSource == "debian") {
			m_debianLoaded = true;
		} else {
			m_flathubLoaded = true;
		}
	}
	filterItems();
	m_status = error;
	if (!error.isEmpty()) {
		qWarning().noquote() << error;
	} else if (discovered.empty()) {
		m_status = "No desktop applications found. Refresh package metadata in Software Update.";
	}
	update();
}

void StoreWindow::configureSource() {
	if (m_busy) {
		return;
	}
	const bool enabling = !m_flathubEnabled;
	if (!Alert::ask(enabling
			? "Enable Flathub for your account? This downloads metadata from flathub.org. "
			  "Applications are installed separately from Debian packages."
			: "Disable Flathub browsing and updates? Installed applications and their data are kept.",
			enabling ? "Enable" : "Disable", "Cancel")) {
		return;
	}
	m_busy = true;
	m_status = enabling ? "Enabling Flathub..." : "Disabling Flathub...";
	update();
	bool ok = false;
	QString error;
	if (enabling) {
		runFlatpak({ "remote-add", "--if-not-exists", "--from", "flathub",
			"https://dl.flathub.org/repo/flathub.flatpakrepo" }, &ok, &error);
		if (ok) {
			runFlatpak({ "remote-modify", "--enable", "flathub" }, &ok, &error);
		}
	} else {
		runFlatpak({ "remote-modify", "--disable", "flathub" }, &ok, &error);
	}
	m_busy = false;
	m_flathubLoaded = false;
	if (ok) {
		selectCategory(m_categoryList.state.selected);
	} else {
		m_status = error;
		update();
	}
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
	if (item->source == "flathub") {
		bool ok;
		QString error;
		m_installed = flatpakInstalled(item->packages.first(), &ok, &error);
		if (!ok) {
			m_status = error;
			m_actionButton.enabled = false;
			return;
		}
	} else {
		bool ok;
		QString error;
		m_installed = packagesInstalled(item->packages, &ok, &error);
		if (!ok) {
			m_status = error;
			m_actionButton.enabled = false;
			return;
		}
	}
	m_actionButton.label = std::make_unique<Text>(m_installed ? "Remove" : "Install",
		m_actionButton.rect.width() - 8, PL_FONT_SYSTEM);
	m_actionButton.enabled = true;
	m_status.clear();

	/* A look-and-feel preset, once the application is there to use it. */
	const bool preset = m_installed && !item->styleId.isEmpty();
	m_styleOn = preset && styleIsOn(item->styleId);
	if (preset) {
		m_styleButton.label = std::make_unique<Text>(m_styleOn ? item->styleReset : item->styleLabel,
			m_styleButton.rect.width() - 8, PL_FONT_SYSTEM);
		m_styleButton.enabled = true;
		m_host.buttons = { &m_styleButton, &m_actionButton };
	} else {
		m_host.buttons = { &m_actionButton };
	}
}

void StoreWindow::actStyle() {
	const StoreItem *item = currentItem();
	if (!item || m_busy || item->styleId.isEmpty()) {
		return;
	}
	const QString id = item->id;
	const QString styleId = item->styleId;
	const bool turningOff = m_styleOn;
	m_busy = true;
	m_actionButton.enabled = false;
	m_styleButton.enabled = false;
	m_status = QString(turningOff ? "Switching to %1…" : "Switching to %1…")
		.arg(turningOff ? item->styleReset : item->styleLabel);
	m_sweepPos = 0;
	m_sweep.start();
	update();

	bool ok = false;
	QString err;
	runStyleHelper({ turningOff ? "reset" : "apply", styleId }, &ok, &err);

	m_sweep.stop();
	m_busy = false;
	if (currentItem() && currentItem()->id == id) {
		refreshInstalled();
	}
	/* After the refresh, which clears the status line. */
	if (!ok) {
		m_status = err.isEmpty() ? "That didn't work." : err;
	}
	update();
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
	const QString id = item->id;
	const QString source = item->source;
	const bool wasInstalled = m_installed;
	m_busy = true;
	m_actionButton.enabled = false;
	m_styleButton.enabled = false;
	m_status = (wasInstalled ? "Removing " : "Installing ") + name + "…";
	m_sweepPos = 0;
	m_sweep.start();
	update();

	bool ok = false;
	QString err;
	if (source == "flathub") {
		runFlatpak(wasInstalled
			? QStringList{ "uninstall", "--noninteractive", "--", packages.first() }
			: QStringList{ "install", "--noninteractive", "flathub", packages.first() }, &ok, &err);
	} else {
		runAppstoreHelper(QStringList{ wasInstalled ? "remove" : "install" } + packages, &ok, &err);
	}

	m_sweep.stop();
	m_busy = false;
	/* The catalog list may be showing a different item by the time a
	 * slow install finishes; only touch its button if it's still this
	 * one, so a quick click elsewhere isn't stomped on. */
	if (currentItem() && currentItem()->id == id) {
		refreshInstalled();
	}
	if (!ok) {
		m_status = err.isEmpty() ? "That didn't work." : err;
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
	if (m_installed && !item->styleId.isEmpty()) {
		y += 4;
		for (const QString &line : panelWrap(item->styleBlurb, textW, PL_FONT_VIEWS)) {
			panelText(c, line, textX, y, PL_FONT_VIEWS, GRAY(0x5));
			y += 12;
		}
	}

	const int barY = H - MARGIN - 20 - 8 - PL_PROGRESS_H;
	if (m_busy) {
		pl_progress_paint(c, textX, barY, W - MARGIN - 90 - 10 - textX, m_sweepPos,
			pl_accent_current());
	} else if (!m_status.isEmpty()) {
		panelText(c, m_status, textX, barY + PL_PROGRESS_H - 1, PL_FONT_VIEWS, GRAY(0x5));
	} else {
		QString state = (item->source == "flathub" ? "Flathub: " : "Debian: ") +
			QString(m_installed ? "Installed." : "Not installed.");
		if (m_styleOn) {
			state += " " + item->styleLabel + " is on.";
		}
		panelText(c, state, textX, barY + PL_PROGRESS_H - 1, PL_FONT_VIEWS, GRAY(0x5));
	}
	(void)bg;
}

void StoreWindow::paintEvent(QPaintEvent *) {
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, FACE);
	panelText(c, "Search:", MARGIN, MARGIN + 14, PL_FONT_SYSTEM, C_BLACK);
	m_categoryList.paint(c, !m_busy);
	if (sourcesPage()) {
		const int x = MARGIN + CAT_W + GAP;
		panelText(c, "Debian applications", x, 58, PL_FONT_SYSTEM, C_BLACK);
		panelText(c, "Uses your configured APT repositories.", x, 76, PL_FONT_VIEWS, C_BLACK);
		panelText(c, "Flathub (optional, for this account)", x, 102, PL_FONT_SYSTEM, C_BLACK);
		panelText(c, m_flathubEnabled ? "Enabled." : "Disabled. No source is added until you enable it.",
			x, 120, PL_FONT_VIEWS, C_BLACK, W - x - MARGIN);
		if (!m_status.isEmpty()) {
			int y = 185;
			for (const QString &line : panelWrap(m_status, W - x - MARGIN, PL_FONT_VIEWS)) {
				panelText(c, line, x, y, PL_FONT_VIEWS, C_BLACK);
				y += 12;
			}
		}
	} else {
		m_itemList.paint(c, !m_busy);
		paintDetail(c, PANE);
	}
	m_host.paintControls(c, FACE);
	QPainter p(this);
	px.blit(p);
}

void StoreWindow::mousePressEvent(QMouseEvent *e) {
	if (m_busy) {
		return;
	}
	const QPoint pos = e->position().toPoint();
	if (m_categoryList.press(pos) || (!sourcesPage() && m_itemList.press(pos))) {
		m_host.setFocus(nullptr);
		update();
		return;
	}
	m_host.hostPress(e);
}

void StoreWindow::mouseMoveEvent(QMouseEvent *e) {
	if (m_busy) {
		return;
	}
	const QPoint pos = e->position().toPoint();
	if (m_categoryList.move(pos) | (!sourcesPage() && m_itemList.move(pos)) || m_host.hostMove(e)) {
		update();
	}
}

void StoreWindow::mouseReleaseEvent(QMouseEvent *e) {
	if (m_categoryList.release() | m_itemList.release() | m_host.hostRelease(e)) {
		update();
	}
}

void StoreWindow::wheelEvent(QWheelEvent *e) {
	if (m_busy) {
		return;
	}
	const QPoint pos = e->position().toPoint();
	const int dy = e->angleDelta().y();
	if (m_categoryList.wheel(pos, dy) || (!sourcesPage() && m_itemList.wheel(pos, dy))) {
		update();
	}
}

void StoreWindow::keyPressEvent(QKeyEvent *e) {
	if (m_busy) {
		return;
	}
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
		close();
		return;
	}
	if (!m_host.focus && !sourcesPage() && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down)) {
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
