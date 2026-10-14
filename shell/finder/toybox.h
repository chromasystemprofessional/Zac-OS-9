#pragma once

#include <QFileSystemWatcher>
#include <QSettings>
#include <QWidget>
#include <memory>
#include <vector>

#include "items.h"
#include "widgets.h"

/*
 * The Toy Box: Mac OS 9's Launcher (Missing Manual pp. 43-46). A window of
 * big buttons that open with one click: aliases kept in System Folder >
 * Toy Box Items. Folders in there whose names start with a bullet ("•Games")
 * are pages, chosen with the topic buttons along the top; everything else
 * is on the Applications page. Drop icons on it to add them (an alias is
 * made), Option-drag a button out (to the Trash, say) to take it away, and
 * ⌘-click or Control-click it for the button size: Small, Medium or Large.
 */
class ToyBox : public QWidget {
public:
	/* Opens it, or brings it forward. */
	static ToyBox *open();
	~ToyBox() override;

	struct Page {
		QString name;   /* shown on its topic button */
		QString folder;
	};
	/* The pages in `root`: the bulleted folders, by name, with the
	 * Applications page (the loose items) first when there are any, or no
	 * bulleted folders at all. Eight at most, as on the Mac. */
	static std::vector<Page> pagesIn(const QString &root);
	/* Adds aliases of `targets` ({name, original path}) to `folder`. */
	static void addAliases(const std::vector<std::pair<QString, QString>> &targets,
		const QString &folder);

	enum class ButtonSize { Small, Medium, Large };

protected:
	void paintEvent(QPaintEvent *) override;
	void resizeEvent(QResizeEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void wheelEvent(QWheelEvent *) override;
	void keyPressEvent(QKeyEvent *) override;
	void dragEnterEvent(QDragEnterEvent *) override;
	void dragMoveEvent(QDragMoveEvent *) override;
	void dropEvent(QDropEvent *) override;
	void closeEvent(QCloseEvent *) override;

private:
	ToyBox();
	void reload();
	void layout();
	void showPage(int page);
	void setButtonSize(ButtonSize size);
	void sizeMenu(QPoint at, Item *item);

	struct Metrics {
		int side, icon, cellW, cellH;
		pl_font font;
	};
	Metrics metrics() const;
	int topicsHeight() const;
	QRect topicRect(int page) const;
	int topicAt(QPoint p) const;
	QRect buttonRect(const Item *item) const; /* window coordinates */
	Item *itemAt(QPoint p) const;
	int contentHeight() const;
	int viewHeight() const;
	void scrollTo(int y);
	pl_scrollbar scrollBar() const;
	QString dropFolder(QPoint p) const;

	QSettings m_settings;
	QFileSystemWatcher m_watcher;
	std::vector<Page> m_pages;
	int m_page = 0;
	ButtonSize m_size = ButtonSize::Medium;
	std::vector<std::unique_ptr<Item>> m_items;
	int m_scroll = 0;
	int m_columns = 1;
	Item *m_down = nullptr;   /* a button being clicked */
	bool m_inside = false;
	Item *m_dragItem = nullptr; /* Option-pressed: may be dragged out */
	QPoint m_pressPos;
	int m_dropTopic = -1;     /* a topic button a drag is over */
	int m_thumbGrab = -1;
};
