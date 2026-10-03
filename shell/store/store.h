#pragma once

#include <QTimer>
#include <QWidget>
#include <memory>
#include <vector>

#include "panelkit.h"
#include "storeclient.h"

/*
 * zacos9-store: the Software window, reached from the Apple menu. A
 * curated catalog on one side (Mac OS 9 never had one of these; styled
 * after its own list-and-details windows, such as the Software Installer
 * that shipped on its CDs), apt underneath through zacos9-appstore-helper.
 */
class StoreWindow : public QWidget {
public:
	StoreWindow();

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void wheelEvent(QWheelEvent *) override;
	void keyPressEvent(QKeyEvent *) override;
	void closeEvent(QCloseEvent *) override;

private:
	void layout();
	void selectCategory(int row);
	void selectItem(int row);
	const StoreItem *currentItem() const;
	void refreshInstalled();  /* re-checks the selected item only */
	void act();               /* the detail button: install or remove */
	void paintDetail(pl_canvas *c, uint32_t bg) const;

	std::vector<StoreItem> m_allItems;
	QStringList m_categories;
	std::vector<const StoreItem *> m_shown; /* items in the chosen category */

	PanelList m_categoryList, m_itemList;
	PanelButton m_actionButton;
	PanelHost m_host{ this };

	/* The selected item's installed state, re-checked when it is chosen
	 * and after an install/remove finishes; not polled continuously
	 * (dpkg-query for every catalog item on every repaint would be
	 * wasteful for something that only changes because of our own
	 * actions or, rarely, someone else's in a terminal). */
	bool m_installed = false;
	bool m_busy = false;
	QString m_status;

	/* An indeterminate sweep while apt runs: real per-package progress
	 * would mean parsing apt's own output, which is not stable across
	 * versions; this says "working", not a precise, possibly wrong,
	 * percentage. */
	QTimer m_sweep;
	double m_sweepPos = 0;
};
