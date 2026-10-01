#pragma once

#include <QFileSystemWatcher>
#include <QTimer>
#include <QWidget>

#include "finder.h"
#include "items.h"
#include "widgets.h"

/*
 * A Finder window onto one folder, icon view. Windows are spatial: each
 * folder has exactly one window, reopened where it was.
 */
class FolderWindow : public QWidget, public FinderView {
public:
	/* Opens (or brings back) the window for `path`. */
	static FolderWindow *open(const QString &path);
	/* Reload every open window showing `folder`. */
	static void reloadAll(const QString &folder);
	~FolderWindow() override;

	/* FinderView */
	QString folderPath() const override { return m_path; }
	std::vector<Item *> selectedItems() override;
	void selectByName(const QString &name) override;
	void reload() override;
	QWidget *widget() override { return this; }

protected:
	void paintEvent(QPaintEvent *) override;
	void resizeEvent(QResizeEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void mouseDoubleClickEvent(QMouseEvent *) override;
	void wheelEvent(QWheelEvent *) override;
	void keyPressEvent(QKeyEvent *) override;
	void dragEnterEvent(QDragEnterEvent *) override;
	void dragMoveEvent(QDragMoveEvent *) override;
	void dragLeaveEvent(QDragLeaveEvent *) override;
	void dropEvent(QDropEvent *) override;
	void changeEvent(QEvent *) override;
	void closeEvent(QCloseEvent *) override;

private:
	explicit FolderWindow(const QString &path);
	void layoutIcons();
	int viewHeight() const;
	int scrollMax() const;
	void scrollTo(int y);
	pl_scrollbar verticalBar() const;
	QString headerText() const;
	Item *itemAt(QPoint windowPos);
	Item *dropTargetAt(QPoint windowPos, const QStringList &dragged);
	void clearDropTarget();
	QPoint toWindow(QPoint contentPos) const;
	void scrollStep();

	QString m_path;
	std::vector<std::unique_ptr<Item>> m_items;
	int m_scroll = 0;
	int m_contentHeight = 0;
	QFileSystemWatcher m_watcher;

	/* A press on an icon that may turn into a drag. */
	Item *m_pressItem = nullptr;
	QPoint m_pressPos;

	/* Scroll bar tracking. */
	sb_part m_sbPart = SB_NONE;
	int m_thumbGrab = 0; /* pointer offset within the thumb */
	QTimer m_repeat;
};

/* ⌘-key equivalents shared by Finder windows and the desktop. Linux apps
 * see ⌘ as Ctrl (platinum-wm translates it). Returns true if handled. */
bool finderShortcut(QKeyEvent *e);
