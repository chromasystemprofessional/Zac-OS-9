#pragma once

#include <QString>
#include <QTimer>
#include <functional>

#include "items.h"

class QKeyEvent;

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
	/* Commit (rename on disk) or cancel; both end editing. */
	void commit();
	void cancel();

	/* Paint the edit box for the item whose icon is at (x, y). */
	void paint(pl_canvas *c, int x, int y) const;
	/* Is `p` inside the edit box of the icon at (x, y)? */
	bool contains(int x, int y, QPoint p) const;

private:
	int advanceTo(int chars) const;
	void deleteSelection();
	void changed();

	std::function<void()> m_repaint;
	Item *m_item = nullptr;
	QString m_text;
	int m_cursor = 0, m_anchor = 0;
	bool m_caretOn = true;
	QTimer m_blink;
};
