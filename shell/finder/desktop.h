#pragma once

#include <QWidget>

#include "items.h"

/*
 * The desktop: a full-screen wlr-layer-shell surface beneath all windows,
 * showing the desktop pattern, the disk at top right and the Trash at
 * bottom right.
 */
class Desktop : public QWidget {
public:
	Desktop();
	/* Turn this widget into the layer surface; call before show(). */
	void becomeLayerSurface();

protected:
	void paintEvent(QPaintEvent *) override;
	void resizeEvent(QResizeEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseDoubleClickEvent(QMouseEvent *) override;

private:
	void placeIcons();
	Item *itemAt(QPoint pos);

	std::vector<std::unique_ptr<Item>> m_items;
};
