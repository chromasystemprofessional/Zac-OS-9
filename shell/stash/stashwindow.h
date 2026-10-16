#pragma once

#include <QMainWindow>

#include "stash.h"

class QLabel;
class QScrollBar;
class ClipView;

/*
 * Stash, ZacOS 9's Scrapbook (Missing Manual ch. 5): one window showing a
 * clip at a time, the scroll bar below to leaf through them, and what the
 * clip is underneath. Paste or drop text and pictures in, Copy or drag
 * them out; Cut and Clear take a page away, and Undo puts it back.
 */
class StashWindow : public QMainWindow {
public:
	explicit StashWindow(const QString &dir = Stash::defaultDir());

	Stash &stash() { return m_stash; }
	int currentPage() const { return m_page; }
	void showPage(int page);
	QString infoText() const;

	/* The Edit menu's commands. */
	void cut();
	void copy();
	void paste();
	void clear();
	void undo();
	/* Add a drop or paste after the page in view, and show it. */
	bool add(const QMimeData *data);

protected:
	void keyPressEvent(QKeyEvent *event) override;

private:
	void refresh();

	/* What Undo reverses: pages added, or one taken away. */
	struct Change {
		enum Kind { None, Added, Removed } kind = None;
		int at = 0;
		int count = 0;
		Clip clip;
	};

	Stash m_stash;
	Change m_last;
	int m_page = 0;
	ClipView *m_view = nullptr;
	QScrollBar *m_scroll = nullptr;
	QLabel *m_number = nullptr;
	QLabel *m_info = nullptr;
};
