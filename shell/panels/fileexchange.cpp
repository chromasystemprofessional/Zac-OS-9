#include "fileexchange.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include "alert.h"
#include "platinumshell.h"

static constexpr uint32_t FACE = GRAY(0xD);

/* ---- the Open With dialog ---------------------------------------------- */

static constexpr int OW_W = 320, OW_H = 270;

OpenWithDialog::OpenWithDialog(const QString &kind, const QStringList &appIds,
		const QString &current)
	: m_kind(kind), m_ids(appIds) {
	setWindowTitle("Open With");
	setFixedSize(OW_W, OW_H);
	m_list.frame = QRect(QPoint(13, 33), QPoint(OW_W - 14, 33 + 11 * PL_LIST_ROW_H + 1));
	QStringList names;
	std::vector<int> icons;
	for (const QString &id : m_ids) {
		const QString name = appDisplayName(id);
		names << (name.isEmpty() ? id : name);
		icons.push_back(PL_ICON_APPLICATION);
	}
	m_list.setItems(names, icons);
	m_list.select(std::max<int>(0, m_ids.indexOf(current)), false);
	m_list.ensureVisible(m_list.state.selected);
	m_ok = PanelButton("OK", QRect(OW_W - 13 - 58 - 3, OW_H - 13 - 20 - 3, 58, 20), true);
	m_ok.clicked = [this] { accept(); };
	m_cancel = PanelButton("Cancel", QRect(m_ok.rect.x() - 12 - 3 - 58, m_ok.rect.y(), 58, 20));
	m_cancel.clicked = [this] { reject(); };
}

QString OpenWithDialog::chosen() const {
	return m_ids.value(m_list.state.selected);
}

void OpenWithDialog::showEvent(QShowEvent *e) {
	QDialog::showEvent(e);
	platinumSetFrameStyle(this, FrameStyle::MovableModal);
}

void OpenWithDialog::paintEvent(QPaintEvent *) {
	Pixels px(OW_W, OW_H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, OW_W - 1, OW_H - 1, FACE);
	panelText(c, QStringLiteral("Open “%1” documents with:").arg(m_kind), 13, 22,
		PL_FONT_SYSTEM, C_BLACK, OW_W - 26);
	m_list.paint(c, true);
	m_cancel.paint(c);
	m_ok.paint(c);
	QPainter p(this);
	px.blit(p);
}

void OpenWithDialog::mousePressEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (m_ok.press(pos) || m_cancel.press(pos) || m_list.press(pos)) {
		update();
	}
}

void OpenWithDialog::mouseMoveEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (m_ok.move(pos) || m_cancel.move(pos) || m_list.move(pos)) {
		update();
	}
}

void OpenWithDialog::mouseReleaseEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (m_list.release() | m_ok.release(pos) || m_cancel.release(pos)) {
		update();
	}
}

void OpenWithDialog::wheelEvent(QWheelEvent *e) {
	if (m_list.wheel(e->position().toPoint(), e->angleDelta().y())) {
		update();
	}
}

void OpenWithDialog::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		accept();
	} else if (e->key() == Qt::Key_Escape ||
			((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_Period)) {
		reject();
	} else if (m_list.key(e->key(), e->text())) {
		update();
	}
}

/* ---- the panel ---------------------------------------------------------- */

static constexpr int W = 440, H = 344, M = 12;
static constexpr int LIST_TOP = 30, LIST_ROWS = 12;
static constexpr int LIST_BOTTOM = LIST_TOP + LIST_ROWS * PL_LIST_ROW_H + 1;
static constexpr int GROUP_TOP = LIST_BOTTOM + 14, GROUP_BOTTOM = H - M - 2;
static constexpr int LABEL_RIGHT = M + 92, VALUE_X = LABEL_RIGHT + 8;

FileExchangePanel::FileExchangePanel() {
	setWindowTitle("File Exchange");
	setFixedSize(W, H);
	m_list.frame = QRect(QPoint(M + 1, LIST_TOP), QPoint(W - M - 2, LIST_BOTTOM));
	m_list.picked = [this](int) { showDetails(); };
	m_change = PanelButton("Change…", QRect(W - M - 14 - 80, GROUP_BOTTOM - 13 - 20, 80, 20));
	m_change.clicked = [this] { change(); };
	/* Another program (or the Finder) changed an association. */
	fileAssocOnChange([this] {
		showDetails();
		update();
	});
	reload();
}

void FileExchangePanel::reload() {
	const QString selected = m_types.empty() || m_list.state.selected < 0
		? QString() : m_types[static_cast<size_t>(m_list.state.selected)].mimeType;
	m_types = openableFileTypes();
	QStringList rows;
	int row = 0;
	for (size_t i = 0; i < m_types.size(); i++) {
		rows << m_types[i].description;
		if (m_types[i].mimeType == selected) {
			row = static_cast<int>(i);
		}
	}
	m_list.setItems(rows);
	if (!m_types.empty()) {
		m_list.select(row, false);
		m_list.ensureVisible(row);
	}
	showDetails();
}

void FileExchangePanel::showDetails() {
	const int row = m_list.state.selected;
	m_appId.clear();
	m_appName.clear();
	m_appIcon.clear();
	if (row >= 0 && static_cast<size_t>(row) < m_types.size()) {
		m_appId = defaultAppFor(m_types[static_cast<size_t>(row)].mimeType);
		if (!m_appId.isEmpty()) {
			m_appName = appDisplayName(m_appId);
			m_appIcon = appIconPixels(m_appId, 16);
		}
	}
	m_change.enabled = row >= 0 && static_cast<size_t>(row) < m_types.size();
}

void FileExchangePanel::change() {
	const int row = m_list.state.selected;
	if (row < 0 || static_cast<size_t>(row) >= m_types.size()) {
		return;
	}
	const FileType &type = m_types[static_cast<size_t>(row)];
	const QStringList ids = appsFor(type.mimeType);
	if (ids.isEmpty()) {
		Alert::ask("No installed application can open this kind of document.", "OK", QString());
		return;
	}
	OpenWithDialog dialog(type.description, ids, m_appId);
	if (dialog.exec() != QDialog::Accepted || dialog.chosen().isEmpty() ||
			dialog.chosen() == m_appId) {
		return;
	}
	QString error;
	if (!setDefaultApp(type.mimeType, dialog.chosen(), &error)) {
		Alert::ask(QStringLiteral("The application for “%1” documents couldn't be changed. %2")
			.arg(type.description, error), "OK", QString());
	}
	showDetails();
	update();
}

void FileExchangePanel::paintEvent(QPaintEvent *) {
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, FACE);
	panelText(c, m_types.empty()
		? QStringLiteral("No installed application says which documents it opens.")
		: QStringLiteral("Kinds of documents your applications can open:"),
		M + 1, 22, PL_FONT_SYSTEM, C_BLACK, W - 2 * M);
	m_list.paint(c, true);

	panelGroup(c, M, GROUP_TOP, W - M - 2, GROUP_BOTTOM, "Selected Kind", FACE);
	const int row = m_list.state.selected;
	if (row >= 0 && static_cast<size_t>(row) < m_types.size()) {
		const FileType &type = m_types[static_cast<size_t>(row)];
		const int line1 = GROUP_TOP + 26, line2 = line1 + 24;
		panelLabel(c, "Extensions:", LABEL_RIGHT, line1);
		QStringList exts;
		for (const QString &e : type.extensions) {
			exts << "." + e;
		}
		panelText(c, exts.isEmpty() ? QStringLiteral("none") : exts.join("  "), VALUE_X, line1,
			PL_FONT_SYSTEM, C_BLACK, W - M - 14 - VALUE_X);
		panelLabel(c, "Opens with:", LABEL_RIGHT, line2);
		int x = VALUE_X;
		if (m_appIcon.size() == 16 * 16) {
			pl_image_blend(c, x, line2 - 12, m_appIcon.data(), 16, 16, false);
			x += 20;
		}
		panelText(c, m_appId.isEmpty() ? QStringLiteral("no application")
			: (m_appName.isEmpty() ? m_appId : m_appName), x, line2, PL_FONT_SYSTEM, C_BLACK,
			m_change.rect.x() - 10 - x);
	}
	m_change.paint(c);
	QPainter p(this);
	px.blit(p);
}

void FileExchangePanel::mousePressEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (m_change.press(pos) || m_list.press(pos)) {
		update();
	}
}

void FileExchangePanel::mouseMoveEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (m_change.move(pos) || m_list.move(pos)) {
		update();
	}
}

void FileExchangePanel::mouseReleaseEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (m_list.release() | m_change.release(pos)) {
		update();
	}
}

void FileExchangePanel::wheelEvent(QWheelEvent *e) {
	if (m_list.wheel(e->position().toPoint(), e->angleDelta().y())) {
		update();
	}
}

void FileExchangePanel::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		change();
	} else if (m_list.key(e->key(), e->text())) {
		showDetails();
		update();
	}
}
