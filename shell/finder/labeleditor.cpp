#include "labeleditor.h"
#include "alias.h"

#include "vfs.h"
#include "apptrash.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>

#include "alert.h"
#include "finder.h"

static constexpr int LABEL_GAP = 2;       /* as items.cpp */
static constexpr int BOX_H = 13;
static constexpr int BOX_PAD = 2;
static constexpr int BASELINE = 10;
static constexpr uint32_t TEXT_HILITE = RGB(0xCC, 0xCC, 0xFF); /* lavender */

LabelEditor::LabelEditor(std::function<void()> repaint) : m_repaint(std::move(repaint)) {
	m_blink.callOnTimeout([this] {
		m_caretOn = !m_caretOn;
		m_repaint();
	});
}

void LabelEditor::begin(Item *item) {
	if (appTrashMarker(item->path)) {
		return;
	}
	if (item->isVirtual && !vfsCanRename(item->path)) {
		Alert::ask("This system resource is read-only and can't be renamed or labelled.",
			"OK", QString());
		return;
	}
	m_item = item;
	m_text = item->name;
	m_anchor = 0;
	m_cursor = m_text.size(); /* the whole name selected */
	m_caretOn = true;
	m_blink.start(QApplication::cursorFlashTime() / 2);
	m_repaint();
}

void LabelEditor::retarget(const std::vector<std::unique_ptr<Item>> &items) {
	if (!m_item) {
		return;
	}
	const QString name = m_item->name; /* still valid: called before the old list dies */
	m_item = nullptr;
	for (auto &item : items) {
		if (item->name == name) {
			m_item = item.get();
		}
	}
	if (!m_item) {
		m_blink.stop();
	}
}

void LabelEditor::changed() {
	m_caretOn = true;
	m_blink.start();
	m_repaint();
}

int LabelEditor::advanceTo(int chars) const {
	if (chars <= 0) {
		return 0;
	}
	Text t(m_text.left(chars), 100000, PL_FONT_VIEWS);
	return t.t ? t.t->advance : 0;
}

void LabelEditor::deleteSelection() {
	int a = std::min(m_cursor, m_anchor), b = std::max(m_cursor, m_anchor);
	m_text.remove(a, b - a);
	m_cursor = m_anchor = a;
}

bool LabelEditor::key(QKeyEvent *e) {
	if (!m_item) {
		return false;
	}
	const bool shift = e->modifiers() & Qt::ShiftModifier;
	switch (e->key()) {
	case Qt::Key_Return:
	case Qt::Key_Enter:
		commit();
		return true;
	case Qt::Key_Escape:
		cancel();
		return true;
	case Qt::Key_Backspace:
		if (m_cursor == m_anchor && m_cursor > 0) {
			m_anchor = m_cursor - 1;
		}
		deleteSelection();
		changed();
		return true;
	case Qt::Key_Delete:
		if (m_cursor == m_anchor && m_cursor < m_text.size()) {
			m_anchor = m_cursor + 1;
		}
		deleteSelection();
		changed();
		return true;
	case Qt::Key_Left:
		m_cursor = std::max(0, m_cursor - 1);
		if (!shift) {
			m_anchor = m_cursor;
		}
		changed();
		return true;
	case Qt::Key_Right:
		m_cursor = std::min<int>(m_text.size(), m_cursor + 1);
		if (!shift) {
			m_anchor = m_cursor;
		}
		changed();
		return true;
	case Qt::Key_Home:
	case Qt::Key_Up:
		m_cursor = 0;
		if (!shift) {
			m_anchor = 0;
		}
		changed();
		return true;
	case Qt::Key_End:
	case Qt::Key_Down:
		m_cursor = m_text.size();
		if (!shift) {
			m_anchor = m_cursor;
		}
		changed();
		return true;
	default:
		break;
	}
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_A) {
		m_anchor = 0;
		m_cursor = m_text.size();
		changed();
		return true;
	}
	const QString typed = e->text();
	if (!typed.isEmpty() && typed.at(0).isPrint() && !(e->modifiers() & Qt::ControlModifier)) {
		/* "/" can't be in a Linux file name (on the Mac it was ":"). */
		if (typed.contains('/')) {
			QApplication::beep();
			return true;
		}
		deleteSelection();
		m_text.insert(m_cursor, typed);
		m_cursor += typed.size();
		m_anchor = m_cursor;
		changed();
		return true;
	}
	return true; /* everything else is swallowed while editing */
}

void LabelEditor::cancel() {
	m_item = nullptr;
	m_blink.stop();
	m_repaint();
}

void LabelEditor::commit() {
	Item *item = m_item;
	if (!item) {
		return;
	}
	const QString name = m_text.trimmed();
	m_item = nullptr;
	m_blink.stop();
	m_repaint();
	if (name.isEmpty() || name == item->name) {
		return;
	}
	if (item->isVirtual) {
		/* A virtual rename is metadata: the registry keeps it, and the
		 * item's id (and so its icon's place) doesn't change. */
		if (vfsNameTaken(item->path, name)) {
			Alert::ask("That name is already taken. Please use a different name.",
				"OK", "");
			return;
		}
		if (!vfsRename(item->path, name)) {
			Alert::ask(QStringLiteral("The item “%1” can't be renamed.")
				.arg(item->name), "OK", "");
			return;
		}
		if (FinderView *v = Finder::instance().front()) {
			v->reload();
			v->selectByName(name);
		}
		return;
	}
	const QFileInfo info(item->path);
	const QDir dir = info.dir();
	if (dir.exists(name)) {
		Alert::ask("That name is already taken. Please use a different name.", "OK", "");
		return;
	}
	const QString folder = dir.absolutePath();
	if (!QDir().rename(item->path, dir.filePath(name))) {
		Alert::ask(QStringLiteral("The item “%1” could not be renamed.").arg(item->name),
			"OK", "");
		return;
	}
	aliasMoveRecord(item->path, dir.filePath(name), false);
	Finder &finder = Finder::instance();
	if (FinderView *v = finder.front()) {
		v->itemRenamed(item->name, name);
	}
	finder.folderChanged(folder);
	if (FinderView *v = finder.front()) {
		v->selectByName(name);
	}
}

/* The box is centred under the icon and sized to the text being typed. */
void LabelEditor::paint(pl_canvas *c, int x, int y) const {
	const int width = advanceTo(m_text.size());
	const int pen = x + PL_ICON_LARGE / 2 - width / 2;
	const int top = y + PL_ICON_LARGE + LABEL_GAP;
	pl_fill(c, pen - BOX_PAD - 1, top, pen + width + BOX_PAD, top + BOX_H - 1, C_WHITE);
	pl_outline(c, pen - BOX_PAD - 1, top, pen + width + BOX_PAD, top + BOX_H - 1, C_BLACK);

	int a = std::min(m_cursor, m_anchor), b = std::max(m_cursor, m_anchor);
	if (a != b) {
		pl_fill(c, pen + advanceTo(a), top + 1, pen + advanceTo(b) - 1, top + BOX_H - 2,
			TEXT_HILITE);
	}
	Text t(m_text, 100000, PL_FONT_VIEWS);
	if (t.t && t.t->ink_l >= 0) {
		/* Text is rendered with its pen at mask column 1. */
		pl_text(c, t.t, pen + t.t->ink_l - 1, top + BASELINE, C_BLACK);
	}
	if (a == b && m_caretOn) {
		pl_vline(c, pen + advanceTo(m_cursor), top + 2, top + BOX_H - 3, C_BLACK);
	}
}

bool LabelEditor::contains(int x, int y, QPoint p) const {
	const int width = advanceTo(m_text.size());
	const int pen = x + PL_ICON_LARGE / 2 - width / 2;
	const int top = y + PL_ICON_LARGE + LABEL_GAP;
	return QRect(QPoint(pen - BOX_PAD - 1, top), QPoint(pen + width + BOX_PAD, top + BOX_H - 1))
		.contains(p);
}
