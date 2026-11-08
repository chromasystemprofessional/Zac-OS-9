#pragma once

#include <QPoint>
#include <QWidget>
#include <memory>
#include <vector>

#include "findcriteria.h"
#include "items.h"
#include "panelkit.h"
#include "pixels.h"

class BloodhoundSearch;

/*
 * Bloodhound 1.0: the Files channel in one window. The search field and
 * scope sit above the result list; the engine is the Sniffer's Find
 * (search.h), and the results open and reveal through zacos9-finder-ctl.
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

private:
	void search();
	void stop();
	void addResults(const QStringList &paths);
	void finished(const QString &problem);
	FindQuery query() const;

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
};
