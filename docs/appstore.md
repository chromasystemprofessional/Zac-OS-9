# Software: a package manager in Mac OS 9's clothes

Mac OS 9 never had an app store; there was nothing to style this after.
It borrows instead from the Mac's own list-and-details convention (the
Finder's own windows, Get Info, the Installer VISE packages shipped on
Mac OS 9 CDs): a category list, an item list, and a details pane with an
icon, a blurb, a status line and one button. Under that, it is apt.

## What it's for

A short, curated catalog of real, well-known applications — not a
front end to the whole of Debian's archive, which would need search,
dependency trees, and a great deal more chrome to be usable. One button
per item: **Install** or **Remove**, whichever applies.

## Architecture

| Piece | Path | What |
|---|---|---|
| The window | `shell/store/store.{h,cpp}` | `StoreWindow`: two `PanelList`s (category, item) and a details pane, built entirely from existing HIG-measured widgets (`lib/widgets.h`) — nothing new was added to the pixel-art toolkit except the icon blend (below). |
| The catalog | `shell/store/storeclient.{h,cpp}` | Reads the shipped catalog and talks to the helper. |
| The data | `assets/store/catalog.json` | Shipped, read-only; installed to `$datadir/platinum/store/catalog.json`, found through `pl_data_dir()` (the same function the alert sounds use), so a build tree and an install both just work. |
| The privileged half | `appstore/platinum-appstore-helper` | A root shell script behind pkexec; validates every package name before `apt-get` ever sees it. |
| Authorization | `appstore/org.platinum2026.appstore.policy`, `appstore/50-platinum-appstore.rules` | Same pattern as `sharing/`: this desktop runs no polkit authentication agent, so a local administrator (the `sudo` group) is granted without a password rather than being asked for one nothing can show. |

Reused, not reinvented: `PanelList`, `PanelButton`, `panelText`,
`panelWrap`, `panelGroup` (`shell/panels/panelkit.*`); `Alert::ask` for
the removal confirmation; `appList()`/`appdb.h` (the Finder's Applications
folder machinery) for a resolved application's real icon, matched by
package origin; `pl_progress_paint` for the busy indicator;
`pl_data_dir()` for finding the shipped catalog.

New: `pl_image_blend` (`lib/draw.{h,c}`) — the one genuinely new drawing
primitive, real alpha compositing for an icon that isn't 1990s pixel art,
first added for the Finder's own Applications folder and reused here
unchanged.

## The catalog

```json
{
  "version": 1,
  "categories": ["Featured", "Internet", "Graphics", "..."],
  "items": [
    { "id": "firefox", "name": "Firefox", "category": "Internet",
      "featured": true, "blurb": "Browse the web.", "packages": ["firefox-esr"] }
  ]
}
```

- `id` is ours (stable; never shown). `packages` are the real apt
  package names `apt-get install`/`remove` act on — more than one, if an
  item needs several (none of the shipped ones do, but the field
  supports it; "installed" means every one of them is).
- `"Featured"` is synthesized, not a real category: it collects every
  item with `"featured": true`, from any category.
- An item missing a name or with no packages is left out rather than
  shown broken (`storeItems()` filters it silently; see
  `shell/store/tests/test_storeclient.cpp`).

No icon is given in the catalog. Before installing, there is nothing on
disk to resolve one from, and the project doesn't ship third-party
logos (the same reasoning that keeps this out of `assets/`: no Apple
assets, and by the same logic, no Firefox fox or GIMP wilber either) —
the generic application icon is shown instead. Once installed, the real
one appears: `installedIcon()` in `store.cpp` matches the package
against `appList()`'s `AppEntry::origin` (`"dpkg:<package>"`, from the
Finder's own package-origin lookup — see `docs/vfs.md`) and uses its
resolved icon, the same `QIcon::fromTheme` pipeline the Finder's
Applications folder uses. Nothing is duplicated to get this: it is the
same code, called a second time.

## Installing and removing

`StoreWindow::act()` runs `platinum-appstore-helper install <packages>`
or `remove <packages>`, through pkexec (or directly, already root — the
same `appstoreHelperCommand` pattern `sharingclient.cpp` established).
It blocks the button's click handler but keeps the window's events
flowing (`QApplication::processEvents` in a loop, exactly as
`runSharingHelper` does), so the window stays responsive and paints a
sweeping progress bar while it waits.

That bar is **indeterminate**, a sweep rather than a percentage.
Real per-package progress would mean parsing apt's own terminal output,
which is not a stable interface across apt versions or locales; rather
than show a number that might be wrong, it says "working" and nothing
more precise. Removal asks for confirmation first (`Alert::ask`,
naming the application and stating plainly that it is the application
being removed, not any of the user's own files); installing does not,
matching how the rest of the Finder treats additive actions versus
destructive ones.

"Installed?" is checked (`packagesInstalled()`, `dpkg-query`) when an
item is selected and right after an install or remove finishes — never
polled continuously: nothing but this window's own actions, or someone
at a terminal, changes it in the meantime.

## Security

Every argument that reaches `apt-get` is validated in the helper itself
(`valid_pkg`: Debian's own package-name character set, and never
starting with `-`, so an argument can't be read as an option), **before**
the helper's root check, so the validation is defense in depth regardless
of who calls it — the GUI is not the only thing trusted to get this
right. `platinum-store`'s own package names come only from the shipped
catalog, never from anything typed by the user (there is no text field
anywhere in the window), so in practice nothing but a curated list of
package names is ever possible, but the helper does not assume that: it
checks for itself. `DEBIAN_FRONTEND=noninteractive` keeps apt from ever
blocking on a question with no one positioned to answer it.

`installed` needs no privilege (reading dpkg's own status is not a
privileged operation) and is never run through pkexec.

## What was deliberately left out

- **Search.** The catalog is small and categorized; a search field would
  be more UI than the catalog currently justifies.
- **A progress percentage.** See above.
- **Batch install** (checkboxes, "Install all checked" — closer to how
  the Mac OS 9 CD's own Software Installer actually worked). One item,
  one button is simpler and was enough to be useful; revisit if the
  catalog grows enough that installing several things at once becomes a
  real chore.
- **An update-available indicator.** `dpkg-query` says installed or not;
  it does not compare versions against what's in the archive. A "Check
  for Updates" affordance is a reasonable follow-up, not yet built.

## Tests

`shell/store/tests/test_storeclient.cpp` (`meson test store-client`)
checks catalog parsing (category order, Featured's synthesis, an entry
with no name or no packages being dropped rather than shown broken, a
missing catalog file reading as empty rather than crashing), and runs the
real helper script directly — never through pkexec, and never with a
package name that could install or remove anything real — to prove its
validation rejects a leading `-`, shell metacharacters, a space, and an
empty argument list, for both `install` and `remove`, before the root
check is ever reached.

`tests/ui/store-install.sh` is the matching look at the screen: it really
installs and removes a small package (`galculator`) through the window,
end to end, and was used to produce the screenshots this feature's
review was based on.
