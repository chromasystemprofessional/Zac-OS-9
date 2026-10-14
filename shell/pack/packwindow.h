#pragma once

#include <QSettings>
#include <QTimer>
#include <QWidget>
#include <vector>

#include "apps.h"

/*
 * The Pack: Mac OS 9's Application palette (Missing Manual pp. 103-104), torn
 * off the Application menu. A floating window, above the others, of a tile
 * per running program: click one to bring it forward (Option-click also
 * hides the program you leave), drop documents on one to open them with
 * it. Its title bar moves it (as does ⌘-dragging anywhere); its zoom box
 * shows or hides the names, Option-zoom makes the icons large, Shift-
 * Option-zoom lays it out across; the collapse box rolls it up; the close
 * box puts it away.
 */
class PackWindow : public QWidget {
public:
	PackWindow();
	/* Moves it so (x, y) is its top-left on the screen. */
	void moveTo(QPoint topLeft);
	void refresh();

	/* Its look, kept between sessions. */
	struct Look {
		bool names = true, large = false, across = false, collapsed = false;
	};
	const Look &look() const { return m_look; }

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void dragEnterEvent(QDragEnterEvent *) override;
	void dragMoveEvent(QDragMoveEvent *) override;
	void dragLeaveEvent(QDragLeaveEvent *) override;
	void dropEvent(QDropEvent *) override;

private:
	enum class Box { None, Close, Zoom, Collapse };
	void becomeLayerSurface();
	void relayout();
	void save();
	int iconSize() const { return m_look.large ? 32 : 16; }
	QSize tileSize() const;
	QRect tileRect(int i) const;
	int tileAt(QPoint p) const;
	QRect boxRect(Box box) const;
	Box boxAt(QPoint p) const;

	QSettings m_settings;
	Look m_look;
	QPoint m_pos;
	std::vector<PackApp> m_apps;
	QTimer m_refresh;

	/* Pointer tracking. */
	bool m_moving = false;
	QPoint m_pressLocal;
	Box m_box = Box::None;
	bool m_boxInside = false;
	int m_pressedTile = -1;
	int m_dropTile = -1;
};
