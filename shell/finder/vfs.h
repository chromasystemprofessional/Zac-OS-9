#pragma once

#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <vector>

#include "icons.h"
#include "items.h"

/*
 * The Macintosh view of this computer.
 *
 * The Finder shows a startup disk holding a System Folder, Applications
 * and Documents, instead of Debian's Unix hierarchy. Nothing on disk is
 * moved, renamed or hidden to achieve it: this is a model the Finder
 * draws from, and /usr, /etc and the rest are untouched and still
 * reachable (see vfsUnixVolumeShown).
 *
 * Virtual paths look like "vfs:/system-folder/fonts". They name nodes by
 * a stable id, never by display name, so renaming an item keeps its
 * window, its icon's place and its remembered layout. Ids also survive
 * package upgrades, because an application's id is its desktop file ID.
 *
 * A node either holds children of its own (curated or generated) or
 * stands for a real directory. Opening one that stands for a real
 * directory opens that directory, and from there everything is an
 * ordinary file: the virtual layer is only as deep as the curated part.
 */

enum class VKind {
	Volume,     /* the startup disk itself */
	Folder,     /* a curated folder; children come from the registry */
	Backed,     /* stands for a real directory */
	Apps,       /* generated: the installed applications */
	Panels,     /* generated: Platinum's control panels */
	AppFolder,  /* generated: one application's folder */
	Launcher,   /* generated: starts an application or a control panel */
	Unix,       /* the real filesystem root, for inspecting Debian */
};

struct VNode {
	QString id;      /* stable and unique; '/' separates it from its parent */
	QString name;    /* what the Finder shows */
	VKind kind = VKind::Folder;
	QString backing; /* Backed: the real directory it stands for */
	pl_icon_kind icon = PL_ICON_FOLDER;
	bool visible = true;
	int order = 0;   /* ties in the Finder's by-name order */
	/* Apps: keep only these XDG categories (empty means all), then drop
	 * anything in `excludeCategories`. */
	QStringList categories, excludeCategories;
	/* Launcher: an application's desktop file ID, and its Desktop Action
	 * if this is one; or a program of ours to run, with its arguments. */
	QString appId, actionId, program;
	QStringList args;

	QString parentId() const {
		const int slash = id.lastIndexOf('/');
		return slash < 0 ? QString() : id.left(slash);
	}
};

/* Virtual paths. "vfs:/" is the startup disk. */
bool vfsIsVirtual(const QString &path);
QString vfsRoot();
QString vfsPathFor(const QString &id);
QString vfsIdOf(const QString &path);

/* The node at `path`, or nullptr if there isn't one. */
const VNode *vfsNode(const QString &path);
/* `path`'s children as Finder items, in the Finder's by-name order. */
std::vector<std::unique_ptr<Item>> vfsList(const QString &path);
/* What `path` shows when opened: a real directory for a Backed node (it
 * is created if missing), else empty, meaning a virtual window. */
QString vfsOpensAs(const QString &path);
/* The name the Finder shows for a virtual path. */
QString vfsName(const QString &path);
/* Starts the application or control panel a Launcher node names. */
bool vfsLaunch(const QString &path);
/* The real file or directory this item stands for, for Get Info: a
 * Backed node's directory, or an application's desktop entry. Empty for
 * the curated and generated folders, which stand for nothing on disk. */
QString vfsRealCounterpart(const QString &path);

/* Renames and labels on virtual items are metadata: they are kept in the
 * registry, so they outlast package upgrades and reboots. Both return
 * false when the item can't carry them. */
bool vfsRename(const QString &path, const QString &newName);
/* Does a sibling of `path` already show this name? The Finder won't let
 * two items in one folder share a name. */
bool vfsNameTaken(const QString &path, const QString &name);
int vfsLabel(const QString &path);
bool vfsSetLabel(const QString &path, int label);

/* What the Finder will allow. Curated folders and package-managed
 * applications can be renamed but never moved, copied or thrown away,
 * so no Finder gesture can damage what a package owns. */
bool vfsCanRename(const QString &path);
bool vfsCanDelete(const QString &path);
/* Is `path` an application's folder in Applications (or Utilities)? The
 * only virtual item the Trash gesture does anything to. */
bool vfsIsAppFolder(const QString &path);
/* Removes an application's folder from the Finder: metadata only, kept
 * in the registry like a rename. The package is never touched, nothing
 * is uninstalled, and the application comes back as soon as the
 * registry's override is cleared (vfsShowAllHidden) or edited by hand. */
bool vfsHideApplication(const QString &path);
/* Brings back every application hidden this way. */
void vfsShowAllHidden();
/* Can items be dropped into this virtual folder? Only a Backed one. */
bool vfsAcceptsDrops(const QString &path);

/* The real filesystem, shown as a second disk for inspecting Debian.
 * Off by default, so the Finder's ordinary views never show Unix. */
bool vfsUnixVolumeShown();
void vfsSetUnixVolumeShown(bool shown);
/* The startup disk's name and icon, for the desktop. */
QString vfsVolumeName();

/* Re-read the registry and the installed applications. */
void vfsRefresh();
/* Call `f` when the virtual hierarchy changes (an application was
 * installed or removed, or the registry was edited). Returns a token:
 * pass it to vfsOffChange() before the receiver goes away. */
int vfsOnChange(std::function<void()> f);
void vfsOffChange(int token);
/* Where the registry lives; it is created with the default mapping the
 * first time the Finder runs. */
QString vfsRegistryPath();
