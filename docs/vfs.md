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

## What you see

```
Zacintosh HD
├── System Folder
│   ├── Appearance        → ~/.local/share/zacos9/appearance
│   ├── Control Panels      (generated: ZacOS 9's control panels)
│   ├── Extensions
│   ├── Fonts             → ~/.local/share/fonts
│   └── Preferences       → ~/.config
├── Applications            (generated: the installed applications)
│   └── Mousepad
│       ├── Mousepad        launches it
│       └── Preferences     one of its Desktop Actions
├── Documents             → ~/Documents
└── Utilities               (generated, off by default)
```

A folder with an arrow stands for a real directory. Opening one opens
that directory, and from there everything is an ordinary file: the
virtual layer is only as deep as the curated part. The directory is
created the first time it is opened if it isn't there yet.

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
application, and nothing maps back to dpkg.

An application's identity is its **desktop file ID** (`mousepad.desktop`,
`org.gnome.Nautilus.desktop`), never its display name. Names are
translated, change between releases, and repeat between packages. When
two entries do share a name, each is told apart by its id — "Files
(org.gnome.Nautilus)" beside "Files (nemo)".

Nothing is copied. An application's folder and the launcher in it are
entries in this model; the package's files stay where dpkg put them.

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
do" below): the owning package, found from the application's own
executable (`dpkg -S` on its canonical path, resolving the symlinks a
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
| `kind` | `folder` (curated), `backed` (stands for a real directory), `apps`, `panels`, `unix`. |
| `backing` | For `backed`. `$HOME`, `$XDG_CONFIG_HOME`, `$XDG_DATA_HOME` and `$XDG_DOCUMENTS_DIR` are expanded. |
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

## What the Finder will and won't do

| | Curated folder | Application folder and its launcher | `backed` folder |
|---|---|---|---|
| Open | yes | launches the application | opens the real directory |
| Get Info | no window yet | its desktop entry | the real directory |
| Rename | yes, kept in the registry | yes, kept in the registry | ordinary rename |
| Label | yes, kept in the registry | yes, kept in the registry | extended attribute |
| Move, copy, duplicate, alias | no | no | ordinary |
| Throw away | no | an application's folder: hides it (next section) | ordinary |
| Take a drop | no | no | yes |
| New folder inside | no | no | yes |

Nothing a package owns can be moved, renamed on disk, or really thrown
away from the Finder, so no Finder gesture can damage an installed
package. Browsing and every one of these actions is unprivileged: the
only directories this hierarchy writes into are the user's own.

A Desktop Action can't be renamed on its own, because it isn't a thing of
its own: it belongs to the application's desktop entry.

Get Info reads a file, so it shows the one the item stands for: an
application's desktop entry, or a `backed` folder's real directory. The
curated folders stand for nothing on disk and have no Get Info window
yet.

## Hiding an application (never uninstalling it)

Move To Trash on an application's folder in Applications doesn't move
anything — there is no file — and it never touches the package. It asks
for confirmation first (unlike an ordinary Trash, there is no fishing the
item back out of it), then sets `hidden` in that folder's own override,
exactly the mechanism a rename already uses. The application comes right
back, with every other override (a rename, a label) intact, as soon as
that one key is gone: from **Special > Show All Applications**, which
clears `hidden` from every override, or by deleting the key from the
registry by hand.

The node itself, and everything generated under it, is never destroyed:
hiding only keeps it out of its parent's listing (`vfsList` filters on
`visible`), so nothing is lost if the application is reinstalled before
anyone brings it back on purpose.

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

`tests/ui/finder-startup-disk.sh` and `tests/ui/finder-hide-application.sh`
are the matching look at the screen.
