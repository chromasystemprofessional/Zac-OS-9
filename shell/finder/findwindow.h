#pragma once

#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <memory>
#include <vector>

#include "finder.h"
#include "pixels.h"
#include "widgets.h"

/*
 * Find (Mac OS 8.0/8.1 "Find File"): a small window asking for part of a
 * name, and an "Items Found" window listing every match on the disk as
 * the search turns them up. Items Found is a Finder view, so Open, Get
 * Info, Move To Trash and the rest work on its selection.
 * TODO: neither window is in the HIG; the layouts are estimates, and the
 * criteria popups ("on all disks", "name", "contains") are fixed.
 */
class FindDialog : public QWidget {
public:
	static void open();

protected:
	void paintEvent(QPaintEvent *) override;
	void keyPressEvent(QKeyEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;

private:
	FindDialog();
	void find();
	QRect findButton() const;

	QString m_text;
	bool m_caretOn = true;
	QTimer m_caretTimer;
	bool m_tracking = false, m_inside = false;
};

class FoundWindow : public QWidget, public FinderView {
public:
	/* A new results window searching for names containing `text`. */
	static void search(const QString &text);
	/* Drop results that no longer exist; re-read labels. */
	static void reloadAll();
	~FoundWindow() override;

	QString folderPath() const override;
	std::vector<Item *> selectedItems() override;
	void selectByName(const QString &name) override;
	void reload() override;
	QWidget *widget() override { return this; }

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void mouseDoubleClickEvent(QMouseEvent *) override;
	void wheelEvent(QWheelEvent *) override;
	void keyPressEvent(QKeyEvent *) override;
	void changeEvent(QEvent *) override;
	void closeEvent(QCloseEvent *) override;

private:
	explicit FoundWindow(const QString &text);
	void addResults(const QStringList &paths);
	void finished();
	void updateTitle();
	int listTop() const;
	int listHeight() const;
	int paneTop() const;
	pl_scrollbar bar() const;
	void scrollTo(int y);
	Item *itemAt(QPoint pos);

	QString m_text;
	std::vector<std::unique_ptr<Item>> m_items;
	bool m_searching = true;
	QThread *m_worker = nullptr;
	int m_scrollY = 0;
	enum sb_part m_sbPart = SB_NONE;
	int m_thumbGrab = 0;
};
