#pragma once

#include <QFileSystemWatcher>
#include <QSet>
#include <QTimer>
#include <QWidget>
#include <map>

#include "finder.h"
#include "folderstate.h"
#include "items.h"
#include "labeleditor.h"
#include "widgets.h"

/*
 * A Finder window onto one folder, as icons or as a list. Windows are
 * spatial: each folder has exactly one window, reopened where it was.
 */
class FolderWindow : public QWidget, public FinderView {
public:
	enum class ViewMode { Icons, List, Buttons };

	/* Opens (or brings back) the window for `path`. */
	static FolderWindow *open(const QString &path);
	static bool isOpen(const QString &path);
	/* Reload every open window showing `folder` (or a folder expanded in it). */
	static void reloadAll(const QString &folder);
	~FolderWindow() override;

	void setViewMode(ViewMode mode);
	ViewMode viewMode() const { return m_mode; }

	/* FinderView */
	QString folderPath() const override { return m_path; }
	std::vector<Item *> selectedItems() override;
	void selectByName(const QString &name) override;
	void reload() override;
	QWidget *widget() override { return this; }
	void itemRenamed(const QString &from, const QString &to) override;

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

	/* Layout and scrolling, shared by both views. */
	void relayout();
	int contentTop() const;   /* first window row of the scrolling area */
	int viewWidth() const;
	int viewHeight() const;
	int scrollMaxX() const;
	int scrollMaxY() const;
	void scrollTo(int x, int y);
	pl_scrollbar verticalBar() const;
	pl_scrollbar horizontalBar() const;
	QString headerText() const;
	QPoint toContent(QPoint windowPos) const;
	QPoint toWindow(QPoint contentPos) const;
	void scrollStep();

	/* Icon view. */
	void layoutIcons();
	void paintIcons(pl_canvas *content);

	/* Button view: each item a bevel button; a click opens it. */
	void layoutButtons();
	void paintButtons(pl_canvas *content);
	Item *buttonAt(QPoint content, bool *onButton);
	Item *m_buttonDown = nullptr;
	bool m_buttonInside = false;

	/* List view (HIG figure 2-24). */
	struct Row {
		Item *item;
		int depth;
	};
	void buildRows();
	void paintColumnHeaders(pl_canvas *band);
	void paintList(pl_canvas *content);
	int listWidth() const;
	int columnAt(int contentX) const;
	void sortItems(std::vector<std::unique_ptr<Item>> &items) const;
	void toggleExpanded(Item *item);

	/* Items: the folder's own, plus the contents of expanded folders. */
	template <typename F> void forEachItem(F f);
	Item *itemAt(QPoint windowPos, bool *onTriangle = nullptr);
	Item *dropTargetAt(QPoint windowPos, const QStringList &dragged);
	void clearDropTarget();

	QString m_path;
	/* Remembered between sessions; written shortly after each change. */
	FolderState m_state;
	QTimer m_saveTimer;
	void saveStateSoon();
	ViewMode m_mode = ViewMode::Icons;
	std::vector<std::unique_ptr<Item>> m_items;
	std::map<QString, std::vector<std::unique_ptr<Item>>> m_children;
	QSet<QString> m_expanded;
	std::vector<Row> m_rows;
	int m_sortColumn = 0; /* Name */
	int m_scrollX = 0, m_scrollY = 0;
	int m_contentHeight = 0;
	int m_contentWidth = 0; /* icon view: rightmost placed icon */
	QPoint m_dragStart;     /* where a drag of our own icons began */
	QFileSystemWatcher m_watcher;
	/* Virtual folders have no directory to watch: they are reloaded when
	 * the Macintosh view changes (see vfs.h). 0 when not subscribed. */
	int m_vfsToken = 0;

	/* A press on an icon that may turn into a drag. */
	Item *m_pressItem = nullptr;
	QPoint m_pressPos;

	/* Renaming (icon view): a click on a selected name, held still. */
	LabelEditor m_editor{ [this] { update(); } };
	QTimer m_renameTimer;
	Item *m_renameItem = nullptr;
	void beginRename(Item *item);

	/* Scroll bar tracking. */
	bool m_sbVertical = true;
	sb_part m_sbPart = SB_NONE;
	int m_thumbGrab = 0; /* pointer offset within the thumb */
	QTimer m_repeat;
};

/* ⌘-key equivalents shared by Finder windows and the desktop. Linux apps
 * see ⌘ as Ctrl (zacos9-wm translates it). Returns true if handled. */
bool finderShortcut(QKeyEvent *e);
