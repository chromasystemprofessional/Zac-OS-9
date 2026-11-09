#include "bloodhoundwindow.h"

#include "alert.h"
#include "appdb.h"
#include "findindex.h"
#include "icons.h"
#include "programs.h"
#include "search.h"
#include "settings.h"
#include "sharingclient.h"
#include "tr.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDate>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QWheelEvent>
#include <algorithm>

#include "conf.h"

static constexpr int W = 760, H = 620, MARGIN = 12;
static constexpr int CHANNEL_H = 28;
static constexpr int SEARCH_Y = 38;
static constexpr int SEARCH_X = MARGIN, SEARCH_W = 330;
static constexpr int SCOPE_X = SEARCH_X + SEARCH_W + 8;
static constexpr int CRITERIA_Y = 68, ROW_STEP = 28, MAX_ROWS = 6;
static constexpr int FIELD_W = 140, OP_W = 180, GAP = 6;
static constexpr int NOTE_H = 17;
static constexpr int RESULTS_Y = 100;
static constexpr int HEADER_H = 21, BAND_H = 21, ROW_H = 19, SNIPPET_H = 13;
static constexpr int STATUS_H = 22, SB_W = 15;
static constexpr int PREVIEW_H = 92;
static constexpr int NAME_X = 30, NAME_W = 260;
static constexpr int WHERE_X = 260, WHERE_W = 190;
static constexpr int DATE_X = 450, DATE_W = 105;
static constexpr int SIZE_X = 555, SIZE_W = 60;
static constexpr int KIND_X = 615, KIND_W = 130;
static constexpr uint32_t FACE = GRAY(0xD);
static constexpr int MAX_SNIPPETS = 200;

/* The fields, in the menu's order. */
static const FindField FIELDS[] = { FindField::Name, FindField::Contents, FindField::Modified,
	FindField::Created, FindField::Size, FindField::Kind, FindField::Label, FindField::Locked,
	FindField::Folder, FindField::CustomIcon, FindField::Comment, FindField::Invisible,
	FindField::Version };

struct BloodhoundWindow::CriterionRow {
	FindField field = FindField::Modified;
	PanelPopup fieldPopup, opPopup, choicePopup;
	PanelEdit edit;
	FindOp op() const {
		const QList<FindOp> ops = findOpsFor(field);
		return ops.value(std::clamp(opPopup.selected, 0, int(ops.size()) - 1));
	}
};

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

static QString bloodhoundConfigFolder() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/zacos9";
}

static QString bloodhoundConfPath() {
	return bloodhoundConfigFolder() + "/bloodhound.conf";
}

static QString bloodhoundRecentsPath() {
	return bloodhoundConfigFolder() + "/bloodhound-recents.json";
}

BloodhoundWindow::BloodhoundWindow(const QString &folder)
	: m_folder(folder), m_live(tr9("Live Search"), QPoint(0, 0)) {
	setWindowTitle(tr9("Bloodhound (Search)"));
	setFixedSize(W, H);
	setFocusPolicy(Qt::StrongFocus);
	setAcceptDrops(true);
	setAccessibleName(tr9("Bloodhound (Search)"));
	setAccessibleDescription(tr9("Search files and the web."));
	setAttribute(Qt::WA_DeleteOnClose);

	m_search.rect = QRect(SEARCH_X, SEARCH_Y, SEARCH_W, PL_EDIT_H);
	m_search.hint = tr9("Search for");
	m_search.edited = [this] {
		m_problem.clear();
		m_status.clear();
		liveSearchSoon();
		update();
	};
	m_scope.rect = QRect(SCOPE_X, SEARCH_Y, 100, PL_POPUP_H);
	m_scope.hint = tr9("Where to search.");
	m_scope.chosen = [this](int i) {
		m_scope.selected = i;
		liveSearchSoon();
		update();
	};
	m_site.hint = tr9("Search with");
	m_site.chosen = [this](int i) {
		m_site.selected = i;
		m_status.clear();
		m_problem.clear();
		update();
	};

	const int stopW = buttonWidth(tr9("Stop"), 44);
	const int findW = buttonWidth(tr9("Search"), 62);
	const int stopX = W - MARGIN - stopW;
	const int findX = stopX - 6 - findW;
	m_scope.rect.setWidth(std::max(100, findX - 6 - SCOPE_X));
	m_find = PanelButton(tr9("Search"), QRect(findX, SEARCH_Y, findW, PL_BUTTON_H), true);
	m_find.hint = tr9("Search the chosen place.");
	m_find.clicked = [this] { search(); };
	m_stop = PanelButton(tr9("Stop"), QRect(stopX, SEARCH_Y, stopW, PL_BUTTON_H), false);
	m_stop.hint = tr9("Stop this search.");
	m_stop.enabled = false;
	m_stop.clicked = [this] { stop(); };

	m_more = PanelButton(tr9("More Choices"), QRect(0, 0, buttonWidth(tr9("More Choices"), 105),
		PL_BUTTON_H), false);
	m_more.hint = tr9("Add another criterion.");
	m_more.clicked = [this] { addCriterion(); };
	m_fewer = PanelButton(tr9("Fewer Choices"), QRect(0, 0, buttonWidth(tr9("Fewer Choices"), 105),
		PL_BUTTON_H), false);
	m_fewer.hint = tr9("Remove the last criterion.");
	m_fewer.clicked = [this] { removeCriterion(); };
	m_saveSearch = PanelButton(tr9("Save Search…"), QRect(0, 0, buttonWidth(tr9("Save Search…"), 105),
		PL_BUTTON_H), false);
	m_saveSearch.hint = tr9("Save these criteria.");
	m_saveSearch.clicked = [this] { saveSearch(); };
	m_savedSearches = PanelButton(tr9("Saved…"), QRect(0, 0,
		buttonWidth(tr9("Saved…"), 65), PL_BUTTON_H), false);
	m_savedSearches.hint = tr9("Use a saved search.");
	m_savedSearches.clicked = [this] { showSavedSearches(); };
	m_indexSelection = PanelButton(tr9("Index…"), QRect(0, 0,
		buttonWidth(tr9("Index…"), 65), PL_BUTTON_H), false);
	m_indexSelection.hint = tr9("Add, remove, report or rebuild the contents index.");
	m_indexSelection.clicked = [this] { showIndexMenu(); };
	m_recents = PanelButton(tr9("Recents…"), QRect(0, 0, buttonWidth(tr9("Recents…"), 85),
		PL_BUTTON_H), false);
	m_recents.hint = tr9("Use a recent search.");
	m_recents.clicked = [this] { showRecents(); };
	m_preview = PanelButton(tr9("Preview"), QRect(W - MARGIN - 70, H - STATUS_H + 2, 70, 18), false);
	m_preview.hint = tr9("Show or hide the preview pane.");
	m_preview.clicked = [this] {
		m_previewOn = !m_previewOn;
		update();
	};
	m_live.hint = tr9("Search as you type.");
	m_live.toggled = [this](bool on) {
		saveSetting("live-search", on ? "yes" : "no");
		if (on) {
			liveSearchSoon();
		}
	};
	m_liveTimer.setSingleShot(true);
	m_liveTimer.setInterval(400);
	connect(&m_liveTimer, &QTimer::timeout, this, &BloodhoundWindow::runLiveSearch);

	m_host.edits = { &m_search };
	m_host.popups = { &m_scope };
	m_host.buttons = { &m_more, &m_fewer, &m_saveSearch, &m_savedSearches, &m_indexSelection,
		&m_recents, &m_find, &m_stop };
	m_host.checks = { &m_live };
	m_host.defaultButton = &m_find;
	m_host.setFocus(&m_search);

	rebuildScope();
	m_worker = new BloodhoundSearch(this);
	connect(m_worker, &BloodhoundSearch::batch, this, &BloodhoundWindow::addResults);
	connect(m_worker, &BloodhoundSearch::finished, this, &BloodhoundWindow::finished);
	loadSettings();
	loadRecents();
	reloadSites();
	relayout();
	setFocus();
}

BloodhoundWindow::~BloodhoundWindow() = default;

void BloodhoundWindow::rebuildScope(FindScope scope) {
	m_scope.items = { PopupItem(tr9("on all disks")), PopupItem(tr9("in my Home folder")) };
	m_scope.selected = 0;
	if (!m_folder.isEmpty() && QDir(m_folder) != QDir::home() && QDir(m_folder) != QDir::root()) {
		m_scope.items.push_back(PopupItem(tr9("in “%1”").arg(folderName(m_folder))));
		if (scope != FindScope::Home) {
			m_scope.selected = 2;
		}
	}
	if (scope == FindScope::Home && m_scope.items.size() > 1) {
		m_scope.selected = 1;
	} else if (scope == FindScope::Folder) {
		if (m_scope.items.size() < 3) {
			m_scope.items.push_back(PopupItem(tr9("in “%1”").arg(folderName(m_folder))));
		}
		m_scope.selected = 2;
	}
}

void BloodhoundWindow::reloadSites() {
	m_sites = searchSites(nullptr);
	rebuildChannels();
	if (!m_channels.contains(m_channelName)) {
		m_channelName = "Files";
	}
	updateSitePopup();
}

void BloodhoundWindow::loadSettings() {
	const KeyValueFile conf = KeyValueFile::read(bloodhoundConfPath());
	m_live.on = conf.yes("live-search");
	const QString channel = conf.value("channel");
	if (!channel.isEmpty()) {
		m_channelName = channel;
	}
}

void BloodhoundWindow::saveSetting(const QString &key, const QString &value) {
	KeyValueFile conf = KeyValueFile::read(bloodhoundConfPath());
	conf.set(key, value);
	QDir().mkpath(bloodhoundConfigFolder());
	conf.save(bloodhoundConfPath());
}

void BloodhoundWindow::loadRecents() {
	m_recentsList.clear();
	QFile file(bloodhoundRecentsPath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
	for (const QJsonValue &value : doc.array()) {
		if (value.isString() && !value.toString().trimmed().isEmpty()) {
			m_recentsList << value.toString();
		}
	}
	m_recentsList = m_recentsList.mid(0, 10);
}

void BloodhoundWindow::saveRecents() {
	QJsonArray array;
	for (const QString &recent : m_recentsList) {
		array.append(recent);
	}
	const QByteArray data = QJsonDocument(array).toJson(QJsonDocument::Indented);
	QDir().mkpath(bloodhoundConfigFolder());
	QSaveFile file(bloodhoundRecentsPath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(data);
		file.commit();
	}
}

void BloodhoundWindow::rememberRecent(const QString &words) {
	const QString clean = words.trimmed();
	if (clean.isEmpty()) {
		return;
	}
	m_recentsList.removeAll(clean);
	m_recentsList.prepend(clean);
	m_recentsList = m_recentsList.mid(0, 10);
	saveRecents();
}

void BloodhoundWindow::showRecents() {
	QMenu menu(this);
	for (const QString &recent : m_recentsList) {
		QAction *action = menu.addAction(recent);
		connect(action, &QAction::triggered, this, [this, recent] {
			m_search.setText(recent);
			search();
		});
	}
	if (m_recentsList.isEmpty()) {
		QAction *empty = menu.addAction(tr9("No recent searches yet."));
		empty->setEnabled(false);
	} else {
		menu.addSeparator();
		QAction *clear = menu.addAction(tr9("Clear Recent Searches"));
		connect(clear, &QAction::triggered, this, [this] {
			m_recentsList.clear();
			saveRecents();
			update();
		});
	}
	menu.exec(mapToGlobal(m_recents.rect.bottomLeft()));
}

void BloodhoundWindow::liveSearchSoon() {
	if (m_live.on && filesChannel()) {
		m_liveTimer.start();
	}
}

void BloodhoundWindow::runLiveSearch() {
	if (!m_live.on || !filesChannel()) {
		return;
	}
	const FindQuery q = query();
	if (q.ready(nullptr)) {
		search(false);
	}
}

void BloodhoundWindow::rebuildChannels() {
	m_channels = { "Files" };
	if (!m_sites.isEmpty()) {
		m_channels << "Web";
		QHash<QString, QString> named;
		bool mine = false;
		for (const SearchSite &site : m_sites) {
			if (site.channel.isEmpty()) {
				mine = mine || site.user;
			} else {
				const QString key = site.channel.toLower();
				if (key != "web" && key != "files" && key != "my channel") {
					named.insert(key, site.channel);
				}
			}
		}
		QStringList groups = named.values();
		std::sort(groups.begin(), groups.end(), [](const QString &a, const QString &b) {
			return a.compare(b, Qt::CaseInsensitive) < 0;
		});
		m_channels += groups;
		if (mine) {
			m_channels << "My Channel";
		}
	}
	m_channelHits.clear();
	int x = 10;
	for (const QString &name : m_channels) {
		Text label(name, 200, PL_FONT_SYSTEM);
		const int width = label.inkWidth();
		if (x + width + 12 > W - 10) {
			break;
		}
		const QRect hit(x - 5, 3, width + 10, 23);
		m_channelHits.push_back({ name, hit });
		x = hit.right() + 9;
	}
}

QList<SearchSite> BloodhoundWindow::sitesForChannel(const QString &name) const {
	QList<SearchSite> out;
	if (name == "Web") {
		return m_sites;
	}
	if (name == "My Channel") {
		for (const SearchSite &site : m_sites) {
			if (site.user && site.channel.isEmpty()) {
				out << site;
			}
		}
		return out;
	}
	for (const SearchSite &site : m_sites) {
		if (!site.channel.isEmpty() && site.channel.compare(name, Qt::CaseInsensitive) == 0) {
			out << site;
		}
	}
	return out;
}

void BloodhoundWindow::updateSitePopup() {
	m_site.items.clear();
	if (filesChannel()) {
		m_site.selected = 0;
		return;
	}
	const QList<SearchSite> sites = sitesForChannel(m_channelName);
	for (const SearchSite &site : sites) {
		m_site.items.push_back(PopupItem(site.name));
	}
	if (m_site.selected >= int(m_site.items.size())) {
		m_site.selected = 0;
	}
}

void BloodhoundWindow::chooseChannel(const QString &name) {
	m_channelName = name;
	m_problem.clear();
	m_status.clear();
	m_items.clear();
	m_snippets.clear();
	m_snippetQueue.clear();
	m_snippetRunning = false;
	m_contentWords.clear();
	m_similarTo.clear();
	m_scrollY = 0;
	m_listFocus = false;
	m_searching = false;
	m_worker->stop();
	++m_generation;
	m_find.enabled = true;
	m_stop.enabled = false;
	reloadSites();
	if (filesChannel()) {
		m_search.hint = tr9("Search for");
	} else {
		m_search.hint = tr9("Search the web for");
	}
	saveSetting("channel", m_channelName);
	relayout();
	update();
}

void BloodhoundWindow::addCriterion(FindField field) {
	if (int(m_rows.size()) >= MAX_ROWS) {
		return;
	}
	auto row = std::make_unique<CriterionRow>();
	CriterionRow *r = row.get();
	for (FindField f : FIELDS) {
		r->fieldPopup.items.push_back(PopupItem(findFieldName(f)));
	}
	r->fieldPopup.chosen = [this, r](int i) {
		setCriterionField(*r, FIELDS[i]);
		liveSearchSoon();
		relayout();
	};
	r->opPopup.chosen = [this, r](int i) {
		r->opPopup.selected = i;
		liveSearchSoon();
		relayout();
	};
	r->choicePopup.chosen = [this, r](int i) {
		r->choicePopup.selected = i;
		liveSearchSoon();
		update();
	};
	r->edit.edited = [this] {
		m_problem.clear();
		liveSearchSoon();
		update();
	};
	setCriterionField(*r, field);
	m_rows.push_back(std::move(row));
	relayout();
	liveSearchSoon();
}

void BloodhoundWindow::removeCriterion() {
	if (m_rows.empty()) {
		return;
	}
	m_rows.pop_back();
	relayout();
	liveSearchSoon();
}

void BloodhoundWindow::setCriterionField(CriterionRow &row, FindField field) {
	row.field = field;
	row.fieldPopup.selected =
		int(std::find(std::begin(FIELDS), std::end(FIELDS), field) - std::begin(FIELDS));
	row.opPopup.items.clear();
	for (FindOp op : findOpsFor(field)) {
		row.opPopup.items.push_back(PopupItem(findOpName(op)));
	}
	row.opPopup.selected = 0;
	row.choicePopup.items.clear();
	row.choicePopup.selected = 0;
	if (field == FindField::Kind) {
		for (const QString &kind : findKindNames()) {
			row.choicePopup.items.push_back(PopupItem(kind));
		}
	} else if (field == FindField::Label) {
		for (int i = 0; i < PL_LABEL_COUNT; i++) {
			row.choicePopup.items.push_back(PopupItem(QString::fromUtf8(pl_labels[i].name)));
		}
	}
	row.edit.setText(field == FindField::Modified || field == FindField::Created
		? QDate::currentDate().toString(Qt::ISODate) : QString());
	row.edit.accepts = field == FindField::Size
		? std::function<bool(QChar)>([](QChar c) { return c.isDigit(); })
		: std::function<bool(QChar)>();
}

bool BloodhoundWindow::hasContentsRow() const {
	for (const auto &row : m_rows) {
		if (row->field == FindField::Contents) {
			return true;
		}
	}
	return false;
}

void BloodhoundWindow::relayout() {
	if (!filesChannel()) {
		m_liveTimer.stop();
		m_host.popups = { &m_site };
		m_host.edits = { &m_search };
		m_host.buttons = { &m_find };
		m_host.checks.clear();
		m_site.rect = QRect(SEARCH_X, SEARCH_Y, 230, PL_POPUP_H);
		const int findX = m_find.rect.x();
		m_search.rect = QRect(SEARCH_X + 238, SEARCH_Y,
			std::max(100, findX - 6 - (SEARCH_X + 238)), PL_EDIT_H);
		m_search.hint = tr9("Search the web for");
		m_find.enabled = true;
		m_stop.enabled = false;
		m_resultsTop = CRITERIA_Y + 30;
		m_listFocus = false;
		m_host.defaultButton = &m_find;
		if (m_host.focus != &m_search) {
			m_host.setFocus(&m_search);
		}
		update();
		return;
	}
	m_search.rect = QRect(SEARCH_X, SEARCH_Y, SEARCH_W, PL_EDIT_H);
	m_search.hint = tr9("Search for");
	m_host.popups = { &m_scope };
	m_host.edits = { &m_search };
	int y = CRITERIA_Y;
	for (auto &row : m_rows) {
		row->fieldPopup.rect = QRect(MARGIN, y, FIELD_W, PL_POPUP_H);
		row->opPopup.rect = QRect(MARGIN + FIELD_W + GAP, y, OP_W, PL_POPUP_H);
		const int vx = MARGIN + FIELD_W + GAP + OP_W + GAP;
		row->choicePopup.rect = QRect(vx, y, W - MARGIN - vx, PL_POPUP_H);
		row->edit.rect = QRect(vx, y - 1, W - MARGIN - vx, PL_EDIT_H);
		m_host.popups.push_back(&row->fieldPopup);
		m_host.popups.push_back(&row->opPopup);
		if (row->field == FindField::Kind || row->field == FindField::Label) {
			m_host.popups.push_back(&row->choicePopup);
		} else if (findOpTakesText(row->field, row->op())) {
			m_host.edits.push_back(&row->edit);
		}
		y += ROW_STEP;
	}
	const int buttonsY = y + 4;
	int x = MARGIN;
	for (PanelButton *button : { &m_more, &m_fewer, &m_saveSearch, &m_savedSearches,
			&m_indexSelection, &m_recents }) {
		button->rect.moveTo(x, buttonsY);
		x = button->rect.right() + 8;
	}
	m_live.pos = QPoint(x, buttonsY + 4);
	m_more.enabled = int(m_rows.size()) < MAX_ROWS;
	m_fewer.enabled = !m_rows.empty();
	const bool contents = hasContentsRow();
	const int noteY = buttonsY + PL_BUTTON_H + 2;
	m_resultsTop = noteY + (contents ? NOTE_H : 0) + 6;
	m_host.buttons = { &m_more, &m_fewer, &m_saveSearch, &m_savedSearches, &m_indexSelection,
		&m_recents, &m_find, &m_stop };
	m_host.checks = { &m_live };
	if (!m_listFocus &&
			std::find(m_host.edits.begin(), m_host.edits.end(), m_host.focus) == m_host.edits.end()) {
		m_host.setFocus(&m_search);
	}
	update();
}

FindQuery BloodhoundWindow::query() const {
	FindQuery q;
	q.scope = m_scope.selected == 1 ? FindScope::Home
		: m_scope.selected >= 2 ? FindScope::Folder : FindScope::AllDisks;
	q.folder = m_folder;
	if (!m_search.text.trimmed().isEmpty()) {
		FindCriterion criterion;
		criterion.field = FindField::Name;
		criterion.op = FindOp::Contains;
		criterion.text = m_search.text.trimmed();
		q.criteria << criterion;
	}
	for (const auto &row : m_rows) {
		FindCriterion criterion;
		criterion.field = row->field;
		criterion.op = row->op();
		criterion.text = row->edit.text;
		criterion.choice = row->choicePopup.selected;
		q.criteria << criterion;
	}
	return q;
}

void BloodhoundWindow::search(bool remember) {
	m_liveTimer.stop();
	if (!filesChannel()) {
		reloadSites();
		if (filesChannel()) {
			search();
			return;
		}
		m_problem.clear();
		m_status.clear();
		const QString words = m_search.text.trimmed();
		if (words.isEmpty()) {
			m_problem = tr9("Type the words to search the web for.");
			update();
			return;
		}
		const QList<SearchSite> sites = sitesForChannel(m_channelName);
		if (sites.isEmpty()) {
			m_problem = tr9("There are no search sites in this channel.");
			update();
			return;
		}
		const int index = std::clamp(m_site.selected, 0, int(sites.size()) - 1);
		QString error;
		if (openSearchSite(sites[index], words, &error)) {
			m_status = tr9("Opened “%1” in your browser.").arg(sites[index].name);
		} else {
			m_problem = error;
		}
		update();
		return;
	}
	FindQuery q = query();
	QString why;
	if (!q.ready(&why)) {
		m_problem = why;
		m_searching = false;
		update();
		return;
	}
	if (remember && !m_search.text.trimmed().isEmpty()) {
		rememberRecent(m_search.text);
	}
	for (const SharedFolder &folder : sharedFolders()) {
		const QString path = QFileInfo(folder.path).canonicalFilePath();
		if (!path.isEmpty() && !q.sharedFolders.contains(path)) {
			q.sharedFolders << path;
		}
	}
	if (q.hasVersionCriterion()) {
		for (const AppEntry &app : appList()) {
			const QString path = QFileInfo(app.file).canonicalFilePath();
			if (!path.isEmpty() && !app.version.isEmpty()) {
				q.applicationVersions.insert(path, app.version);
			}
		}
	}
	++m_generation;
	m_contentWords = q.hasContents() ? q.contentWords() : QStringList();
	m_snippets.clear();
	m_snippetQueue.clear();
	m_snippetRequests = 0;
	m_snippetRunning = false;
	m_similarTo.clear();
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
	++m_generation;
	m_snippetQueue.clear();
	m_snippetRunning = false;
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
			if (!m_contentWords.isEmpty()) {
				requestSnippet(path);
			}
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

void BloodhoundWindow::findSimilar() {
	const std::vector<Item *> selected = selectedItems();
	if (selected.empty()) {
		return;
	}
	const QString path = selected.front()->path;
	m_worker->stop();
	++m_generation;
	m_items.clear();
	m_scrollY = 0;
	m_problem.clear();
	m_contentWords.clear();
	m_snippets.clear();
	m_snippetQueue.clear();
	m_snippetRequests = 0;
	m_snippetRunning = false;
	m_similarTo = displayName(path);
	m_searching = true;
	m_find.enabled = false;
	m_stop.enabled = true;
	m_listFocus = true;
	m_host.setFocus(nullptr);
	update();

	const quint64 generation = m_generation;
	QPointer<BloodhoundWindow> self(this);
	indexSimilar(path, [self, generation](const QStringList &paths, const QString &error) {
		if (!self || generation != self->m_generation) {
			return;
		}
		if (!error.isEmpty()) {
			self->finished(error);
			return;
		}
		QStringList present;
		for (const QString &p : paths) {
			if (QFileInfo::exists(p)) {
				present << p;
			}
		}
		self->addResults(present);
		self->finished(QString());
	});
}

void BloodhoundWindow::requestSnippet(const QString &path) {
	if (m_contentWords.isEmpty() || m_snippetRequests >= MAX_SNIPPETS ||
			m_snippets.contains(path) || m_snippetQueue.contains(path)) {
		return;
	}
	m_snippetQueue << path;
	startNextSnippet();
}

void BloodhoundWindow::startNextSnippet() {
	if (m_snippetRunning || m_snippetQueue.isEmpty()) {
		return;
	}
	const QString path = m_snippetQueue.takeFirst();
	m_snippets.insert(path, Snippet{});
	m_snippetRequests++;
	m_snippetRunning = true;
	const quint64 generation = m_generation;
	QPointer<BloodhoundWindow> self(this);
	indexSnippet(path, m_contentWords, [self, generation, path](const IndexSnippet &snippet,
			const QString &) {
		if (!self || generation != self->m_generation) {
			return;
		}
		auto it = self->m_snippets.find(path);
		if (it != self->m_snippets.end()) {
			it->text = snippet.text;
			it->words = snippet.words;
			it->asked = true;
			self->update();
		}
		self->m_snippetRunning = false;
		self->startNextSnippet();
	});
}

void BloodhoundWindow::paintPreview(pl_canvas *c) const {
	const int top = paneTop(), bottom = H - STATUS_H;
	pl_hline(c, 0, W - 1, top, C_BLACK);
	pl_fill(c, 0, top + 1, W - 1, bottom - 1, FACE);
	pl_hline(c, 0, W - 1, top + 1, C_WHITE);
	const std::vector<Item *> selected = selectedItems();
	if (selected.size() != 1) {
		Text hint(selected.empty() ? tr9("Select an item to preview.")
			: tr9("%1 items selected.").arg(selected.size()), W - 2 * MARGIN,
			PL_FONT_VIEWS);
		pl_text(c, hint.t, MARGIN, top + 30, GRAY(0x5));
		return;
	}
	Item *item = selected.front();
	paintIcon(c, *item, MARGIN, top + 8, PL_ICON_LARGE, false);
	const int x = MARGIN + 40, width = W - x - MARGIN;
	Text name(item->visibleName(), width, PL_FONT_SYSTEM);
	pl_text(c, name.t, x, top + 24, C_BLACK);
	Text kind(item->kindName(), width, PL_FONT_VIEWS);
	pl_text(c, kind.t, x, top + 42, C_BLACK);
	const QString size = item->isDir ? QString::fromUtf8("—") : formatSize(item->size);
	Text facts(size + " — " + formatDate(item->modified), width, PL_FONT_VIEWS);
	pl_text(c, facts.t, x, top + 58, C_BLACK);
	Text where(item->path, width, PL_FONT_VIEWS);
	pl_text(c, where.t, x, top + 76, GRAY(0x5));
}

void BloodhoundWindow::paintSnippet(pl_canvas *c, const Snippet &snippet, int x, int baseline,
		int width) const {
	if (snippet.text.isEmpty()) {
		return;
	}
	const int limit = x + width;
	int pos = 0;
	while (pos < snippet.text.size() && x < limit) {
		int at = -1, length = 0;
		for (const QString &word : snippet.words) {
			const int found = snippet.text.indexOf(word, pos, Qt::CaseInsensitive);
			if (found >= 0 && (at < 0 || found < at)) {
				at = found;
				length = word.size();
			}
		}
		if (at < 0) {
			Text rest(snippet.text.mid(pos), 10000, PL_FONT_VIEWS);
			pl_text(c, rest.t, x, baseline, GRAY(0x5));
			break;
		}
		if (at > pos) {
			Text before(snippet.text.mid(pos, at - pos), 10000, PL_FONT_VIEWS);
			pl_text(c, before.t, x, baseline, GRAY(0x5));
			x += before.inkWidth();
		}
		const QString matched = snippet.text.mid(at, length);
		Text text(matched, 10000, PL_FONT_VIEWS);
		if (text.inkWidth() > 0) {
			pl_fill(c, x, baseline - 9, x + text.inkWidth() - 1, baseline + 1,
				RGB(0xFF, 0xF1, 0x86));
			pl_text(c, text.t, x, baseline, C_BLACK);
			x += text.inkWidth();
		}
		pos = at + length;
	}
}

void BloodhoundWindow::saveSearch() {
	bool accepted = false;
	const QString name = QInputDialog::getText(this, tr9("Save Find Search"),
		tr9("Name:"), QLineEdit::Normal, QString(), &accepted);
	if (!accepted) {
		return;
	}
	QString why;
	if (!saveFindQuery(name, query(), &why)) {
		m_problem = why;
		update();
		return;
	}
	m_problem.clear();
	update();
}

void BloodhoundWindow::showSavedSearches() {
	QString why;
	const QStringList names = savedFindQueryNames(&why);
	if (!why.isEmpty()) {
		m_problem = why;
		update();
		return;
	}
	if (names.isEmpty()) {
		m_problem = tr9("No saved Find searches yet.");
		update();
		return;
	}
	QMenu menu(this);
	for (const QString &name : names) {
		QAction *action = menu.addAction(name);
		connect(action, &QAction::triggered, this, [this, name] {
			FindQuery saved;
			QString error;
			if (!loadFindQuery(name, &saved, &error)) {
				m_problem = error;
				update();
				return;
			}
			applyQuery(saved);
			search();
		});
	}
	menu.exec(mapToGlobal(m_savedSearches.rect.bottomLeft()));
}

void BloodhoundWindow::applyQuery(const FindQuery &q) {
	m_rows.clear();
	m_search.setText(QString());
	if (q.scope == FindScope::Folder && !q.folder.isEmpty()) {
		m_folder = q.folder;
	}
	rebuildScope(q.scope);
	for (const FindCriterion &criterion : q.criteria) {
		if (criterion.field == FindField::Name && criterion.op == FindOp::Contains &&
				m_search.text.trimmed().isEmpty()) {
			m_search.setText(criterion.text);
			continue;
		}
		addCriterion(criterion.field);
		CriterionRow *row = m_rows.back().get();
		const QList<FindOp> ops = findOpsFor(criterion.field);
		const int op = ops.indexOf(criterion.op);
		if (op >= 0) {
			row->opPopup.selected = op;
		}
		row->choicePopup.selected = criterion.choice;
		row->edit.setText(criterion.text);
	}
	relayout();
}

void BloodhoundWindow::indexSelection() {
	const QString folder = QFileDialog::getExistingDirectory(this, tr9("Index Selection"),
		QDir::homePath());
	if (folder.isEmpty()) {
		return;
	}
	QPointer<BloodhoundWindow> self(this);
	indexAddFolder(folder, [self, folder](const QString &error) {
		if (!self) {
			return;
		}
		if (!error.isEmpty()) {
			self->m_problem = error;
			self->update();
			return;
		}
		Alert::ask(tr9("“%1” will be indexed in the background. Bloodhound can search its "
			"contents once that's done.").arg(displayName(folder)), "OK", QString());
	});
}

void BloodhoundWindow::showIndexMenu() {
	QMenu menu(this);
	QAction *add = menu.addAction(tr9("Add Folder to Index…"));
	QAction *remove = menu.addAction(tr9("Remove Folder from Index…"));
	menu.addSeparator();
	QAction *status = menu.addAction(tr9("Index Status…"));
	QAction *rebuild = menu.addAction(tr9("Rebuild Index…"));
	QAction *chosen = menu.exec(mapToGlobal(m_indexSelection.rect.bottomLeft()));
	if (chosen == add) {
		indexSelection();
	} else if (chosen == remove) {
		indexRemoval();
	} else if (chosen == status) {
		indexStatusReport();
	} else if (chosen == rebuild) {
		indexRebuildConfirm();
	}
}

void BloodhoundWindow::indexRemoval() {
	const QString folder = QFileDialog::getExistingDirectory(this,
		tr9("Remove Folder from Index"), QDir::homePath());
	if (folder.isEmpty()) {
		return;
	}
	QPointer<BloodhoundWindow> self(this);
	indexRemoveFolder(folder, [self, folder](const QString &error) {
		if (!self) {
			return;
		}
		if (!error.isEmpty()) {
			self->m_problem = error;
			self->update();
			return;
		}
		Alert::ask(tr9("“%1” is no longer in the contents index.")
			.arg(displayName(folder)), "OK", QString());
	});
}

void BloodhoundWindow::indexStatusReport() {
	QPointer<BloodhoundWindow> self(this);
	indexStatus([self](const QString &text, const QString &error) {
		if (!self) {
			return;
		}
		Alert::ask(error.isEmpty() ? text : error, "OK", QString());
	});
}

void BloodhoundWindow::indexRebuildConfirm() {
	if (!Alert::ask(tr9("Rebuild the contents index? Its index is cleared and everything "
			"is read again in the background."), tr9("Rebuild"), tr9("Cancel"))) {
		return;
	}
	QPointer<BloodhoundWindow> self(this);
	indexRebuild([self](const QString &error) {
		if (!self) {
			return;
		}
		Alert::ask(error.isEmpty() ? tr9("The contents index is being rebuilt in the "
			"background.") : error, "OK", QString());
	});
}

std::vector<Item *> BloodhoundWindow::selectedItems() const {
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
		const int step = rowHeight();
		if (index * step < m_scrollY) {
			scrollTo(index * step);
		} else if ((index + 1) * step > m_scrollY + listHeight()) {
			scrollTo((index + 1) * step - listHeight());
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

void BloodhoundWindow::contextMenuEvent(QContextMenuEvent *e) {
	if (!filesChannel()) {
		return;
	}
	Item *hit = itemAt(e->pos());
	if (!hit) {
		return;
	}
	if (!hit->selected) {
		selectOnly(hit);
	}
	QMenu menu(this);
	QAction *open = menu.addAction(tr9("Open"));
	QAction *reveal = menu.addAction(tr9("Reveal"));
	QAction *info = menu.addAction(tr9("Get Info"));
	menu.addSeparator();
	QAction *copy = menu.addAction(tr9("Copy Path"));
	QAction *similar = menu.addAction(tr9("Find Similar Items"));
	QAction *chosen = menu.exec(e->globalPos());
	if (chosen == open) {
		openSelection();
	} else if (chosen == reveal) {
		revealSelection();
	} else if (chosen == info) {
		getInfo();
	} else if (chosen == copy) {
		copyPaths();
	} else if (chosen == similar) {
		findSimilar();
	}
}

void BloodhoundWindow::dragEnterEvent(QDragEnterEvent *e) {
	if (!e->mimeData()->hasUrls()) {
		return;
	}
	const QUrl url = e->mimeData()->urls().value(0);
	if (!url.isLocalFile()) {
		return;
	}
	const QFileInfo info(url.toLocalFile());
	const QString suffix = info.suffix().toLower();
	if (info.isDir() || suffix == "searchsite" || suffix == "xml" || suffix == "src") {
		e->acceptProposedAction();
	}
}

void BloodhoundWindow::dropEvent(QDropEvent *e) {
	if (!e->mimeData()->hasUrls()) {
		return;
	}
	const QUrl url = e->mimeData()->urls().value(0);
	if (!url.isLocalFile()) {
		return;
	}
	const QString path = url.toLocalFile();
	const QFileInfo info(path);
	if (info.isDir()) {
		searchFolder(path);
		e->acceptProposedAction();
	} else if (info.suffix().compare("searchsite", Qt::CaseInsensitive) == 0 ||
			info.suffix().compare("xml", Qt::CaseInsensitive) == 0 ||
			info.suffix().compare("src", Qt::CaseInsensitive) == 0) {
		installSiteFile(path);
		e->acceptProposedAction();
	}
}

void BloodhoundWindow::searchFolder(const QString &path) {
	m_worker->stop();
	++m_generation;
	m_liveTimer.stop();
	m_searching = false;
	m_find.enabled = true;
	m_stop.enabled = false;
	m_channelName = "Files";
	m_folder = path;
	m_items.clear();
	m_snippets.clear();
	m_snippetQueue.clear();
	m_snippetRunning = false;
	m_contentWords.clear();
	m_similarTo.clear();
	m_problem.clear();
	m_status.clear();
	m_scrollY = 0;
	m_listFocus = false;
	rebuildScope(FindScope::Folder);
	relayout();
	update();
	if (!m_search.text.trimmed().isEmpty()) {
		search();
	}
}

void BloodhoundWindow::installSiteFile(const QString &path) {
	SearchSite site;
	QString error;
	if (!readSearchSiteFile(path, &site, &error)) {
		m_problem = error;
		update();
		return;
	}
	const QString folder = userSearchSitesFolder();
	const QString target = folder + "/" + QFileInfo(path).fileName();
	bool ok = QDir().mkpath(folder);
	if (ok && QFileInfo(path).absoluteFilePath() != QFileInfo(target).absoluteFilePath()) {
		if (QFileInfo::exists(target) && !QFile::remove(target)) {
			ok = false;
		}
		if (ok && !QFile::copy(path, target)) {
			ok = false;
		}
	}
	if (!ok) {
		m_problem = tr9("“%1” couldn't be installed in your Internet Search Sites folder.")
			.arg(site.name);
		update();
		return;
	}
	reloadSites();
	chooseChannel("Web");
	m_status = tr9("Installed “%1”.").arg(site.name);
	update();
}

int BloodhoundWindow::rowHeight() const {
	return m_contentWords.isEmpty() ? ROW_H : ROW_H + SNIPPET_H;
}

int BloodhoundWindow::listTop() const {
	return m_resultsTop + HEADER_H + BAND_H;
}

int BloodhoundWindow::paneTop() const {
	return H - STATUS_H - ((filesChannel() && m_previewOn) ? PREVIEW_H : 0);
}

int BloodhoundWindow::listHeight() const {
	return std::max(0, paneTop() - listTop());
}

pl_scrollbar BloodhoundWindow::bar() const {
	const int content = static_cast<int>(m_items.size()) * rowHeight();
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
	const int max = std::max(0, static_cast<int>(m_items.size()) * rowHeight() - listHeight());
	m_scrollY = std::clamp(y, 0, max);
	update();
}

Item *BloodhoundWindow::itemAt(QPoint pos) const {
	if (pos.y() < listTop() || pos.y() >= paneTop() || pos.x() >= W - SB_W + 1) {
		return nullptr;
	}
	const int row = (pos.y() - listTop() + m_scrollY) / rowHeight();
	return row >= 0 && row < static_cast<int>(m_items.size()) ? m_items[row].get() : nullptr;
}

void BloodhoundWindow::paintEvent(QPaintEvent *) {
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, FACE);

	/* The channel bar: Files, Web and the sites' own channels. */
	pl_fill(c, 0, 0, W - 1, CHANNEL_H - 1, GRAY(0xC));
	pl_hline(c, 0, W - 1, CHANNEL_H - 1, GRAY(0x8));
	for (const auto &[name, rect] : m_channelHits) {
		if (name == m_channelName) {
			pl_fill(c, rect.x(), rect.y(), rect.right(), rect.bottom(), C_WHITE);
			pl_hline(c, rect.x(), rect.right(), rect.y(), C_BLACK);
			pl_hline(c, rect.x(), rect.right(), rect.bottom(), C_BLACK);
			pl_vline(c, rect.x(), rect.y(), rect.bottom(), C_BLACK);
			pl_vline(c, rect.right(), rect.y(), rect.bottom(), C_BLACK);
		}
		Text label(name, rect.width() - 8, PL_FONT_SYSTEM);
		pl_text(c, label.t, rect.x() + 5, 19, C_BLACK);
	}

	/* The search field, scope or site, criteria rows and their buttons. */
	m_host.paintControls(c, FACE);
	if (!filesChannel()) {
		const int top = m_resultsTop, pt = paneTop();
		pl_fill(c, 0, top, W - 1, pt - 1, C_WHITE);
		const QList<SearchSite> sites = sitesForChannel(m_channelName);
		QString siteName;
		if (!sites.isEmpty()) {
			siteName = sites.value(std::clamp(m_site.selected, 0, int(sites.size()) - 1)).name;
		}
		QString header;
		if (!m_problem.isEmpty()) {
			header = m_problem;
		} else if (!m_status.isEmpty()) {
			header = m_status;
		} else if (!siteName.isEmpty()) {
			header = tr9("Search the web with %1").arg(siteName);
		} else {
			header = tr9("Search the web");
		}
		Text headerText(header, W - 40, PL_FONT_VIEWS);
		pl_text(c, headerText.t, (W - headerText.inkWidth()) / 2, top + 24,
			m_problem.isEmpty() ? C_BLACK : RGB(0xAA, 0x22, 0x22));
		Text hint(tr9("Type words and press Search. The results open in your browser."),
			W - 60, PL_FONT_VIEWS);
		pl_text(c, hint.t, (W - hint.inkWidth()) / 2, top + 54, GRAY(0x5));
		pl_hline(c, 0, W - 1, pt, C_BLACK);
		pl_fill(c, 0, pt + 1, W - 1, H - 1, GRAY(0xD));
		pl_hline(c, 0, W - 1, pt + 1, C_WHITE);
		Text where(siteName.isEmpty() ? m_channelName : siteName, W - 2 * MARGIN,
			PL_FONT_VIEWS);
		pl_text(c, where.t, MARGIN, pt + 15, C_BLACK);
		QPainter p(this);
		px.blit(p);
		return;
	}

	/* Results header. */
	if (hasContentsRow()) {
		Text note(tr9("Contents are searched in indexed folders; Index Selection adds one."),
			W - 2 * MARGIN, PL_FONT_VIEWS);
		const int noteY = m_resultsTop - 6;
		pl_text(c, note.t, MARGIN, noteY, GRAY(0x5));
	}
	pl_fill(c, 0, m_resultsTop, W - 1, m_resultsTop + HEADER_H - 1, FACE);
	pl_hline(c, 0, W - 1, m_resultsTop + HEADER_H - 1, GRAY(0xA));
	const int count = static_cast<int>(m_items.size());
	QString header;
	if (m_searching) {
		header = m_contentWords.isEmpty() ? tr9("Searching…  %1 found").arg(count)
			: tr9("Searching contents…  %1 found").arg(count);
	} else if (!m_problem.isEmpty()) {
		header = m_problem;
	} else if (!m_similarTo.isEmpty()) {
		header = count == 1 ? tr9("Similar to “%1” — 1 item").arg(m_similarTo)
			: tr9("Similar to “%1” — %2 items").arg(m_similarTo).arg(count);
	} else if (count == 0) {
		header = tr9("No items found");
	} else if (count == 1) {
		header = tr9("1 item found");
	} else {
		header = tr9("%1 items found").arg(count);
	}
	Text headerText(header, W - 20, PL_FONT_VIEWS);
	pl_text(c, headerText.t, (W - headerText.inkWidth()) / 2, m_resultsTop + 14,
		m_problem.isEmpty() ? C_BLACK : RGB(0xAA, 0x22, 0x22));

	/* Column titles, drawn like a folder window's unsorted columns. */
	const int bandY = m_resultsTop + HEADER_H;
	pl_canvas band = { c->px + bandY * c->stride, c->stride, 0, 0, W, BAND_H, c->log };
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
	const int top = listTop(), viewW = W - SB_W + 1, step = rowHeight();
	pl_fill(c, 0, top, viewW - 1, paneTop() - 1, C_WHITE);
	pl_canvas list = { c->px + top * c->stride, c->stride, 0, m_scrollY, viewW, listHeight(), c->log };
	const int first = m_scrollY / step;
	const int last = std::min<int>(static_cast<int>(m_items.size()) - 1,
		(m_scrollY + listHeight()) / step);
	for (int r = first; r <= last; r++) {
		Item *item = m_items[r].get();
		const int y = r * step;
		pl_hline(&list, 0, viewW - 1, y + step - 1, GRAY(0xE));
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
		if (!m_contentWords.isEmpty()) {
			const Snippet snippet = m_snippets.value(item->path);
			paintSnippet(&list, snippet, NAME_X, y + ROW_H + 10, viewW - NAME_X - 6);
		}
	}

	/* Bottom pane: the preview, then the selected path or item count. */
	const int pt = paneTop();
	const int st = H - STATUS_H;
	pl_scrollbar v = bar();
	pl_scrollbar_paint(c, W - SB_W + 1, top - 1, &v, pl_accent_current());
	if (m_listFocus) {
		pl_focus_ring_paint(c, 0, top - 1, W - SB_W, pt, pl_accent_current(), FACE);
	}
	if (m_previewOn) {
		paintPreview(c);
	}
	pl_hline(c, 0, W - 1, st, C_BLACK);
	pl_fill(c, 0, st + 1, W - 1, H - 1, GRAY(0xD));
	pl_hline(c, 0, W - 1, st + 1, C_WHITE);
	const std::vector<Item *> selected = selectedItems();
	const int statusW = W - 2 * MARGIN - SB_W - 88;
	if (selected.size() == 1) {
		Text where(selected.front()->path, statusW, PL_FONT_VIEWS);
		pl_text(c, where.t, MARGIN, st + 15, C_BLACK);
	} else if (count > 0) {
		Text items(count == 1 ? tr9("1 item") : tr9("%1 items").arg(count),
			statusW, PL_FONT_VIEWS);
		pl_text(c, items.t, MARGIN, st + 15, C_BLACK);
	}
	if (filesChannel()) {
		m_preview.paint(c);
	}

	QPainter p(this);
	px.blit(p);
}

void BloodhoundWindow::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const QPoint pos = e->position().toPoint();
	if (pos.y() < CHANNEL_H) {
		for (const auto &[name, rect] : m_channelHits) {
			if (rect.contains(pos)) {
				if (name != m_channelName) {
					chooseChannel(name);
				}
				return;
			}
		}
		return;
	}
	if (filesChannel() && m_preview.rect.contains(pos)) {
		m_previewOn = !m_previewOn;
		update();
		return;
	}
	if (m_host.hostPress(e)) {
		m_listFocus = false;
		update();
		return;
	}
	if (!filesChannel()) {
		return;
	}
	if (pos.x() >= W - SB_W + 1 && pos.y() >= listTop() - 1 && pos.y() < paneTop()) {
		pl_scrollbar v = bar();
		const int along = pos.y() - (listTop() - 1);
		m_sbPart = sb_hit(&v, along);
		const int page = listHeight() - rowHeight();
		switch (m_sbPart) {
		case SB_DEC_ARROW: scrollTo(m_scrollY - rowHeight()); break;
		case SB_INC_ARROW: scrollTo(m_scrollY + rowHeight()); break;
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
		const int content = static_cast<int>(m_items.size()) * rowHeight() - listHeight();
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
	if (e->button() == Qt::LeftButton && filesChannel()) {
		Item *hit = itemAt(e->position().toPoint());
		if (hit) {
			selectOnly(hit);
			openSelection();
		}
	}
}

void BloodhoundWindow::wheelEvent(QWheelEvent *e) {
	if (!filesChannel()) {
		return;
	}
	scrollTo(m_scrollY - e->angleDelta().y() / 120 * rowHeight() * 3);
}

void BloodhoundWindow::keyPressEvent(QKeyEvent *e) {
	const bool cmd = e->modifiers() & Qt::ControlModifier;
	const bool shift = e->modifiers() & Qt::ShiftModifier;
	const int key = e->key();
	if (cmd && key == Qt::Key_F && shift && m_listFocus) {
		findSimilar();
		return;
	}
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
		if (!filesChannel()) {
			m_host.setFocus(&m_search);
			update();
			return;
		}
		const int direction = key == Qt::Key_Backtab ? -1 : 1;
		std::vector<PanelEdit *> live;
		for (PanelEdit *edit : m_host.edits) {
			if (edit->enabled) {
				live.push_back(edit);
			}
		}
		if (m_listFocus) {
			m_listFocus = false;
			m_host.setFocus(direction > 0 ? &m_search
				: (live.empty() ? &m_search : live.back()));
		} else {
			auto it = std::find(live.begin(), live.end(), m_host.focus);
			const int at = it == live.end() ? -1 : int(it - live.begin());
			const int next = at + direction;
			if (next < 0 || next >= int(live.size())) {
				m_listFocus = true;
				m_host.setFocus(nullptr);
			} else {
				m_host.setFocus(live[next]);
			}
		}
		update();
		return;
	}
	if (key == Qt::Key_Escape) {
		if (m_worker->running() || m_searching) {
			stop();
		} else {
			m_search.setText(QString());
			m_items.clear();
			m_snippets.clear();
			m_snippetQueue.clear();
			m_snippetRunning = false;
			m_contentWords.clear();
			m_similarTo.clear();
			m_problem.clear();
			m_status.clear();
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
