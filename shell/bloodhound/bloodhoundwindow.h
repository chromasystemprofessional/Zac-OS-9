#pragma once

#include <QHash>
#include <QPoint>
#include <QStringList>
#include <QWidget>
#include <memory>
#include <vector>

#include "findcriteria.h"
#include "items.h"
#include "panelkit.h"
#include "pixels.h"

class BloodhoundSearch;
class QContextMenuEvent;

/*
 * Bloodhound (Search): Sherlock 2's Files channel in one window. The search
 * field, scope and criteria rows sit above the result list; the engine is
 * the Sniffer's Find (search.h, findcriteria.*, findindex.*), and results
 * open and reveal through zacos9-finder-ctl.
 */
class BloodhoundWindow : public QWidget {
public:
	explicit BloodhoundWindow(const QString &folder = QString());
	~BloodhoundWindow() override;

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void mouseDoubleClickEvent(QMouseEvent *) override;
	void wheelEvent(QWheelEvent *) override;
	void keyPressEvent(QKeyEvent *) override;
	void contextMenuEvent(QContextMenuEvent *) override;

private:
	struct CriterionRow;
	struct Snippet {
		QString text;
		QStringList words;
		bool asked = false;
	};

	void search();
	void stop();
	void addResults(const QStringList &paths);
	void finished(const QString &problem);
	void findSimilar();
	FindQuery query() const;

	void addCriterion(FindField field = FindField::Modified);
	void removeCriterion();
	void setCriterionField(CriterionRow &row, FindField field);
	void relayout();
	void saveSearch();
	void showSavedSearches();
	void indexSelection();
	void applyQuery(const FindQuery &q);
	void rebuildScope(FindScope scope = FindScope::AllDisks);
	bool hasContentsRow() const;
	int rowHeight() const;
	void requestSnippet(const QString &path);
	void startNextSnippet();
	void paintSnippet(pl_canvas *c, const Snippet &snippet, int x, int baseline, int width) const;

	std::vector<Item *> selectedItems();
	void selectOnly(Item *item);
	void moveSelection(int step);
	void openSelection();
	void revealSelection();
	void getInfo();
	void copyPaths();
	void runFinderCtl(const QStringList &args);

	int listTop() const;
	int paneTop() const;
	int listHeight() const;
	pl_scrollbar bar() const;
	void scrollTo(int y);
	Item *itemAt(QPoint pos) const;

	QString m_folder;
	PanelHost m_host{ this };
	PanelEdit m_search;
	PanelPopup m_scope;
	PanelButton m_find, m_stop;
	PanelButton m_more, m_fewer, m_saveSearch, m_savedSearches, m_indexSelection;
	std::vector<std::unique_ptr<CriterionRow>> m_rows;
	QPoint m_pressPos;
	Item *m_pressItem = nullptr;
	bool m_listFocus = false;
	bool m_dragging = false;
	int m_scrollY = 0;
	enum sb_part m_sbPart = SB_NONE;
	int m_thumbGrab = 0;
	QString m_problem;
	std::vector<std::unique_ptr<Item>> m_items;
	BloodhoundSearch *m_worker = nullptr;
	bool m_searching = false;
	QStringList m_contentWords;
	QHash<QString, Snippet> m_snippets;
	QStringList m_snippetQueue;
	int m_snippetRequests = 0;
	bool m_snippetRunning = false;
	quint64 m_generation = 0;
	QString m_similarTo;
	int m_resultsTop = 100;
};
