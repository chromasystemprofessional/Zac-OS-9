#pragma once

#include <QImage>

#include <QFileSystemWatcher>
#include <QSet>
#include <QTimer>
#include <QWidget>
#include <atomic>
#include <map>
#include <memory>

#include "finder.h"
#include "folderstate.h"
#include "items.h"
#include "labeleditor.h"
#include "viewoptions.h"
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
	/* File > Print Window: everything in the window, not just what's
	 * scrolled into view, on white and with nothing selected; a list
	 * with its column headings. */
	QImage printImage();
	static bool isOpen(const QString &path);
	static void closeAll();
	/* Reload every open window showing `folder` (or a folder expanded in it). */
	static void reloadAll(const QString &folder);
	~FolderWindow() override;

	void setViewMode(ViewMode mode);
	ViewMode viewMode() const { return m_mode; }
	/* View > as Window / as Pop-up Window: the window docked at the bottom
	 * of the screen as a tab, opening above it when clicked. */
	void setPopup(bool popup);
	bool isPopup() const { return m_state.popup; }

	/* View Options (viewoptions.h): this window's, for each of its views. */
	const ViewOptions &viewOptions() const { return m_state.options; }
	void setViewOptions(const ViewOptions &options);
	/* View > Sort List and the column headers: a ListColumn shown. */
	void sortBy(int column);
	int sortColumn() const { return m_sortColumn; }
	/* The list's columns, left to right (Name first), and their widths:
	 * the user's, from dragging headers and dividers, else the Mac's. */
	std::vector<int> columns() const;
	int columnWidth(int column) const;
	bool columnsCustomized() const;
	/* View > Reset Column Positions. */
	void resetColumns();
	/* The sort-direction button (or View > Sort List's twin): reversed
	 * shows the list from the other end. */
	bool sortReversed() const { return m_state.sortReversed; }
	void setSortReversed(bool reversed);
	/* Preferences changed (grid spacing, labels, standard views): every
	 * window lays itself out again. */
	static void preferencesChanged();
	/* The proxy icon in the title bar (platinumSetProxyIcon). */
	void setUpProxyIcon();

	/* FinderView */
	QString folderPath() const override { return m_path; }
	std::vector<Item *> selectedItems() override;
	void selectByName(const QString &name) override;
	void reload() override;
	QWidget *widget() override { return this; }
	void itemRenamed(const QString &from, const QString &to) override;
	void arrange(Arrange how) override;
	void selectAll() override;

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
	/* Tell zacos9-wm the size the zoom box gives us: just big enough for
	 * the icons (or the list), as the Mac's Finder did. */
	void updateStandardSize();
	QSize m_standardSize;
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
	void showContextMenuAt(QPoint pos);

	/* Icon view, standard or small icons, on a Wide or Tight grid. */
	struct IconGrid {
		int cellW, cellH, inset; /* inset: the icon's left within its cell */
	};
	IconGrid iconGrid() const;
	int iconColumns() const;
	QPoint iconSlot(int i) const;
	bool smallIcons() const;
	void layoutIcons();
	void paintIcons(pl_canvas *content);
	/* An icon item at item->pos: its icon and name, as either size draws them. */
	QRect itemRect(Item &item);
	bool itemHit(Item &item, QPoint content);
	bool labelHit(Item &item, QPoint content);
	void paintItem(pl_canvas *c, Item &item, bool showLabel);
	void paintItemLabel(pl_canvas *c, Item &item);
	QRect smallLabelBox(Item &item);
	/* Always snap to grid: the moved icons go to the nearest free slots. */
	void snapToGrid(const std::vector<Item *> &moved);

	/* Button view: each item a bevel button; a click opens it. */
	int buttonSide() const;
	QSize buttonCell() const;
	QRect buttonRect(const Item *item) const;
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
	struct ListMetrics {
		int rowH, icon, iconX, iconY, nameX, baseline;
	};
	ListMetrics metrics() const;
	int columnX(int column) const;
	/* The sort-direction button: above the vertical scroll bar, at the
	 * end of the column headers. */
	QRect sortDirectionButton() const;
	/* A press in the column headers: a click sorts, a drag on a divider
	 * resizes the column to its left, a drag on a header moves it. */
	enum class HeaderDrag { None, Press, Move, Resize };
	HeaderDrag m_headerDrag = HeaderDrag::None;
	int m_headerColumn = -1, m_headerPressX = 0, m_headerPointerX = 0, m_headerStartWidth = 0;
	void moveColumn(int column, int contentX);
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
	bool acceptsStartupDrops();

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
	int m_sortColumn = 0; /* a ListColumn; Name */
	/* Calculate folder sizes: folders' contents added up off the main
	 * thread, kept until the window reloads. */
	void calculateFolderSizes();
	void folderSized(const QString &path, qint64 bytes);
	QHash<QString, qint64> m_folderSizes;
	QSet<QString> m_sizing;
	std::shared_ptr<std::atomic<bool>> m_sizingCancel;
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

	/* Selection rectangle, in content coordinates. With Shift, the
	 * items it covers toggle from the selection it started with. */
	bool m_marquee = false;
	bool m_marqueeExtend = false;
	QPoint m_marqueeStart, m_marqueeEnd, m_marqueePointer;
	QSet<Item *> m_marqueeBase;
	QTimer m_marqueeScroll;
	std::vector<Item *> visibleItems();
	QRect selectionRect(Item *item);
	bool listSelectableAt(QPoint windowPos, Item *item);
	void updateMarquee(QPoint windowPos);
	void endMarquee();

	/* Renaming (icon view): a click on a selected name, held still. */
	LabelEditor m_editor{ [this] { update(); } };
	QTimer m_renameTimer;
	Item *m_renameItem = nullptr;
	void beginRename(Item *item);
	QTimer m_typeSelectTimer;
	QString m_typeSelect;

	/* Scroll bar tracking. */
	bool m_sbVertical = true;
	sb_part m_sbPart = SB_NONE;
	int m_thumbGrab = 0; /* pointer offset within the thumb */
	QTimer m_repeat;

	/* ⌘-drag scrolls with the grabber hand. Clients see ⌘ and Control
	 * both as Ctrl, so a Ctrl-press waits: moved, it grabs; released
	 * still, it was a Control-click and shows the contextual menu. */
	bool m_grabPending = false;
	bool m_grabbing = false;
	QPoint m_grabPress, m_grabScroll;
	void contextClick(QPoint pos);
};

/* ⌘-key equivalents shared by Finder windows and the desktop. Linux apps
 * see ⌘ as Ctrl (zacos9-wm translates it). Returns true if handled. */
bool finderShortcut(QKeyEvent *e);
