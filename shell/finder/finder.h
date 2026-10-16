#pragma once

#include "contextitems.h"
#include <QMouseEvent>

#include <QLocalServer>
#include <QTimer>
#include <QLocalSocket>
#include <QPointer>
#include <QString>
#include <vector>

#include "items.h"

class Desktop;
class FolderWindow;
class QWidget;

/* Something showing a folder's icons: a Finder window or the desktop. */
/* Control-click (the Mac's way) or a right-click: a contextual menu.
 * ⌘ reaches us as Control too, so ⌘-click opens one as well. */
inline bool isContextClick(const QMouseEvent *e) {
	return e->button() == Qt::RightButton ||
		(e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier));
}

class FinderView {
public:
	virtual ~FinderView() = default;
	virtual QString folderPath() const = 0;
	virtual std::vector<Item *> selectedItems() = 0;
	virtual void selectByName(const QString &name) = 0;
	virtual void reload() = 0;
	virtual QWidget *widget() = 0;
	/* An item here was renamed: keep its remembered icon position. */
	virtual void itemRenamed(const QString &, const QString &) {}
	/* View > Clean Up and View > Arrange (icon views only). */
	virtual void arrange(Arrange) {}
	/* Edit > Select All: every item shown in the view. */
	virtual void selectAll() {}
};

/*
 * The Finder's application logic: which view is in front, the commands
 * behind its menus, and the channel to the menu bar.
 *
 * Menu bar protocol (newline-separated text over a Unix socket at
 * $XDG_RUNTIME_DIR/zacos9-finder.$WAYLAND_DISPLAY.sock):
 *   menu bar -> Finder:  "cmd <name>"   (new-folder, open, close-window,
 *                         move-to-trash, empty-trash, ...)
 *   Finder -> menu bar:  "state selection=<n> window=<0|1> trash=<0|1>
 *                         view=<0 icons|1 list|2 buttons>
 *                         label=<the selection's common label, or -1>
 *                         erase=<0|1 selected mounted USB disk>
 *                         network=<0|1 selected network disk>
 *                         unmount=<0|1 selected local mounted disk>
 *                         sort=<column> columns=<bits> custom=<0|1>
 *                         popup=<0|1> popups=<0|1>
 *                         documents=<0|1 selection has documents to print>"
 *   and before "context <kind>" (a contextual menu, at the pointer):
 *                        "tricks <0|1 a folder to attach to>" then TAB "+name"
 *                         per trick attached, TAB "*name" per trick there is;
 *                        "commands" then TAB "name" per Contextual Menu Item
 *   menu bar -> Finder:  "cmd attach-trick NAME", "cmd remove-trick NAME",
 *                        "cmd open-folder-tricks", "cmd contextual INDEX"
 */
class Finder {
public:
	static Finder &instance();
	static QString socketPath();

	void start(Desktop *desktop);
	/* A new desktop surface took over (the screen's size or scale changed). */
	void replaceDesktop(Desktop *desktop);
	void setFront(FinderView *view);
	void viewClosed(FinderView *view);
	FinderView *front();
	/* Option-Command-W: close the Finder's open windows, not its desktop. */
	void closeAllWindows();

	/* Commands (from menus or ⌘-keys). */
	void command(const QString &name);
	void newFolder();
	/* Edit > Copy in the Sniffer copies the selected names as text. */
	void copySelectionNames();
	void openSelection();
	/* Opens just this item, whatever else is selected: what a double-click
	 * means, so two icons selected by accident aren't both opened. */
	void openItem(Item *item);
	/* Option-double-click on an application icon closes its front window. */
	bool closeApplicationWindow(Item *item);
	void closeWindow();
	void moveSelectionToTrash();
	void emptyTrash(bool removeLocked = false);
	void getInfo(bool sharing = false);
	void duplicate();
	void makeAlias();
	void putAway();
	/* File > Index Selection: the selected folders (or the front window's)
	 * into the search index, for Find's contents searches. */
	void indexSelection();
	void unmountSelection(bool network);
	/* A contextual menu (Control-click or right-click) on `hit` in
	 * `view`, or on its background: the menu bar opens it at the pointer,
	 * from the state just sent. */
	void showContextMenu(FinderView *view, Item *hit);
	/* ⌘-click on a folder window's title: the folder and those enclosing
	 * it, as a menu at the pointer (the menu bar draws it). Choosing one
	 * opens it; with `option`, the window clicked closes. */
	void showPathMenu(FolderWindow *window, bool option);
	/* ⌘-↑ (Option-⌘-↑ also closes the front window): the enclosing folder. */
	void openEnclosing(bool closeThis);
	/* A folder's window, the Macintosh view's folders through their real
	 * counterparts where they have one. */
	void openFolder(const QString &path);
	/* File > Add To Favorites: an alias of each selected item in Favorites. */
	void addToFavorites();
	/* Open a file, folder, disk or alias by path, as a double-click does
	 * (the Zac menu's Favorites and Recent Documents). */
	void openPath(const QString &path);
	/* Special > Eject (⌘E): every selected disk, local or network. */
	void ejectSelection();
	/* Special > Log Out (⇧⌘Q): asks, then has the menu bar quit the
	 * programs and end the session. */
	void logOut();
	void showOriginal();
	void setLabel(int label);
	/* Start classic Mac OS (shell/classic/zacos9-classic), with extra
	 * disk images; explains in an alert when ROM or emulator is missing. */
	void launchClassic(const QStringList &disks = {});
	/* File > Print (⌘P): the selected documents, on your printer. */
	void printSelection();
	/* File > Print Window… / Print Desktop…: the front window's contents
	 * as pages (Page Setup's paper, orientation and scale). */
	void printWindow();
	void encryptSelection();
	void decryptSelection();
	/* Documents printed on a queue (your printer's if ""): what CUPS can
	 * print itself goes there; the rest is explained. */
	void printDocuments(const QStringList &files, const QString &queue = {});
	/* A desktop printer's window: zacos9-printqueue. */
	void openPrintQueue(const QString &queue);
	/* Open a .exe or .msi in the Windows Installer. */
	void launchWindows(const QString &exe);
	/* A supported archive expanded beside itself (zacos9-expand), with a
	 * status dialog; what came out is then selected. */
	void expandArchive(const QString &archive);
	void mountMacImage(const QString &image);

	/* Spring-loaded folders: hovering a drag over a folder opens it; the
	 * windows that sprang open close again when the drag ends. Each view
	 * calls springHover() from its drag handlers. */
	void springHover(const QString &folderOrEmpty);
	/* Open the currently hovered spring-loaded folder immediately. */
	bool springOpenNow();
	void dragEnded();

	/* Re-read every view showing `folder`, and the Trash icon. */
	void folderChanged(const QString &folder);
	/* Selection or front view changed: tell the menu bar. */
	void notifyState();
	/* Put Away a disk; the desktop's Trash also ejects network volumes. */
	void ejectVolume(QString path, QString name, bool network);

private:
	Finder() = default;
	QString stateLine();

	Desktop *m_desktop = nullptr;
	/* The folder-path menu on show: where it came from, and its folders. */
	struct {
		QPointer<QWidget> from;
		QStringList folders;
		bool option = false;
	} m_pathMenu;
	QString m_springPath;
	QTimer *m_springTimer = nullptr;
	std::vector<QPointer<QWidget>> m_sprung;
	FinderView *m_front = nullptr;
	QLocalServer m_server;
	std::vector<QPointer<QLocalSocket>> m_clients;
	QString m_lastState;
	/* What the last contextual menu was for: the paths its add-on
	 * commands get, and the folder Folder Tricks attach to. */
	void sendContextExtras(FinderView *view, Item *hit, const char *kind);
	QStringList m_contextPaths;
	QList<ContextCommand> m_contextCommands;
	QString m_trickFolder;
};
