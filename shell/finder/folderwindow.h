#pragma once

#include <QTimer>
#include <QWidget>

#include "items.h"
#include "widgets.h"

/*
 * A Finder window onto one folder, icon view. Windows are spatial: each
 * folder has exactly one window, reopened where it was.
 */
class FolderWindow : public QWidget {
public:
	/* Opens (or brings back) the window for `path`. */
	static FolderWindow *open(const QString &path);
	~FolderWindow() override;

	const QString &path() const { return m_path; }

protected:
	void paintEvent(QPaintEvent *) override;
	void resizeEvent(QResizeEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void mouseDoubleClickEvent(QMouseEvent *) override;
	void wheelEvent(QWheelEvent *) override;

private:
	explicit FolderWindow(const QString &path);
	void reload();
	void layoutIcons();
	int viewHeight() const;
	int scrollMax() const;
	void scrollTo(int y);
	pl_scrollbar verticalBar() const;
	QString headerText() const;
	Item *itemAt(QPoint windowPos);
	void openItem(Item *item);
	void scrollStep();

	QString m_path;
	std::vector<std::unique_ptr<Item>> m_items;
	int m_scroll = 0;
	int m_contentHeight = 0;

	/* Scroll bar tracking. */
	sb_part m_sbPart = SB_NONE;
	int m_thumbGrab = 0; /* pointer offset within the thumb */
	QTimer m_repeat;
};
