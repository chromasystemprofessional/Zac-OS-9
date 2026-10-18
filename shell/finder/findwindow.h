#pragma once

#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <memory>
#include <vector>

#include "findcriteria.h"
#include "finder.h"
#include "panelkit.h"
#include "pixels.h"
#include "widgets.h"

/*
 * Find (Mac OS 9's Find, Missing Manual ch. 15): where to look, and rows
 * of criteria that must all match ("name contains", "date modified is
 * within the past week", "contents include"...), with More Choices and
 * Fewer Choices. The results open in an Items Found window, a Finder view,
 * so Open, Get Info, Move To Trash and the rest work on its selection.
 */
class FindDialog : public QWidget {
public:
	/* `folder`: the front folder window's, offered as a place to look. */
	static void open(const QString &folder = QString());
	~FindDialog() override;

protected:
	void paintEvent(QPaintEvent *) override;
	void keyPressEvent(QKeyEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;

private:
	struct Row;
	explicit FindDialog(const QString &folder);
	void addRow(FindField field);
	void setField(Row &row, FindField field);
	void relayout();
	FindQuery query() const;
	void find();

	QString m_folder;
	PanelHost m_host{ this };
	PanelPopup m_scope;
	int m_internetFirst = 0; /* the scope's first Internet channel */
	std::vector<std::unique_ptr<Row>> m_rows;
	PanelButton m_more, m_fewer, m_saveSearch, m_savedSearches, m_find;
	QString m_problem;
};

class FoundWindow : public QWidget, public FinderView {
public:
	/* A new results window for the query. */
	static void search(const FindQuery &query);
	/* Find Similar Items: one with the files like `path`. */
	static void similarTo(const QString &path);
	/* The open ones, oldest first. */
	static std::vector<QPointer<FoundWindow>> &windows();
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
	explicit FoundWindow(const QString &title);
	void walk(const FindQuery &query, const QStringList &candidates, bool useCandidates);
	void addResults(const QStringList &paths);
	void finished();
	void updateTitle();
	int listTop() const;
	int listHeight() const;
	int paneTop() const;
	pl_scrollbar bar() const;
	void scrollTo(int y);
	Item *itemAt(QPoint pos);

	QString m_title;
	QString m_problem; /* why there are no results (the index isn't running...) */
	std::vector<std::unique_ptr<Item>> m_items;
	bool m_searching = true;
	QThread *m_worker = nullptr;
	int m_scrollY = 0;
	enum sb_part m_sbPart = SB_NONE;
	int m_thumbGrab = 0;
};
