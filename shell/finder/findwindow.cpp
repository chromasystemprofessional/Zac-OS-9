#include "findwindow.h"
#include "thumbnails.h"
#include "fileassoc.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QDate>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QWheelEvent>

#include "settings.h"
#include "appdb.h"
#include "infowindow.h"
#include "findindex.h"
#include "sharingclient.h"

static constexpr uint32_t FACE = GRAY(0xD);

/* ---- Find ------------------------------------------------------------------ */

static constexpr int DIALOG_W = 540;
static constexpr int M = 12;              /* window margin */
static constexpr int SCOPE_Y = 12;
static constexpr int ROWS_Y = 44, ROW_STEP = 30, MAX_ROWS = 6;
static constexpr int FIELD_W = 130, OP_W = 190, GAP = 6;
static constexpr int NOTE_H = 18;

/* The fields, in the menu's order. */
static const FindField FIELDS[] = { FindField::Name, FindField::Contents, FindField::Modified,
	FindField::Created, FindField::Size, FindField::Kind, FindField::Label, FindField::Locked,
	FindField::Folder, FindField::CustomIcon, FindField::Comment, FindField::Invisible,
	FindField::Version };

struct FindDialog::Row {
	FindField field = FindField::Name;
	PanelPopup fieldPopup, opPopup, choicePopup;
	PanelEdit edit;
	FindOp op() const {
		const QList<FindOp> ops = findOpsFor(field);
		return ops.value(std::clamp(opPopup.selected, 0, int(ops.size()) - 1));
	}
};

static QPointer<FindDialog> &findDialog() {
	static QPointer<FindDialog> dialog;
	return dialog;
}

void FindDialog::open(const QString &folder) {
	if (!findDialog()) {
		findDialog() = new FindDialog(folder);
	}
	findDialog()->show();
	findDialog()->raise();
	findDialog()->activateWindow();
}

FindDialog::~FindDialog() = default;

FindDialog::FindDialog(const QString &folder) : m_folder(folder) {
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle("Find");
	m_scope.label = "Find items";
	m_scope.items = { PopupItem("on all disks"), PopupItem("in my Home folder") };
	if (!folder.isEmpty() && QDir(folder) != QDir::home() && QDir(folder) != QDir::root()) {
		m_scope.items.push_back(PopupItem(QString("in “%1”").arg(displayName(folder))));
		m_scope.selected = 2;
	}
	m_scope.chosen = [this](int i) { m_scope.selected = i; update(); };
	// Sized here: a button lays out its label when it's made; relayout() moves them.
	m_more = PanelButton("More Choices", QRect(0, 0, 110, PL_BUTTON_H), false);
	m_more.clicked = [this] {
		addRow(FindField::Modified);
		relayout();
	};
	m_fewer = PanelButton("Fewer Choices", QRect(0, 0, 110, PL_BUTTON_H), false);
	m_fewer.clicked = [this] {
		if (m_rows.size() > 1) {
			m_rows.pop_back();
			relayout();
		}
	};
	m_find = PanelButton("Find", QRect(0, 0, PL_BUTTON_MIN_W, PL_BUTTON_H), true);
	m_find.clicked = [this] { find(); };
	m_saveSearch = PanelButton("Save Search…", QRect(0, 0, 100, PL_BUTTON_H), false);
	m_saveSearch.clicked = [this] {
		bool accepted = false;
		const QString name = QInputDialog::getText(this, "Save Find Search",
			"Name:", QLineEdit::Normal, QString(), &accepted);
		if (!accepted) {
			return;
		}
		QString why;
		if (!saveFindQuery(name, query(), &why)) {
			m_problem = why;
			relayout();
			return;
		}
		m_problem.clear();
		relayout();
	};
	m_savedSearches = PanelButton("Saved Searches…", QRect(0, 0, 115, PL_BUTTON_H), false);
	m_savedSearches.clicked = [this] {
		QString why;
		const QStringList names = savedFindQueryNames(&why);
		if (!why.isEmpty()) {
			m_problem = why;
			relayout();
			return;
		}
		if (names.isEmpty()) {
			m_problem = "No saved Find searches yet.";
			relayout();
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
					relayout();
					return;
				}
				FoundWindow::search(saved);
				close();
			});
		}
		menu.exec(mapToGlobal(m_savedSearches.rect.bottomLeft()));
	};
	m_host.defaultButton = &m_find;
	addRow(FindField::Name);
	relayout();
	m_host.setFocus(&m_rows.front()->edit);
}

void FindDialog::addRow(FindField field) {
	auto row = std::make_unique<Row>();
	Row *r = row.get();
	for (FindField f : FIELDS) {
		r->fieldPopup.items.push_back(PopupItem(findFieldName(f)));
	}
	r->fieldPopup.chosen = [this, r](int i) {
		setField(*r, FIELDS[i]);
		relayout();
	};
	r->opPopup.chosen = [this, r](int i) {
		r->opPopup.selected = i;
		relayout();
	};
	r->choicePopup.chosen = [this, r](int i) {
		r->choicePopup.selected = i;
		update();
	};
	r->edit.edited = [this] { m_problem.clear(); update(); };
	setField(*r, field);
	m_rows.push_back(std::move(row));
}

void FindDialog::setField(Row &row, FindField field) {
	row.field = field;
	row.fieldPopup.selected = int(std::find(std::begin(FIELDS), std::end(FIELDS), field) - std::begin(FIELDS));
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
			PopupItem item(QString::fromUtf8(pl_labels[i].name));
			row.choicePopup.items.push_back(item);
		}
	}
	// A date to start from: today, which the user changes.
	row.edit.setText(field == FindField::Modified || field == FindField::Created
		? QDate::currentDate().toString(Qt::ISODate) : QString());
	row.edit.accepts = field == FindField::Size ? std::function<bool(QChar)>([](QChar c) { return c.isDigit(); })
		: std::function<bool(QChar)>();
}

void FindDialog::relayout() {
	const int n = int(m_rows.size());
	bool contents = false;
	m_host.popups = { &m_scope };
	m_host.edits.clear();
	int y = ROWS_Y;
	PanelEdit *firstEdit = nullptr;
	for (auto &row : m_rows) {
		row->fieldPopup.rect = QRect(M, y, FIELD_W, PL_POPUP_H);
		row->opPopup.rect = QRect(M + FIELD_W + GAP, y, OP_W, PL_POPUP_H);
		const int vx = M + FIELD_W + GAP + OP_W + GAP;
		row->choicePopup.rect = QRect(vx, y, DIALOG_W - M - vx, PL_POPUP_H);
		row->edit.rect = QRect(vx, y - 1, DIALOG_W - M - vx, PL_EDIT_H);
		m_host.popups.push_back(&row->fieldPopup);
		m_host.popups.push_back(&row->opPopup);
		if (row->field == FindField::Kind || row->field == FindField::Label) {
			m_host.popups.push_back(&row->choicePopup);
		} else if (findOpTakesText(row->field, row->op())) {
			m_host.edits.push_back(&row->edit);
			firstEdit = firstEdit ? firstEdit : &row->edit;
		}
		contents = contents || row->field == FindField::Contents;
		y += ROW_STEP;
	}
	const int bottom = y + (contents ? NOTE_H : 0) + 8;
	m_scope.rect = QRect(M + 80, SCOPE_Y, 220, PL_POPUP_H);
	m_more.rect.moveTo(M, bottom);
	m_fewer.rect.moveTo(M + m_more.rect.width() + 10, bottom);
	m_saveSearch.rect.moveTo(m_fewer.rect.right() + 10, bottom);
	m_savedSearches.rect.moveTo(m_saveSearch.rect.right() + 10, bottom);
	m_find.rect.moveTo(DIALOG_W - M - 4 - m_find.rect.width(), bottom);
	m_more.enabled = n < MAX_ROWS;
	m_fewer.enabled = n > 1;
	m_host.buttons = { &m_more, &m_fewer, &m_saveSearch, &m_savedSearches, &m_find };
	if (std::find(m_host.edits.begin(), m_host.edits.end(), m_host.focus) == m_host.edits.end()) {
		m_host.setFocus(firstEdit);
	}
	setFixedSize(DIALOG_W, bottom + PL_BUTTON_H + M + (m_problem.isEmpty() ? 0 : NOTE_H));
	update();
}

FindQuery FindDialog::query() const {
	FindQuery q;
	q.scope = m_scope.selected == 2 ? FindScope::Folder : m_scope.selected == 1 ? FindScope::Home : FindScope::AllDisks;
	q.folder = m_folder;
	for (const auto &row : m_rows) {
		FindCriterion c;
		c.field = row->field;
		c.op = row->op();
		c.text = row->edit.text;
		c.choice = row->choicePopup.selected;
		q.criteria << c;
	}
	return q;
}

void FindDialog::find() {
	FindQuery q = query();
	QString why;
	if (!q.ready(&why)) {
		m_problem = why;
		relayout();
		return;
	}
	FoundWindow::search(q);
	close();
}

void FindDialog::paintEvent(QPaintEvent *) {
	const int W = width(), H = height();
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, FACE);
	Text whose("whose", 80, PL_FONT_SYSTEM);
	pl_text(c, whose.t, m_scope.rect.right() + 8, m_scope.rect.y() + PL_POPUP_BASELINE(PL_POPUP_H), C_BLACK);
	bool contents = false;
	for (const auto &row : m_rows) {
		contents = contents || row->field == FindField::Contents;
	}
	const int notesY = ROWS_Y + int(m_rows.size()) * ROW_STEP;
	if (contents) {
		Text note("Contents are searched in indexed folders; File ▸ Index Selection adds one.",
			W - 2 * M, PL_FONT_VIEWS);
		pl_text(c, note.t, M, notesY + 4, GRAY(0x5));
	}
	if (!m_problem.isEmpty()) {
		Text problem(m_problem, W - 2 * M, PL_FONT_VIEWS);
		pl_text(c, problem.t, M, H - 8, RGB(0xAA, 0x22, 0x22));
	}
	m_host.paintControls(c, FACE);
	QPainter p(this);
	px.blit(p);
}

void FindDialog::keyPressEvent(QKeyEvent *e) {
	const bool cmd = e->modifiers() & Qt::ControlModifier;
	if (e->key() == Qt::Key_Escape || (cmd && (e->key() == Qt::Key_W || e->key() == Qt::Key_Period))) {
		close();
		return;
	}
	m_host.hostKey(e);
	update();
}

void FindDialog::mousePressEvent(QMouseEvent *e) {
	m_host.hostPress(e);
	update();
}

void FindDialog::mouseMoveEvent(QMouseEvent *e) {
	m_host.hostMove(e);
	update();
}

void FindDialog::mouseReleaseEvent(QMouseEvent *e) {
	m_host.hostRelease(e);
	update();
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

void FoundWindow::search(const FindQuery &query) {
	FindQuery prepared = query;
	for (const SharedFolder &folder : sharedFolders()) {
		const QString path = QFileInfo(folder.path).canonicalFilePath();
		if (!path.isEmpty() && !prepared.sharedFolders.contains(path)) {
			prepared.sharedFolders << path;
		}
	}
	if (prepared.hasVersionCriterion()) {
		for (const AppEntry &app : appList()) {
			const QString path = QFileInfo(app.file).canonicalFilePath();
			if (!path.isEmpty() && !app.version.isEmpty()) {
				prepared.applicationVersions.insert(path, app.version);
			}
		}
	}
	auto *w = new FoundWindow("whose " + query.describe());
	foundWindows().push_back(w);
	w->show();
	Finder::instance().setFront(w);
	if (!prepared.hasContents()) {
		w->walk(prepared, {}, false);
		return;
	}
	// Contents come from the index; the other rows sift what it finds.
	QPointer<FoundWindow> self(w);
	indexSearch(prepared.contentWords(), prepared.root(),
			[self, prepared](const QStringList &paths, const QString &error) {
		if (!self) {
			return;
		}
		if (!error.isEmpty()) {
			self->m_problem = error;
			self->finished();
			return;
		}
		self->walk(prepared, paths, true);
	});
}

void FoundWindow::similarTo(const QString &path) {
	auto *w = new FoundWindow(QString("similar to “%1”").arg(displayName(path)));
	foundWindows().push_back(w);
	w->show();
	Finder::instance().setFront(w);
	QPointer<FoundWindow> self(w);
	indexSimilar(path, [self](const QStringList &paths, const QString &error) {
		if (!self) {
			return;
		}
		self->m_problem = error;
		QStringList present;
		for (const QString &p : paths) {
			if (QFileInfo::exists(p)) {
				present << p;
			}
		}
		self->addResults(present);
		self->finished();
	});
}

std::vector<QPointer<FoundWindow>> &FoundWindow::windows() {
	return foundWindows();
}

void FoundWindow::reloadAll() {
	for (auto &w : foundWindows()) {
		if (w) {
			w->reload();
		}
	}
}

FoundWindow::FoundWindow(const QString &title) : m_title(title) {
	watchThumbnails(this);
	watchFileAssociations(this);
	setAttribute(Qt::WA_DeleteOnClose);
	resize(560, 320);
	setMinimumSize(300, 160);
	updateTitle();
}

/* Walks the disk from the query's root, or goes through the index's
 * candidates, off the main thread, handing matches over in batches. */
void FoundWindow::walk(const FindQuery &query, const QStringList &candidates, bool useCandidates) {
	QPointer<FoundWindow> self(this);
	m_worker = QThread::create([self, query, candidates, useCandidates] {
		QStringList batch;
		int found = 0;
		QElapsedTimer sent;
		sent.start();
		const QDate today = QDate::currentDate();
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
		auto consider = [&](const QFileInfo &info) {
			if (found < MAX_RESULTS && findMatches(query, info, today)) {
				batch << info.absoluteFilePath();
				found++;
			}
			if (batch.size() >= 200 || sent.elapsed() > 250) {
				flush();
			}
		};
		if (useCandidates) {
			for (const QString &path : candidates) {
				if (QThread::currentThread()->isInterruptionRequested()) {
					break;
				}
				const QFileInfo info(path);
				if (info.exists()) {
					consider(info);
				}
			}
		} else {
			QStringList stack = { query.root() };
			while (!stack.isEmpty() && found < MAX_RESULTS &&
					!QThread::currentThread()->isInterruptionRequested()) {
				const QString dir = stack.takeLast();
				QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;
				if (query.hasVisibilityCriterion()) {
					filters |= QDir::Hidden;
				}
				const auto entries = QDir(dir).entryInfoList(
					filters,
					QDir::Name | QDir::IgnoreCase | QDir::Reversed);
				for (const QFileInfo &info : entries) {
					const QString path = info.absoluteFilePath();
					if (info.isDir() && !info.isSymLink() && !skipDir(path)) {
						stack.push_back(path);
					}
					consider(info);
				}
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
	setToolTip(m_title);
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
	Text header(m_searching ? "Searching…  " + count : !m_problem.isEmpty() && m_items.empty() ? m_problem
		: count + " found " + m_title, W - 20, PL_FONT_VIEWS);
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
		paintIcon(&list, *item, ICON_X, y + 1, PL_ICON_SMALL, item->selected);
		Text name(item->visibleName(), NAME_W - NAME_X - 6, item->nameFont());
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
