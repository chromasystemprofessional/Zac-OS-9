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
	~Desktop() override;
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
	void arrange(Arrange how) override;

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
	void loadPattern();

	int m_pattern = -1;
	QFileSystemWatcher m_settingsWatcher;
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

	/* The startup disk (the Macintosh view), the Trash, and Debian's own
	 * filesystem when the registry asks for it. */
	std::unique_ptr<Item> m_disk, m_trash, m_unix;
	void buildUnixDisk();
	std::vector<Item *> fixedItems() const;
	bool isFixed(const Item *item) const;
	int m_vfsToken = 0;

	/* Volumes mounted from the Network Browser: polled (nothing watches
	 * fusermount3 -u or gio mount -u run from a terminal, so there is
	 * no event to wait for instead), not kept open across a rename —
	 * there is nothing to rename, the name is the server's. */
	std::vector<std::unique_ptr<Item>> m_netVolumes;
	QTimer m_netVolumesTimer;
	void refreshNetVolumes();

	/* Block-device volumes mounted locally (USB, extra HDDs, optical
	 * discs, SD cards). Updated via GVolumeMonitor signals. */
	std::vector<std::unique_ptr<Item>> m_localVolumes;
	void refreshLocalVolumes();

	std::vector<std::unique_ptr<Item>> m_files; /* ~/Desktop */
	QFileSystemWatcher m_watcher;
};
