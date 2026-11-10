#include "labeleditor.h"
#include "tr.h"
#include "alias.h"
#include "fileassoc.h"

#include "vfs.h"
#include "apptrash.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLocale>
#include <algorithm>

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
		Alert::ask(tr9("This system resource is read-only and can't be renamed or labelled."),
			"OK", QString());
		return;
	}
	m_item = item;
	m_text = item->visibleName();
	m_anchor = 0;
	m_cursor = m_text.size(); /* the whole name selected */
	m_preedit.clear();
	m_preeditCursor = 0;
	m_caretOn = true;
	m_blink.start(QApplication::cursorFlashTime() / 2);
	reportEditing();
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
	reportEditing();
}

void LabelEditor::reportEditing() {
	const bool now = m_item != nullptr;
	if (now == m_editingReported) {
		return;
	}
	m_editingReported = now;
	if (m_editingChanged) {
		m_editingChanged(now);
	}
}

void LabelEditor::changed() {
	m_caretOn = true;
	m_blink.start();
	m_repaint();
}

/* The committed text with the input method's preedit inserted at the
 * caret. Indices below are UTF-16, as Qt's are. */
QString LabelEditor::displayed() const {
	if (m_preedit.isEmpty()) {
		return m_text;
	}
	const int at = std::clamp(m_cursor, 0, static_cast<int>(m_text.size()));
	return m_text.left(at) + m_preedit + m_text.mid(at);
}

int LabelEditor::advanceTo(int chars) const {
	if (chars <= 0) {
		return 0;
	}
	Text t(displayed().left(chars), 100000, PL_FONT_VIEWS);
	return t.t ? t.t->advance : 0;
}

void LabelEditor::deleteSelection() {
	int a = std::min(m_cursor, m_anchor), b = std::max(m_cursor, m_anchor);
	m_text.remove(a, b - a);
	m_cursor = m_anchor = a;
}

/* Insert typed or pasted text: printable characters, less "/" (which can't
 * be in a Linux file name; on the Mac it was ":"). A commit can carry a
 * whole word at once. */
void LabelEditor::insertText(const QString &raw) {
	QString s;
	bool slash = false;
	for (QChar ch : raw) {
		if (ch == '/') {
			slash = true;
		} else if (ch.isPrint()) {
			s += ch;
		}
	}
	if (slash) {
		QApplication::beep();
	}
	if (s.isEmpty()) {
		return;
	}
	deleteSelection();
	m_text.insert(m_cursor, s);
	m_cursor += s.size();
	m_anchor = m_cursor;
	clearPreedit();
	changed();
}

void LabelEditor::setPreedit(const QString &text, int cursor) {
	m_preedit = text;
	m_preeditCursor = std::clamp(cursor, 0, static_cast<int>(m_preedit.size()));
	changed();
}

void LabelEditor::clearPreedit() {
	m_preedit.clear();
	m_preeditCursor = 0;
}

bool LabelEditor::inputMethodEvent(QInputMethodEvent *e) {
	if (!m_item) {
		return false;
	}
	int cursor = static_cast<int>(e->preeditString().size());
	for (const QInputMethodEvent::Attribute &a : e->attributes()) {
		if (a.type == QInputMethodEvent::Cursor) {
			cursor = a.start;
		}
	}
	const QString commit = e->commitString();
	if (!commit.isEmpty()) {
		insertText(commit);
	}
	setPreedit(e->preeditString(), cursor);
	return true;
}

QRect LabelEditor::boxRect(QPoint iconPos) const {
	const int width = advanceTo(displayed().size());
	int pen, top;
	origin(iconPos.x(), iconPos.y(), width, &pen, &top);
	return QRect(QPoint(pen - BOX_PAD - 1, top), QPoint(pen + width + BOX_PAD, top + BOX_H - 1));
}

QRect LabelEditor::cursorRect(QPoint iconPos) const {
	const int width = advanceTo(displayed().size());
	int pen, top;
	origin(iconPos.x(), iconPos.y(), width, &pen, &top);
	const int caret = m_preedit.isEmpty() ? m_cursor :
		m_cursor + std::clamp(m_preeditCursor, 0, static_cast<int>(m_preedit.size()));
	return QRect(pen + advanceTo(caret), top + 2, 1, BOX_H - 4);
}

QVariant LabelEditor::inputMethodQuery(Qt::InputMethodQuery query, QPoint iconPos) const {
	if (!m_item) {
		return {};
	}
	switch (query) {
	case Qt::ImEnabled:
		return true;
	case Qt::ImHints:
		return static_cast<int>(Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase);
	case Qt::ImSurroundingText:
		return m_text;
	case Qt::ImCursorPosition:
		return m_cursor;
	case Qt::ImAnchorPosition:
		return m_anchor;
	case Qt::ImCurrentSelection: {
		const int a = std::min(m_cursor, m_anchor), b = std::max(m_cursor, m_anchor);
		return m_text.mid(a, b - a);
	}
	case Qt::ImTextBeforeCursor:
		return m_text.left(m_cursor);
	case Qt::ImTextAfterCursor:
		return m_text.mid(m_cursor);
	case Qt::ImCursorRectangle:
		return cursorRect(iconPos);
	case Qt::ImInputItemClipRectangle:
		return boxRect(iconPos);
	case Qt::ImReadOnly:
		return false;
	case Qt::ImPreferredLanguage:
		return QLocale().name();
	default:
		return {};
	}
}

bool LabelEditor::key(QKeyEvent *e) {
	if (!m_item) {
		return false;
	}
	/* While the input method is composing, the keys belong to it. The
	 * compositor normally keeps them from reaching us; this keeps a stray
	 * one from editing the committed text behind the preedit. */
	if (!m_preedit.isEmpty()) {
		if (e->key() == Qt::Key_Escape) {
			clearPreedit();
			changed();
		} else if (e->key() == Qt::Key_Backspace && m_preeditCursor > 0) {
			m_preedit.remove(m_preeditCursor - 1, 1);
			--m_preeditCursor;
			changed();
		}
		return true;
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
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_V) {
		/* Paste: the way to get a bullet (•) into a name for a Toy Box page. */
		insertText(QApplication::clipboard()->text());
		return true;
	}
	const QString typed = e->text();
	if (!typed.isEmpty() && typed.at(0).isPrint() && !(e->modifiers() & Qt::ControlModifier)) {
		insertText(typed);
		return true;
	}
	return true; /* everything else is swallowed while editing */
}

void LabelEditor::cancel() {
	m_item = nullptr;
	clearPreedit();
	m_blink.stop();
	reportEditing();
	m_repaint();
}

QString renamedFileName(const QString &oldName, const QString &shownName, const QString &typed) {
	if (typed.isEmpty() || shownName.isEmpty()) {
		return typed;
	}
	/* Typed with an extension of its own: the user means that one. */
	if (!hiddenExtension(typed).isEmpty()) {
		return typed;
	}
	return typed + hiddenExtension(oldName);
}

void LabelEditor::commit() {
	Item *item = m_item;
	if (!item) {
		return;
	}
	/* A preedit still in the field when editing ends is text the person
	 * sees; keep it rather than losing what they typed. */
	if (!m_preedit.isEmpty()) {
		insertText(m_preedit);
	}
	const QString name = renamedFileName(item->name, item->shownName, m_text.trimmed());
	m_item = nullptr;
	m_blink.stop();
	reportEditing();
	m_repaint();
	if (name.isEmpty() || name == item->name) {
		return;
	}
	if (item->isVirtual) {
		/* A virtual rename is metadata: the registry keeps it, and the
		 * item's id (and so its icon's place) doesn't change. */
		if (vfsNameTaken(item->path, name)) {
			Alert::ask(tr9("That name is already taken. Please use a different name."),
				"OK", "");
			return;
		}
		if (!vfsRename(item->path, name)) {
			Alert::ask(tr9("The item “%1” can't be renamed.")
				.arg(item->visibleName()), "OK", "");
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
		Alert::ask(tr9("That name is already taken. Please use a different name."), "OK", "");
		return;
	}
	const QString folder = dir.absolutePath();
	if (!QDir().rename(item->path, dir.filePath(name))) {
		Alert::ask(QStringLiteral("The item “%1” could not be renamed.").arg(item->visibleName()),
			"OK", "");
		return;
	}
	if (!aliasMoveRecord(item->path, dir.filePath(name), false)) {
		Alert::ask(QStringLiteral("“%1” was renamed, but its alias identity record could not be "
			"preserved.").arg(nameWithoutExtension(name)), "OK", "");
	}
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
void LabelEditor::origin(int x, int y, int width, int *pen, int *top) const {
	if (m_beside) {
		*pen = x + PL_ICON_SMALL + 4;
		*top = y + 1;
	} else {
		*pen = x + PL_ICON_LARGE / 2 - width / 2;
		*top = y + PL_ICON_LARGE + LABEL_GAP;
	}
}

void LabelEditor::paint(pl_canvas *c, int x, int y) const {
	const QString shown = displayed();
	const int width = advanceTo(shown.size());
	int pen, top;
	origin(x, y, width, &pen, &top);
	pl_fill(c, pen - BOX_PAD - 1, top, pen + width + BOX_PAD, top + BOX_H - 1, C_WHITE);
	pl_outline(c, pen - BOX_PAD - 1, top, pen + width + BOX_PAD, top + BOX_H - 1, C_BLACK);

	const int a = std::min(m_cursor, m_anchor), b = std::max(m_cursor, m_anchor);
	if (a != b && m_preedit.isEmpty()) {
		pl_fill(c, pen + advanceTo(a), top + 1, pen + advanceTo(b) - 1, top + BOX_H - 2,
			TEXT_HILITE);
	}
	Text t(shown, 100000, PL_FONT_VIEWS);
	if (t.t && t.t->ink_l >= 0) {
		/* Text is rendered with its pen at mask column 1. */
		pl_text(c, t.t, pen + t.t->ink_l - 1, top + BASELINE, C_BLACK);
	}
	if (!m_preedit.isEmpty()) {
		const int from = m_cursor, to = m_cursor + static_cast<int>(m_preedit.size());
		pl_hline(c, pen + advanceTo(from), pen + advanceTo(to), top + BASELINE + 1, C_BLACK);
	}
	const int caret = m_preedit.isEmpty() ? m_cursor :
		m_cursor + std::clamp(m_preeditCursor, 0, static_cast<int>(m_preedit.size()));
	if ((a == b || !m_preedit.isEmpty()) && m_caretOn) {
		pl_vline(c, pen + advanceTo(caret), top + 2, top + BOX_H - 3, C_BLACK);
	}
}

bool LabelEditor::contains(int x, int y, QPoint p) const {
	return boxRect(QPoint(x, y)).contains(p);
}
