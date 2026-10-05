#pragma once

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
 *                         erase=<0|1 selected mounted USB disk>"
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

	/* Commands (from menus or ⌘-keys). */
	void command(const QString &name);
	void newFolder();
	void openSelection();
	/* Opens just this item, whatever else is selected: what a double-click
	 * means, so two icons selected by accident aren't both opened. */
	void openItem(Item *item);
	void closeWindow();
	void moveSelectionToTrash();
	void emptyTrash();
	void getInfo(bool sharing = false);
	void duplicate();
	void makeAlias();
	void putAway();
	void showOriginal();
	void setLabel(int label);
	/* Start classic Mac OS (shell/classic/zacos9-classic), with extra
	 * disk images; explains in an alert when ROM or emulator is missing. */
	void launchClassic(const QStringList &disks = {});
	/* Open a .exe or .msi in the Windows Installer. */
	void launchWindows(const QString &exe);
	/* A .zip or .tar.* expanded beside itself (zacos9-expand), as StuffIt
	 * Expander did on a double-click; what came out is then selected. */
	void expandArchive(const QString &archive);

	/* Spring-loaded folders: hovering a drag over a folder opens it; the
	 * windows that sprang open close again when the drag ends. Each view
	 * calls springHover() from its drag handlers. */
	void springHover(const QString &folderOrEmpty);
	void dragEnded();

	/* Re-read every view showing `folder`, and the Trash icon. */
	void folderChanged(const QString &folder);
	/* Selection or front view changed: tell the menu bar. */
	void notifyState();

private:
	Finder() = default;
	QString stateLine();

	Desktop *m_desktop = nullptr;
	QString m_springPath;
	QTimer *m_springTimer = nullptr;
	std::vector<QPointer<QWidget>> m_sprung;
	FinderView *m_front = nullptr;
	QLocalServer m_server;
	std::vector<QPointer<QLocalSocket>> m_clients;
	QString m_lastState;
};
