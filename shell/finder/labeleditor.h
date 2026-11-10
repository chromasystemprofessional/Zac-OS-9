#pragma once

#include <QPoint>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <functional>

#include "items.h"

class QInputMethodEvent;
class QKeyEvent;

/* The file name a rename to `typed` gives: an item whose extension is
 * hidden (`shownName` set) keeps it, unless `typed` names one itself. */
QString renamedFileName(const QString &oldName, const QString &shownName, const QString &typed);

/*
 * Editing an icon's name in place. The label becomes a white box with a
 * black outline; the selection uses the text highlight colour and a
 * blinking caret marks the insertion point.
 * TODO: measure the edit box from a real Finder.
 */
class LabelEditor {
public:
	/* `repaint` is called whenever the box needs redrawing. */
	explicit LabelEditor(std::function<void()> repaint);

	void begin(Item *item);
	/* The view reloaded: follow the edited item (by name) or stop. */
	void retarget(const std::vector<std::unique_ptr<Item>> &items);
	bool editing(const Item *item) const { return m_item == item; }
	bool active() const { return m_item != nullptr; }
	Item *item() const { return m_item; }

	/* Handles a key; returns false if it isn't an editing key. On Return
	 * the edit is committed, on Escape cancelled. */
	bool key(QKeyEvent *e);
	/* The input method's preedit and commit text, and the queries that put
	 * its candidate window under the caret. `iconPos` is where paint draws
	 * the icon in the owner's coordinates. */
	bool inputMethodEvent(QInputMethodEvent *e);
	QVariant inputMethodQuery(Qt::InputMethodQuery query, QPoint iconPos) const;
	QRect cursorRect(QPoint iconPos) const;
	/* What has been committed, and what is still being composed. */
	QString text() const { return m_text; }
	QString preedit() const { return m_preedit; }
	/* The view calls this to tell Qt and the on-screen keyboard when the
	 * label becomes, or stops being, a text field. */
	void setEditingChanged(std::function<void(bool)> changed) {
		m_editingChanged = std::move(changed);
	}
	/* Commit (rename on disk) or cancel; both end editing. */
	void commit();
	void cancel();

	/* Small icons (View Options): the name sits beside the 16 px icon,
	 * not under it. */
	void setBeside(bool beside) { m_beside = beside; }

	/* Paint the edit box for the item whose icon is at (x, y). */
	void paint(pl_canvas *c, int x, int y) const;
	/* Is `p` inside the edit box of the icon at (x, y)? */
	bool contains(int x, int y, QPoint p) const;

private:
	int advanceTo(int chars) const;
	void deleteSelection();
	void insertText(const QString &raw);
	void setPreedit(const QString &text, int cursor);
	void clearPreedit();
	void reportEditing();
	void changed();
	QString displayed() const;
	QRect boxRect(QPoint iconPos) const;

	std::function<void()> m_repaint;
	std::function<void(bool)> m_editingChanged;
	Item *m_item = nullptr;
	QString m_text;
	QString m_preedit;
	int m_cursor = 0, m_anchor = 0;
	int m_preeditCursor = 0;
	bool m_caretOn = true;
	bool m_beside = false;
	bool m_editingReported = false;
	QTimer m_blink;
	/* The pen and the box's top for the icon at (x, y). */
	void origin(int x, int y, int width, int *pen, int *top) const;
};
