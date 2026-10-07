# The Macintosh view of this computer

The Finder shows a startup disk holding a System Folder, Applications and
Documents. Debian's own hierarchy is not moved, renamed or hidden to
achieve that: `/usr`, `/etc`, `/bin`, `/var`, `/lib`, `/proc` and `/sys`
are exactly where the system put them, and every other program sees them
as it always did. What changes is only what the Finder draws.

The model lives in [`shell/finder/vfs.h`](../shell/finder/vfs.h); the
application list it draws on is in
[`shell/finder/appdb.h`](../shell/finder/appdb.h). No FUSE filesystem is
mounted: nothing outside the Finder needs this view, so nothing outside
the Finder pays for it.

## Desktop files and aliases

The Desktop is **not** part of the virtual Macintosh hierarchy. It displays
the real XDG Desktop directory (`~/Desktop` by default), alongside the startup
disk, mounted disks and Trash icons. Files saved there by other applications
appear automatically; New Folder, renaming, copying and moving work on the
same files.

File > Make Alias creates a filesystem symbolic link. Drag an application
or a backed folder from the Macintosh view to Desktop to create an alias
without moving its package or original directory. For ordinary files and
folders, Command-Option-drag creates an alias; Option-drag copies. Aliases can
also be moved between real folders and Desktop. Copying an alias preserves
the link, and moving it to Trash does not delete the original.
Aliases inherit the resolved original's icon, including application icons in
both icon and list views, while retaining their own name and italic alias label.
Alias chains use the final target's icon; missing or looping targets show a
generic document icon.

Folder aliases open as folders and accept file drops. In a standard application
save dialog, choose Desktop and open a folder or folder alias to save inside
it. Saves through an alias go into its target directory. An alias does not
track a target that is renamed/moved, unlike a classic Mac alias record; a
disconnected or deleted original must be reconnected or the alias recreated.
Finder reports unavailable originals rather than creating empty replacement
folders.

At login, `zacos9-desktop-places` registers Desktop with XDG user directories
and GTK bookmarks. Existing Desktop paths, user files, configuration entries
and bookmark labels are preserved. Qt dialogs using the ZacOS style add the
same Desktop location without replacing their other places; portal file
choosers use the GTK backend. Registration errors are reported without
preventing login. Applications with their own custom file pickers may still
need navigation through Home to Desktop.

## File-transfer status

Copying, moving, Option-drag copying and File > Duplicate use a movable
Platinum status dialog showing the current filename, destination, progress
bar and percentage. Folder copies include their contents and hidden files;
aliases remain links and classic resource-fork companions travel with their
files. Preparing/counting and copying run off the GUI thread so the dialog
continues to respond.

Choose **Stop**, press Escape or Command-period to stop a batch. Completed
items stay in the destination. The current incomplete copy is removed, and
its original is not deleted. Same-disk moves are renames: an individual
rename finishes atomically, then Stop prevents further items from moving.
Cross-disk drags retain their copy semantics. Closing the progress window
also requests Stop and waits for cleanup rather than abandoning the worker.
Unreadable files, name conflicts and cleanup failures produce explicit
alerts; existing destination items are never overwritten.

Finder-originated drops release their drag grab before the progress dialog
opens so its Stop button can receive input. External drops are not reported
as successfully completed if a transfer is stopped or fails.

## What you see

```
Zacintosh HD
├── System Folder
│   ├── Appearance        → ~/.local/share/zacos9/appearance
│   │   ├── Desktop Patterns → custom user-created tiles and classic Mac pattern files
│   │   ├── Wallpaper     → ~/.local/share/zacos9/appearance/Wallpaper
│   │   ├── Themes        → compatible appearance preset JSON files
│   │   └── Sound Themes  → custom WAV themes and classic resource-fork sound sets
│   ├── Control Panels      (generated: ZacOS 9's control panels)
│   ├── Extensions         (generated, read-only driver/module status)
│   ├── Sounds             (generated, read-only aliases to user/system sounds)
│   ├── Startup Items      (generated, read-only enabled autostart entries)
│   ├── Application Support (generated, read-only aliases to support resources)
│   ├── Device Drivers     (generated, read-only loaded kernel modules)
│   ├── Network            (generated, read-only interface status)
│   ├── System Logs        (generated, protected informational view only)
│   ├── Software Components (generated, read-only installed app/package metadata)
│   ├── System Information (generated launcher, read-only status window)
│   ├── Extensions Manager (generated launcher, read-only status window)
│   ├── Fonts             → ~/.local/share/fonts
│   ├── Preferences       → ~/.config
│   └── Utilities           (generated: settings, file managers and system tools,
│                            including shipped tools such as balenaEtcher)
├── Applications            (generated: user-installed applications)
│   └── Mousepad
│       ├── Mousepad        launches it
│       └── Preferences     one of its Desktop Actions
├── Home                  → ~ (Documents, Downloads, Desktop, everything of the user's)
├── (folders you make: File > New Folder in the disk's window; each is a real
│    directory under ~/.local/share/zacos9/finder/folders; Trash removes an empty one)
└── Utilities               (generated, off by default)
```

A folder with an arrow stands for a real directory. Opening one opens
that directory, and from there everything is an ordinary file: the
virtual layer is only as deep as the curated part. The directory is
created the first time it is opened if it isn't there yet.

Desktop Patterns is created automatically by the desktop and Appearance panel.
Registry versions 5 and 6 add Desktop Patterns and the separate Wallpaper
backed nodes to existing registries without removing user changes. Version 7
moves Wallpaper under Appearance, retaining node overrides and existing files.
If the old folder cannot be moved (including an existing destination), the
catalog retains the old folder and logs the conflict rather than overwriting files.
Version 8 adds Themes and Sound Themes under Appearance.
Version 9 moves the shipped Utilities folder into System Folder and preserves
its custom name and the renamed titles/labels of system launchers. Version 10
adds read-only System Folder resource providers (Sounds, Startup Items,
Application Support, Device Drivers, Network, System Logs, Software Components
and Extensions) and built-in launchers for System Information and Extensions
Manager.
See
[custom desktop patterns](../README.md#custom-desktop-patterns) and
[desktop wallpaper](../README.md#desktop-wallpaper) for supported formats,
placement choices and import limits.

## How applications are found

Through GIO, so the desktop entry specification is followed rather than
approximated:

- XDG data directory precedence: an entry in `$XDG_DATA_HOME` replaces
  the one of that id in `/usr/share`, and only one of them shows.
- `Hidden=true` and `NoDisplay=true` entries are left out.
- `OnlyShowIn` / `NotShowIn` are checked against `$XDG_CURRENT_DESKTOP`,
  which the session sets to `ZacOS9`.
- An entry whose `TryExec` names a program that isn't installed is left
  out — and so, GIO's own rule, not ours, is one whose plain `Exec`
  names a program that can't be found at all, even with no `TryExec`.
- `Exec` field codes, `Path`, `Terminal` and `DBusActivatable` are
  honoured at launch, by GIO. **`Exec` is never handed to a shell.**
- Desktop Actions appear beside the application in its folder.
- Localized names and comments: GIO reads the one for the current
  locale (`Name[de]=`, and the rest) the same way it reads everything
  else about the entry.

Flatpak and Snap applications appear with the rest, because they install
desktop entries like everything else. A package with several desktop
entries gets a folder for each: one package is not assumed to be one
application. Debian desktop-file ownership determines installation status
and uninstall identity, not how many application entries a package supplies.

An application's identity is its **desktop file ID** (`mousepad.desktop`,
`org.gnome.Nautilus.desktop`), never its display name. Names are
translated, change between releases, and repeat between packages. When
two entries do share a name, each is told apart by its id — "Files
(org.gnome.Nautilus)" beside "Files (nemo)".

Nothing is copied. An application's folder and the launcher in it are
entries in this model; the package's files stay where dpkg put them.

### User applications versus system utilities

Applications excludes the desktop-entry baseline recorded on the installation
image and Debian packages marked `Auto-Installed: 1` in
`/var/lib/apt/extended_states`. Package ownership is checked against the desktop
file itself, not the executable that may merely wrap or launch another app.
Explicitly installed Debian apps, user-local launchers, Wine shortcuts and
Flatpak exports remain eligible. Changes to apt's automatic/manual marks
refresh the view without requiring a new launcher file or a login restart.

Entries with `System`, `Settings`, `FileManager` or `X-ZacOS9-Utility`
categories appear only in **System Folder > Utilities**, even when explicitly
installed, present on the original image or installed as dependencies. This
includes qps, PCManFM, desktop/screen-saver settings, print settings and shipped
tools such as balenaEtcher. The ordinary `Utility` category alone does not move
an application: calculators and other user utilities stay in Applications.
ZacOS's own control panels remain in Control Panels.

This uses installation metadata, not a historical record of user intent:
`apt-mark manual` makes a non-baseline application eligible, while packages
marked manual by another installer are indistinguishable from explicit user
installs. A dependency can provide several launchers; the filter applies to
its package, not to a translated application name. No packages are removed,
and system tools remain launchable from their new virtual folder.

### Icon

The application's own icon is resolved through the freedesktop icon
theme spec (`QIcon::fromTheme`, tried against each name GIO offers in
its own preference order — the icon itself, then a symbolic fallback),
rendered to 32x32 and 16x16 straight-alpha ARGB and drawn with real alpha
blending, not the 1990s icon set's plain on/off transparency. Nothing is
cached to disk: the cost is one theme lookup per application per process
lifetime, paid again only when an application is newly discovered.

When nothing resolves — no icon named, the name doesn't exist in any
theme, or the process has no GUI platform to rasterize into at all (a
headless tool, not the Finder itself) — the generic application icon is
drawn instead. A Desktop Action always keeps the generic document icon:
it isn't the application itself.

### Where it came from

For Get Info, once there is one (see "What the Finder will and won't
do" below): the package owning the desktop file, or, for an unowned launcher,
the application's own executable (`dpkg -S` on its canonical path, resolving the symlinks a
merged-`/usr` system has — `dpkg`'s file database has `/usr/bin/x`, not
the `/bin/x` most `Exec=` lines actually name), then its version
(`dpkg-query`). Looked up once per executable path and kept for the life
of the process.

A Flatpak export is read from its own `X-Flatpak` key instead: its
`Exec=` runs the `flatpak` program, and attributing the application to
whatever package owns *that* would be wrong, not merely approximate.
Neither is guessed when there is nothing to find it from (a desktop
entry dropped in by hand, with an executable dpkg doesn't know).

## The registry

`~/.local/share/zacos9/finder/vfs.json`, written with the default
mapping the first time the Finder runs, and yours to edit after that.

The startup disk's name: `volume.name` once the user renames it; until then
the name it was installed with (`/etc/zacos9/disk-name`, written by
`zacos9-install`), else "Zacintosh HD".

```json
{
  "version": 1,
  "volume": {},
  "showUnixVolume": false,
  "nodes": [
    { "id": "system-folder", "name": "System Folder", "kind": "folder",
      "icon": "system-folder", "order": 0 },
    { "id": "system-folder/fonts", "name": "Fonts", "kind": "backed",
      "backing": "$XDG_DATA_HOME/fonts" },
    { "id": "applications", "name": "Applications", "kind": "apps", "order": 1 },
    { "id": "utilities", "name": "Utilities", "kind": "apps", "visible": false,
      "categories": ["Utility", "System", "Settings"] }
  ],
  "overrides": {}
}
```

A node's fields:

| Field | What |
|---|---|
| `id` | Stable and unique. `/` separates it from its parent, so `system-folder/fonts` sits in `system-folder`. |
| `name` | What the Finder shows. |
| `kind` | `folder` (curated), `backed` (real directory), `apps`, `panels`, `resource`, `launcher`, `unix`. |
| `backing` | For `backed`. `$HOME`, `$XDG_CONFIG_HOME`, `$XDG_DATA_HOME` and `$XDG_DOCUMENTS_DIR` are expanded. |
| `provider` | For `resource`: fixed provider id (`sounds`, `startup-items`, …), never a command or arbitrary path. |
| `action` | For built-in `launcher` rows: trusted internal actions (`system-information`, `extensions-manager`) only. |
| `icon` | `folder`, `system-folder`, `control-panels`, `disk`, `application`, `document`. |
| `visible` | `false` hides it without deleting it. |
| `order` | Where it sits among its siblings before the user moves it; ties go by name. |
| `categories`, `excludeCategories` | For `apps`: keep only these XDG categories, then drop these. |

Ids, not names, are what everything else refers to: a window, a
remembered icon position and a label all follow the id. So renaming an
item leaves its window where it was and its icon where you put it, and a
package upgrade that changes an application's display name doesn't
scatter your desktop.

### Things you might want to change

- **Rename the disk**: `"volume": { "name": "Work HD" }`.
- **Point Documents elsewhere**: change that node's `backing`.
- **Show Utilities**: set its `visible` to `true`. To keep the same
  application out of both, give `applications` the matching
  `"excludeCategories": ["Utility", "System", "Settings"]`.
- **Add a folder of your own**: add a node with a new `id`; migration
  keeps it.

### Overrides

`overrides` holds what the Finder itself writes: renames, labels, and
items hidden from a menu. It is keyed by node id, so it also covers
generated nodes, which aren't in `nodes` at all.

```json
"overrides": {
  "applications/mousepad.desktop/launch": { "name": "Text Editor", "label": 2 }
}
```

Overrides are applied last and win over everything. An override left
behind for an application that has since been removed is ignored, and
starts working again if that application comes back.

## Migration

The registry carries a `version`. When the Finder finds an older one it
adds the nodes the running version expects, keeps every node and
override already there, writes the new version back, and leaves the file
otherwise as it was. Nothing you added is dropped, and a node you deleted
on purpose comes back only if a later version introduces it again. A file
that is missing or unreadable is replaced with the default mapping.

## Refreshing

The Finder watches the XDG application directories. A change settles for
a moment before it is read, because installing a package writes many
files and because GIO drops its own cached list only once its file
monitors have been served. `Finder` also takes a `refresh` command for
when something appears without the directories changing in a way the
watcher sees.
Missing launcher directories are monitored through their nearest existing
ancestor, so the first Flatpak installation can create its export tree after
Finder starts and still appear automatically. GIO application-list change
notifications also trigger the same settled refresh. Newly created icon-theme
directories are added when the application list reloads. No launcher copies or
login restart are needed; XDG precedence and hidden-entry rules still apply.

System Folder resource providers are lazily enumerated when opened. They do not
recursively scan `/proc`, `/sys` or `/usr/lib` during ordinary navigation.
Provider identities are allowlisted in code; registry rows can select only known
providers and fixed built-in actions.

## Read-only resource providers

Resource providers are virtual collections and informational entries, not copies
of system files. They support three kinds of entries:

- **Filesystem alias**: read-only alias to an allowlisted real source.
- **Virtual collection**: generated grouping with no single source path.
- **Information item**: metadata/status with no openable file source.

For safety, provider aliases are canonical-path checked against allowlisted
roots, reject broken/out-of-root/looping symlinks, and never expand into
write-enabled virtual folders. Operations such as rename, label, drop and trash
are disabled for provider items.

Current providers:

- **Sounds**: user and system sound roots (`$XDG_DATA_HOME/sounds`,
  `/usr/share/sounds`) as read-only aliases.
- **Startup Items**: enabled autostart desktop entries from supported startup
  directories; command text is not exposed.
- **Application Support**: allowlisted user/system support roots as read-only
  aliases.
- **Device Drivers**: every loaded kernel module, with startup-module metadata,
  informational only.
- **Extensions**: a curated view of the same modules
  (`shell/finder/extensioncatalog.cpp`). Each entry gives a friendly name, an
  icon and a one-line description, and groups the modules behind it (for
  example "HDMI Sound" covers `snd_hda_codec_hdmi`). Only entries with a loaded
  module appear. Helper libraries and plumbing modules are left out on purpose;
  they still show up under Device Drivers.
- **Network**: current interface status and addresses, informational only.
- **System Logs**: protected informational notice only; raw log content is not
  exposed here.
- **Software Components**: installed application/component metadata from desktop
  entries and package ownership.

Built-in launchers:

- **System Information**: read-only summary of ZacOS version, distro, kernel,
  CPU, memory, storage, network, audio, loaded drivers and uptime.
- **Extensions Manager**: read-only list of the curated Extensions, with
  icons, a description and status/startup/module details for the selection.
  **Show All** switches to every loaded kernel module by technical name.
  Nothing here loads or unloads modules.

## What the Finder will and won't do

| | Curated folder | Application folder and its launcher | `backed` folder |
|---|---|---|---|
| Open | yes | launches the application | opens the real directory |
| Get Info | no window yet | its desktop entry | the real directory |
| Rename | yes, kept in the registry | yes, kept in the registry | ordinary rename |
| Label | yes, kept in the registry | yes, kept in the registry | extended attribute |
| Move, copy, duplicate, alias | no | no | ordinary |
| Throw away | no | queues a supported application for uninstall (next section) | ordinary |
| Take a drop | no | no | yes |
| New folder inside | no | no | yes |

Package-owned launchers are never moved or deleted directly. Debian uninstall
uses the existing privileged software helper; other browsing and metadata
actions write only into the user's own directories.

A Desktop Action can't be renamed on its own, because it isn't a thing of
its own: it belongs to the application's desktop entry.

Get Info reads a file, so it shows the one the item stands for: an
application's desktop entry, or a `backed` folder's real directory. The
curated folders stand for nothing on disk and have no Get Info window
yet.

## Uninstalling applications through Trash

**Move To Trash**, or dragging an application from Applications onto
Trash or into its window, queues it for uninstall. It remains installed but
disappears from Applications, with its name and icon displayed in Trash.
Queue records persist in the user's Trash across logins. No package-owned
launcher is moved, and application aliases remain ordinary links: trashing an
alias does not queue or uninstall its original.

**Put Away**, or dragging a queued app back to Applications, cancels removal
and restores its launcher. **Empty Trash** asks for confirmation listing queued
app names and install sources, then shows progress while uninstalling them.
Debian uses the existing software helper's `remove` operation, not `purge`;
user Flatpak uses `flatpak --user uninstall` without `--delete-data`. Personal
data is retained. A Debian package can own several applications or have
dependents, so removal may remove those as well. System-wide Flatpak apps and
unsupported/unowned apps are not queued; Finder reports how to remove them
instead.
System utilities and automatically installed dependencies cannot be queued
for uninstall through Finder. System Folder > Utilities also does not accept
application-installation drops.

Failed uninstalls leave their records in Trash for retry or Put Away, and stop
emptying before ordinary files are deleted. Completed removals are not undone
if a later app fails. A changed install source or invalid queue record blocks
uninstall with an explicit error. Queued entries cannot be renamed or copied
as ordinary files. New app queue entries created during emptying require a
new confirmation.

**Special > Show All Applications** still restores legacy hidden overrides
from earlier versions, but does not cancel the uninstall queue. Renames and
labels remain intact when an app is put away or later reinstalled.

## Looking at Debian

`"showUnixVolume": true` puts a second disk, "Unix", on the desktop,
which opens `/` as ordinary files. It is off by default, so the Finder's
ordinary views never show the Unix hierarchy. The Finder also takes a
`show-unix-volume` command, which turns it on and off.

## Errors

- An application that is removed between being listed and being opened
  gives an alert saying so; its folder disappears at the next refresh.
- A registry that won't parse is replaced by the default mapping rather
  than leaving the Finder with no startup disk.
- A `backed` directory that can't be created simply opens empty; nothing
  is created outside the user's own directories.
- Renaming to a name a sibling already has is refused, as in the Finder.

## Tests

`shell/finder/tests/test_vfs.cpp` (`meson test finder-vfs`) builds a
private XDG environment in a temporary directory and checks discovery,
precedence, hidden entries, duplicate names, Desktop Actions, launching,
installing and uninstalling, renames and labels surviving a reload,
migration, what the Finder refuses, hiding and un-hiding an application,
package/version lookup against the real dpkg database, Flatpak origin
detection, icon resolution (and its fallback when nothing resolves), and
that every writable folder is inside the user's own directories. It runs
as a `QGuiApplication` on the offscreen QPA platform so icon rendering
is exercised for real, not skipped.

`lib/tests/test_draw.c` (`meson test draw`) checks the alpha-blend math
`pl_image_blend` uses to draw a resolved icon's real alpha, separately
from the icon set's plain on/off transparency.

`finder-desktop` tests menu and drag-to-Trash queueing, persisted records,
Put Away, cancellation, failed-removal retries, both uninstall command routes,
and personal-data preservation with isolated commands (no real uninstall).
`tests/ui/finder-startup-disk.sh` and `tests/ui/finder-hide-application.sh`
are the matching look at the screen; the latter queues and restores an app
without emptying Trash.
