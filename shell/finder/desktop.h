#pragma once

#include <QFileSystemWatcher>
#include <QWidget>

#include <QTimer>

#include "finder.h"
#include "folderstate.h"
#include "items.h"
#include "labeleditor.h"

/*
 * The desktop: a full-screen wlr-layer-shell surface beneath all windows,
 * showing the desktop pattern, the disk at top right, the items in
 * ~/Desktop below it, and the Trash at bottom right. Like the Mac's, it is
 * a view onto the Desktop folder.
 */
class Desktop : public QWidget, public FinderView {
public:
	Desktop();
	/* Turn this widget into the layer surface; call before show(). */
	void becomeLayerSurface();
	/* Something changed in `folder`: refresh desktop items or the Trash. */
	void folderChanged(const QString &folder);

	/* FinderView */
	QString folderPath() const override;
	std::vector<Item *> selectedItems() override;
	void selectByName(const QString &name) override;
	void reload() override;
	QWidget *widget() override { return this; }
	void itemRenamed(const QString &from, const QString &to) override;

protected:
	void paintEvent(QPaintEvent *) override;
	void resizeEvent(QResizeEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseDoubleClickEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void dragEnterEvent(QDragEnterEvent *) override;
	void dragMoveEvent(QDragMoveEvent *) override;
	void dragLeaveEvent(QDragLeaveEvent *) override;
	void dropEvent(QDropEvent *) override;

private:
	void placeIcons();
	void updateTrashIcon();
	Item *itemAt(QPoint pos);
	Item *dropTargetAt(QPoint pos, const QStringList &dragged);
	void clearDropTarget();
	std::vector<Item *> allItems();

	Item *m_pressItem = nullptr;
	QPoint m_pressPos;
	QPoint m_dragStart;

	/* Icons the user placed on the desktop, remembered (spatial). */
	FolderState m_state;

	/* Renaming items from ~/Desktop (the disk and Trash keep their names). */
	LabelEditor m_editor{ [this] { update(); } };
	QTimer m_renameTimer;
	Item *m_renameItem = nullptr;
	bool renamable(const Item *item) const;
	void beginRename(Item *item);

	std::unique_ptr<Item> m_disk, m_trash;
	std::vector<std::unique_ptr<Item>> m_files; /* ~/Desktop */
	QFileSystemWatcher m_watcher;
};
