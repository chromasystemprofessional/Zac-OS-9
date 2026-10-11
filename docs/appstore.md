# Software: a package manager in Mac OS 9's clothes

Mac OS 9 never had an app store; there was nothing to style this after.
It borrows instead from the Mac's own list-and-details convention (the
Finder's own windows, Get Info, the Installer VISE packages shipped on
Mac OS 9 CDs): a category list, an item list, and a details pane with an
icon, a blurb, a status line and one button. Under that, it is apt.
It opens from Apple menu > **New Tricks**.

## What it's for

A short, curated catalog remains the default. **All Applications** expands
it with desktop applications described by the configured Debian repositories'
AppStream metadata. Search filters names, descriptions and package identifiers.
**Include Flathub applications** adds Flathub apps to the main categories and
All Applications. The checkbox defaults to checked, remembers your choice, and
only includes apps when the official user remote is enabled. Unchecking hides
them from combined browsing without disabling the remote, changing updates, or
removing apps. **Flathub** also remains available as a source-only view.
**Additional Sources** enables or disables the official Flathub remote for the
current user after confirmation.
One button per item: **Install** or **Remove**, whichever applies.

Debian discovery needs the packaged `appstream`, `python3-gi` and
`gir1.2-appstream-1.0` dependencies. Refresh Debian package metadata through
Software Update (or `sudo apt update`) after installing them, so APT downloads
DEP-11 metadata and refreshes the AppStream cache. Repositories that do not
publish AppStream desktop-app metadata will not contribute applications.
This is not a list of every library or command-line package.

Flathub uses `flatpak --user`, never a system-wide remote. Enabling contacts
the official HTTPS repository and downloads metadata; installing may download
large runtimes. Disabling keeps installed apps and their data but disables
that remote's browsing and updates. No remote is added by default, and arbitrary
third-party APT sources are not supported. Existing remotes named `flathub`
with a different URL are rejected rather than silently replaced.
Remote validation accepts Flatpak's omitted empty options column as well as
an explicit empty column; neither form changes the official URL requirement.
Catalog rows may likewise omit an empty trailing application description.
Discovery refreshes only Flathub's AppStream metadata (not installed apps), then
reads localized names, summaries, desktop categories, and cached icons from the
user installation. Network/Graphics/Office/AudioVideo/Game metadata maps into
Internet/Graphics/Productivity/Multimedia/Games; Education and Science map into
Productivity, with other or missing categories in Utilities. Apps without
published summaries or cached icons retain the listing summary or generic icon.
Metadata failures are reported rather than replacing the catalog with partial
results.

Flathub's AppStream download is large (about 80 seconds on a slow
connection), so the helper never kills it. The refresh runs as a detached
`flatpak update --appstream` that finishes even when the helper or the window
stops waiting. When a cached catalog already exists, it is read immediately
and refreshed in the background for the next visit. The first download is
waited on for 60 seconds; after that, Software says the catalog is still
downloading instead of starting over. Icon paths go through Flatpak's
`active` link, so a later refresh doesn't break them. The Featured page is
the curated catalog only and never waits on Flathub.
Software Update still manages ZacOS releases and Debian updates; Flatpak
updates can be applied separately with `flatpak --user update`.

## Architecture

| Piece | Path | What |
|---|---|---|
| The window | `shell/store/store.{h,cpp}` | `StoreWindow`: two `PanelList`s (category, item) and a details pane, built entirely from existing HIG-measured widgets (`lib/widgets.h`) — nothing new was added to the pixel-art toolkit except the icon blend (below). |
| The catalog | `shell/store/storeclient.{h,cpp}` | Reads the shipped catalog and talks to the helper. |
| The data | `assets/store/catalog.json` | Shipped, read-only; installed to `$datadir/zacos9/store/catalog.json`, found through `pl_data_dir()` (the same function the alert sounds use), so a build tree and an install both just work. |
| Discovery | `appstore/zacos9-software-catalog` | Unprivileged JSON helper: distro AppStream desktop applications or the enabled user Flathub remote. Asynchronous GUI loading has a two-minute timeout and explicit error status. |
| The privileged half | `appstore/zacos9-appstore-helper` | A root shell script behind pkexec; validates every package name before `apt-get` ever sees it. |
| Authorization | `appstore/org.zacos9.appstore.policy`, `appstore/50-zacos9-appstore.rules` | Same pattern as `sharing/`: this desktop runs no polkit authentication agent, so a local administrator (the `sudo` group) is granted without a password rather than being asked for one nothing can show. |

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

Discovered results are validated before merging; a failed refresh leaves the
previous catalog intact and reports the failure. Curated Debian packages are
deduplicated from the expanded results. Debian and Flathub entries remain
distinct even when their displayed names match, with their source shown in
the detail status. Search applies to the current category, including Featured.

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

No icon is given in the shipped Debian catalog. Before installing those
apps, there is nothing on disk to resolve one from, and the project doesn't ship third-party
logos (the same reasoning that keeps this out of `assets/`: no Apple
assets, and by the same logic, no Firefox fox or GIMP wilber either) —
the generic application icon is shown instead. Once installed, the real
one appears: `installedIcon()` in `store.cpp` matches the package
against `appList()`'s `AppEntry::origin` (`"dpkg:<package>"`, from the
Finder's own package-origin lookup — see `docs/vfs.md`) and uses its
resolved icon, the same `QIcon::fromTheme` pipeline the Finder's
Applications folder uses. Nothing is duplicated to get this: it is the
same code, called a second time. Flathub apps can show their cached AppStream
icons before installation; these are downloaded by Flatpak from the enabled
official remote, not bundled in ZacOS. Installed launcher icons take precedence,
with the metadata icon used if the launcher has none. Missing icons use the
generic application icon. The catalog JSON carries optional `category` and
absolute `icon` fields for discovered metadata, validated before merging.

## Installing and removing

`StoreWindow::act()` runs `zacos9-appstore-helper install <packages>`
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

Flathub install/remove/status commands run as the user, without pkexec.
Uninstall leaves application data intact. Flatpak-exported launcher directories
are included in the session's `XDG_DATA_DIRS`, making installed apps available
to Sniffer and the application menu after the updated session starts.

## Security

Every argument that reaches `apt-get` is validated in the helper itself
(`valid_pkg`: Debian's own package-name character set, and never
starting with `-`, so an argument can't be read as an option), **before**
the helper's root check, so the validation is defense in depth regardless
of who calls it — the GUI is not the only thing trusted to get this
right. `zacos9-store`'s package names come from the shipped catalog or validated
AppStream metadata, never directly from the search field. Flatpak identifiers
are validated separately and commands receive argument arrays, not shell strings.
The privileged helper still checks Debian arguments independently.
`DEBIAN_FRONTEND=noninteractive` keeps apt from ever
blocking on a question with no one positioned to answer it.

`installed` needs no privilege (reading dpkg's own status is not a
privileged operation) and is never run through pkexec.

## What was deliberately left out

- **Arbitrary third-party APT sources.** The optional external source is
  Flathub, keeping it separate from system packages.
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

`software-catalog` exercises metadata filtering, schema, remote URL validation,
disabled remotes, subprocess failures and timeouts using isolated mocks.
`store-browser` exercises the offscreen window, keeping Featured as default,
expanded results, search, source confirmation, Flathub install/remove routing
and visible errors. It uses private mock commands, never real installs or
repository mutations.

## Look-and-feel presets

An item in `assets/store/catalog.json` may carry a `"style"` (`id`, `label`,
`reset`, `blurb`). Once the application is installed, its detail pane gets a
second button beside Remove that switches the preset on (`label`) and off
(`reset`). It runs `zacos9-appstyle` (appstore/, Python 3, no privileges) as
the user.

- **GIMP › Photoshop Layout** is PhotoGIMP 3.1 by Diolinux (GPL-3.0): GIMP 3's
  tool order, panels and shortcuts arranged like Photoshop's. The release zip
  is fetched on first use, checked against a SHA-256 fixed in the helper, and
  only its `.config/GIMP/3.0` settings are used. Left out: the splash screen,
  icons and launcher (artwork whose license PhotoGIMP doesn't state), the
  author's monitor resolution (`gimprc`) and every window position, size and
  monitor (`sessionrc`, which holds windows for a two-monitor 2560-wide
  desk).
- **Inkscape › Illustrator Layout** downloads nothing: Inkscape 1.4 ships a keyboard set that
  follows Illustrator's (`/usr/share/inkscape/keys/adobe-illustrator-cs2.xml`: V select, A node,
  P pen, T text, M rectangle, L ellipse ...). The preset (`"kind": "prefs"`) sets
  `/options/kbshortcuts/shortcutfile` in `~/.config/inkscape/preferences.xml` and nothing else;
  Standard Layout puts that one setting back to what it was, keeping any other change made since.
- Anything it replaces is kept in `~/.local/share/zacos9/styles/ID.backup`;
  Standard Layout restores it exactly and removes what the preset added. A
  failure part way through is rolled back. It refuses while the application
  runs (GIMP rewrites its settings when it quits).
- Updating it means changing the URL and SHA-256 in `STYLES`, after trying the
  new release; `tests/store/test_appstyle.py` covers the mechanics with a made-up
  preset.
