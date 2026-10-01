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
};

/*
 * The Finder's application logic: which view is in front, the commands
 * behind its menus, and the channel to the menu bar.
 *
 * Menu bar protocol (newline-separated text over a Unix socket at
 * $XDG_RUNTIME_DIR/platinum-finder.$WAYLAND_DISPLAY.sock):
 *   menu bar -> Finder:  "cmd <name>"   (new-folder, open, close-window,
 *                         move-to-trash, empty-trash, ...)
 *   Finder -> menu bar:  "state selection=<n> window=<0|1> trash=<0|1>
 *                         view=<0 icons|1 list|2 buttons>"
 */
class Finder {
public:
	static Finder &instance();
	static QString socketPath();

	void start(Desktop *desktop);
	void setFront(FinderView *view);
	void viewClosed(FinderView *view);
	FinderView *front();

	/* Commands (from menus or ⌘-keys). */
	void command(const QString &name);
	void newFolder();
	void openSelection();
	void closeWindow();
	void moveSelectionToTrash();
	void emptyTrash();
	void getInfo();
	void duplicate();
	void makeAlias();
	void putAway();
	void showOriginal();

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
