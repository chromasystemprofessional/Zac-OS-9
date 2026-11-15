# Bloodhound: the search app

Bloodhound is ZacOS 9's name for Sherlock 2: the search dog. The plan maps
Sherlock 2 to Bloodhound in the
[Mac OS 9 gap plan](mac-os-9-gap-plan.md) (the glossary and §3.13), and the
System Folder plan has Sherlock's [Internet Search
Sites](system-folder.md).

This page is the design brief. It records the shape kept from Sherlock, what
each release should do, and the rules that keep Bloodhound a Mac OS 9 search
app rather than a modern search overlay.

## Where it is now

Bloodhound 1.3 shipped in ZacOS 9 1.13.0: `shell/bloodhound/` is a Qt 6
program with its own app id and one Platinum window. It appears as
**Bloodhound (Search)** in Applications, the Zac menu and, since 1.4, the
desktop. The Files channel has criteria rows, contents through the index
with snippets, Find Similar Items and saved searches; the channel bar reads
Internet Search Sites. Live Search, Recents, the Index menu, folder and
site-file drops and a collapsible Preview are in. Bloodhound 1.4 (in the
tree for the next release) gives the bar Sherlock 2's icons and fixed
channel set: Internet, People, Shopping, News, ZacOS, Reference, My Channel
and the user's channels; see
[Sherlock 2's channel bar](#sherlock-2s-channel-bar).

The search engine is shared with the Sniffer's Find:

- `shell/files/findcriteria.cpp`: queries, criteria rows, saved searches.
- `shell/files/findindex.cpp`: contents through the local search index.
- `shell/files/searchsites.cpp`: Internet Search Sites for both programs.
- `shell/finder/findwindow.cpp`: the Find dialog and Items Found windows.

The Sniffer's Find keeps its own Find dialog; both read one engine, one
saved-search file and one site list.

## What Sherlock 2 got right

Sherlock 2 shipped with Mac OS 9 and was the first Sherlock with channels.
The parts worth keeping:

- **One window** holds the query, the settings and the results. It did not
  open a new results window for every search.
- **Channels** are the first choice: a Files channel, Internet channels, and
  user-made channels that group search sites, drawn as a row of icons along
  the top of the window.
- **The Files channel** searches names and, after indexing, the contents of
  files. "More Choices" adds criteria.
- **Internet Search Sites** are plain-text files in the System Folder. Anyone
  can write one; channels are just groups of them. There is a personal
  channel for the sites you add.
- **The result list is where the work happens**: open, reveal and use the
  results, not a preview-only list.

The parts not to copy: the giant "More Search Options" dialog, banner
advertisements, and searches that could not be stopped. Bloodhound improves
those pieces rather than reproducing them.

## Sherlock 2's channel bar

Sherlock 2 shipped with Mac OS 9 in October 1999. Its window began with a
row of icon tiles. Choosing one changed the search beneath it: the web sites,
the fields and the results all belonged to that channel. The tiles were
icon-only; the name came from a hint bubble, and the chosen tile was pressed
into the accent colour. Apple's own review material and the Missing Manual
both describe the same set of built-in channels:

| Sherlock 2 channel | What it was |
|---|---|
| **Files** | The local disks: names, criteria and, with Find by Content, the words inside documents. |
| **Internet** | The general web search sites. |
| **People** | LDAP and white-pages directories: Yahoo, Bigfoot, Four11. |
| **Shopping** | Retail search sites. Sherlock 2 parsed price and availability out of their results. |
| **News** | News sites. Sherlock 2 parsed publication dates out of their results. |
| **Apple** | Apple's own support, software and news. |
| **Reference** | Dictionaries and encyclopedias. |
| **My Channel** | The user's own sites, plus any site without a route. |
| User channels | Channels the user made, each a group of sites. |

Mac OS 9.1 added an **Entertainment** channel (films and music). The built-in
channels could not be deleted; their sites could. A search site was a plain
file in `Internet Search Sites`, and its `routeType` attribute decided which
channel it joined; without one it went to My Channel. That is the part worth
keeping: a channel is a group of files, not a service baked into the
program.

The parts not to copy are the embedded result pages and their banner ads,
the price and date scrapers, and the dead directories. People search in 2026
means data brokers; Apple's channel means a company ZacOS is not; a
shopping channel that scrapes prices is a second web browser with worse
privacy. Bloodhound keeps the shape and replaces the services.

Sources: Apple's Technical Note TN1141 (the search-site format, `routeType`
and the default channel names), TidBITS' and Macworld's Mac OS 9 reviews
(the built-in set, People's LDAP directories and custom channels), and the
Missing Manual's chapter 15.

### The equivalents

Bloodhound's channels, in Sherlock 2's order:

| Sherlock 2 | Bloodhound 1.4 | What it searches |
|---|---|---|
| Files | **Files** | The local search index and the Sniffer's Find engine, unchanged. |
| Internet | **Internet** | Every installed search site; a general engine first, DuckDuckGo by default. |
| People | **People** | The people who can use this computer: their full names and Home folders, from the system's account list. It never sends a name to a people-search service. A web people directory would be a site the user adds to another channel; it is not in the box. |
| Shopping | **Shopping** | A group of shop search sites. eBay and Amazon ship as plain search URLs: no affiliate or tracking parameters, no price scraping, and the results open in the browser. |
| News | **News** | A group of news search sites; BBC News and NPR ship. The site does the searching; Bloodhound does not fetch or render the page. |
| Apple | **ZacOS** | ZacOS's own Help. Search runs `zacos9-help --search WORDS`; with the field empty it opens the Help Center. This is the vendor channel, and it is local. |
| Reference | **Reference** | Wikipedia and Wiktionary ship; more can be dropped in. |
| My Channel | **My Channel** | The user's sites without a `Channel=`, as before. |
| User channels | **User channels** | Sites grouped by `Channel=`, each with the generic channel icon. |
| Entertainment (9.1) | — | Not copied. A user channel can hold film or music sites. |

The icons are original 32×32 Platinum art in
`assets/icons/platinum-icons.picon` (`PL_ICON_CHANNEL_*`; the app's own
`PL_ICON_APP_BLOODHOUND` is a paw print and a magnifier). Sherlock 2's
tiles had no labels; Bloodhound puts a small label under each, because a
32-pixel bone does not say ZacOS by itself and a page with a pencil does not
say My Channel. The hint bubble names every tile as well, and `⌘1`–`⌘8`
choose the fixed channels. The chosen tile is pressed into the accent
colour; hovering raises the one under the pointer. Tiles that do not fit
behind the last one live behind a `»` tile and a menu.

The fixed channels stay even when they have no sites, as Sherlock's did:
a channel is a place to put sites, and a user channel with no sites says so.
**People** is the local exception: it lists the accounts on this computer,
with their full names, Home folders and kind, and filters as the user types.
**ZacOS** is the other local channel: it has the bone, the Help sentence and
the Search button, and no site popup. A channel with sites shows the site
popup, the search field and the usual "results open in your browser" line.

## The shape we keep

```
+----------------------------------------------------------------+
| [Files] [Internet] [People] [Shopping] [News] [ZacOS]           |
| [Reference] [My Channel] [user channels...]                     |
+----------------------------------------------------------------+
| Search: [................................] [ Search ] [ Stop ] |
| Find items: [ on all disks v ] whose [ name v ] [ contains v ] |
|            [................................]                   |
| More Choices...                                                 |
+----------------------------------------------------------------+
| Name              Where                 Modified       Size    |
| Report.txt        Documents/Letters     8 Nov 2026     12 K    |
| ...                                                             |
+----------------------------------------------------------------+
| Searching... 42 found                                           |
+----------------------------------------------------------------+
```

- The channel bar is a Platinum toolbar of 32×32 icons with a label under
  each, not text tabs, a sidebar or a hamburger menu. The fixed channels are
  always there; the user's channels follow them in one row, and a `»` tile
  holds any that don't fit.
- A web channel replaces the criteria rows with its site popup and a short
  pane that names the site and says the results open in the browser. The
  ZacOS channel replaces them with the Help sentence.
- The search field, scope and criteria are one row group above the results.
- The result list replaces its contents for each search; a new search does not
  leave a trail of result windows. "New Bloodhound Window" stays available
  for a second search.
- Files results behave like Sniffer results: Open, Reveal, Get Info, labels,
  drag and drop, Move to Trash.
- The menu bar, menus, buttons, focus rings, fonts and sounds are ZacOS's own.

## Releases

### 1.0: the standalone window

Shipped in ZacOS 9 1.11.0.

The first real Bloodhound process, with the Files channel only.

- `zacos9-bloodhound` is a Qt program with its own app id and a normal
  Platinum window, not the Sniffer's Find dialog.
- Channel bar with **Files** (the other channels come later).
- Search field, scope (all disks, Home, the front folder), Search and Stop.
- Name search with the existing name operations.
- Results list with name, location, date modified, size and kind.
- Open, Reveal, Get Info, drag, copy path; multi-selection.
- Keyboard: `⌘F` focuses the field, Return searches, arrows move, Return
  opens, `⌘R` reveals, `⌘I` Get Info, Escape stops or clears, Tab reaches the
  channel bar.
- Progress, result count, empty state and an honest error when a place cannot
  be read.

Not in 1.0: criteria rows, contents, saved searches, Internet channels.

### 1.1: the Files channel, complete

Shipped in ZacOS 9 1.12.0.

- The existing criteria rows (dates, size, kind, label, lock, folder,
  custom icon, comments, invisibility, version).
- Contents through the search index, with Index Selection and an honest
  index-unavailable state.
- Find Similar Items.
- Saved searches, read from and written to the same
  `~/.config/zacos9/find-searches.json` as the Sniffer, so both see one list.
- Result snippets with the matched words highlighted for contents searches.
- Search results stream in; Stop cancels the worker; a result cap and count
  keep a broad search under control.

### 1.2: channels and the web

Shipped in ZacOS 9 1.12.0.

- **Internet Search Sites** in `System Folder/Internet Search Sites`
  (read-only, the system's sites) and
  `~/.config/zacos9/Internet Search Sites` (the user's own, drop-in).
- A **Web** channel: choose a site, search, and open the results in the
  default browser.
- User channels group sites by their `Channel=` field; the user's own sites
  form **My Channel**, as Sherlock's did.
- The Sniffer's `File ▸ Search Internet…` (`⌘H`) and the Find window's
  "Find items" Internet choices read the same site list, so there is one
  source of truth.
- Search sites are data, never code.

### 1.3: modern polish

Shipped in ZacOS 9 1.13.0.

- Live search as an option, debounced, with the Search button still there.
- Recent searches, kept locally and clearable.
- Index status and control: what is indexed, rebuild, and add or remove
  folders.
- Drag a folder onto Bloodhound to search it; drag a search-site file on to
  install it.
- A collapsible preview or Get Info pane, not a permanent modern sidebar.
- Accessibility names for the window and its main controls, with the
  keyboard operation already in place.
- The app's strings in the translation files; site and channel names stay as
  their files write them.

### 1.4: the channel bar

In the tree for the next release.

- Sherlock 2's channels as a row of 32×32 Platinum icons with labels:
  Files, Internet, People, Shopping, News, ZacOS, Reference and My Channel,
  then the user's own channels. The fixed set stays when a channel is empty.
- The selected tile is pressed into the accent colour; hovering raises a
  tile; Hint Bubbles name each one; `⌘1`–`⌘8` choose the fixed channels and
  Tab, the arrow keys, Return and Escape work the bar from the keyboard.
- A `»` tile and a menu when the channels outgrow the window.
- **Internet** is every installed site, with DuckDuckGo first, then the other
  general engines, then the rest.
- **People** replaces Sherlock's white pages with the people who can use this
  computer: their full names, Home folders and kind, from the system's
  account list, filtered as you type. No name is sent anywhere.
- **Shopping** ships eBay and Amazon as plain search URLs; **News** BBC News
  and NPR; **Reference** Wikipedia and Wiktionary. No affiliate codes, no
  price or date scraping, and no embedded pages: the site's own page opens in
  the browser.
- **ZacOS** replaces Sherlock's Apple channel: Search runs
  `zacos9-help --search WORDS`, or opens the Help Center when the field is
  empty.
- A search-site file dropped on Bloodhound is installed into its own
  `Channel=`, not always into Internet.
- `zacos9-desktop-places` puts a **Bloodhound (Search)** alias on the desktop
  once, with the app's own icon. Deleting it keeps it deleted; a marker in
  `~/.config/zacos9/desktop-shortcuts` records that it was offered.

### Later, carefully

- Optional local channels: Programs, Control Panels, Settings.
- Smart Folders in the Sniffer backed by a saved Bloodhound search.
- A global hotkey that opens Bloodhound as an app window.
- Local, clearly labelled semantic search. Never the default.
- An Entertainment channel (Mac OS 9.1's): not copied; a user channel can
  hold film and music sites.
- People stays local: the accounts on this computer, never a people-search
  broker. Shopping and News stay groups of site files: Shopping is plain
  store searches, not price scraping; News sends the words to the news site
  and opens the page in the browser.

## The Files channel

**Scope.** All disks, Home, the folder in front (offered when Bloodhound is
opened from a folder), or a folder dragged onto the window. The scope popup
remembers the last choice.

**Criteria.** The existing `FindField`/`FindOp` rows, up to six, all of which
must match. "More Choices" adds a row; "Fewer Choices" removes one. The first
row starts as name contains. The old Find dialog's layout is the model; the
Sherlock 2 modal "More Search Options" is not.

**Contents.** A contents row asks the index (localsearch/tinysparql first,
the packaging fallback later if needed). The other rows sift its answers.
Index Selection adds a folder; the index's own scheduler does the rest. If
the index is unavailable, the row says so and the search still works for the
other criteria.

**Results.** A list, as the Sniffer's list view: disclosure where it makes
sense, sortable columns, multi-selection, type-select and arrows. For a
contents match, a snippet in the Views font under the row, with the matched
words highlighted. The list is the same whether the result came from a name
walk or from the index.

**Actions.** Open, Open With, Reveal, Get Info, Copy (name or path), Label,
Move to Trash, drag to a folder, the desktop or the Trash. Print and other
document actions follow the Sniffer's rules rather than growing a second
implementation.

**Search worker.** A separate thread, results in batches, a Stop button and
Escape, a 5,000-result cap (the Find window's current cap), and no blocking
of the UI. A search that returns nothing says so; a search that cannot read a
place names the place.

## Web channels and Internet Search Sites

A search site is a file. That is the rule that keeps Bloodhound open:
channels are groups of files, not a provider table in the source.

**Where.** `System Folder/Internet Search Sites` for the system's read-only
sites, and `~/.config/zacos9/Internet Search Sites` for the user's. A site
file dropped in either appears in Bloodhound and in the Sniffer's Search
Internet command without a restart.

**Formats.** Read, in this order:

1. **OpenSearch** XML (`.xml`): the modern standard, with the site's name,
   description, URL template and optional icon.
2. **A ZacOS search site** (`.searchsite`), key=value, for a site that does
   not publish OpenSearch:

   ```
   [Search Site]
   Name=Wikipedia
   URL=https://en.wikipedia.org/w/index.php?search={query}
   Channel=Reference
   Icon=system-search
   ```

   `URL` must be `http` or `https`; `{query}` is replaced with the
   URL-encoded words. `Channel` groups sites; sites without one are in
   My Channel.

3. **A classic Sherlock `.src`** (`.src`, best effort): read its `<search>`
   URL and input encoding and treat it as a search-URL site. Its
   `<interpret>` result parser is not run: modern Bloodhound does not scrape
   a page and render someone else's HTML.

The format is data only. No scripts, no executable plug-ins, no automatic
updates from the network.

**The fixed groups.** The system's seed files put DuckDuckGo, Google and Bing
in **Internet**; eBay and Amazon in **Shopping**; BBC News and NPR in
**News**; Wikipedia and Wiktionary in **Reference**. **People** and
**ZacOS** are local and have no site files. A site whose `Channel=` names
one of the fixed web channels joins it; any other name makes a user channel;
no `Channel=` means **My Channel**. The Internet channel is the exception:
it offers every site, with a general engine first, so the familiar
one-field search never runs out of choices.

**Searching.** A Web search is only sent when the user presses Search or
Return. The query goes to the chosen site; the results open in the default
browser. Bloodhound does not fetch the result page, does not show ads, and
does not add affiliate or tracking parameters. The status line names the
site that will be used. The ZacOS channel is local: it runs the Help Center
with the words, and no network request is made.

**One list.** The Find window's "Find items" popup and File ▸ Search
Internet… use the same site reader. When the site list changes, both update;
the old hard-coded DuckDuckGo/Google/Bing list becomes the seed files, not
the only choices.

## Modernising without betraying it

- **Local first.** Files are indexed and searched on this computer. The web
  is contacted only for a web search the user asked for. No telemetry, no
  cloud index, no uploaded file names.
- **Progressive disclosure.** The simple search stays simple. Criteria are
  behind More Choices; index controls are behind a status or a menu, not a
  permanent panel of toggles.
- **Fast and cancellable.** Sherlock could make people wait. Bloodhound
  streams, counts, stops and says what it is doing.
- **Keyboard and accessibility first.** Sherlock was mouse-heavy. Bloodhound
  should be fully usable without a mouse, with names for assistive
  technology and visible focus.
- **The browser is the web viewer.** That is the modern replacement for
  Sherlock's embedded, ad-bearing result pages. It also keeps untrusted HTML
  out of the shell. The ZacOS channel is the other side of the rule: the
  vendor's own content is local, so it opens the installed Help rather than
  a web page.
- **Plain files, not a registry.** Search sites and channels are files a
  person can read, copy and edit. This is Sherlock's best idea and the
  cheapest way to keep the app open.
- **One engine.** The Sniffer's Find and Bloodhound should share the query,
  criteria and index code. Two search engines would drift.

## What not to do

- No banner ads, tracking pixels, affiliate codes or "approved partner"
  results.
- No arbitrary HTML/JavaScript from a search site; no remote code.
- No Spotlight-style global overlay that replaces the desktop. Bloodhound
  is an app with channels and a window.
- No tabbed-browser chrome, hamburger menus or a permanent filter sidebar.
- No giant "More Search Options" modal.
- No separate results window for every search.
- No mandatory live search.
- No cloud index or AI search by default.
- No people-search broker in the box: People lists the accounts on this
  computer. A web people directory would be a site the user adds.
- No shopping price scrapers, affiliate codes or "partner" results: Shopping
  is a group of ordinary store searches.
- No Entertainment channel or other built-in service that would need a
  directory to keep alive; user channels cover those interests.
- Do not rename the Sniffer's **Find…** command. Bloodhound is the app;
  Find is the verb.

## How it fits the code

- **The app.** `shell/bloodhound/` is a Qt 6 program with its own app
  id, `platinumShellInit()`, `lib/` drawing and `panelkit` controls. Its
  desktop entry keeps the name Bloodhound and the Zac menu alias keeps
  working.
- **The engine.** The criteria and the search index live in `shell/files`
  (`libzacos9-files`: `findcriteria.*`, `findindex.*`), so the Finder and
  Bloodhound link one copy. `findwindow.cpp` stays in the Finder while the
  Sniffer's Find dialog remains; Bloodhound gets its own window and result
  list.
- **The Sniffer link.** Open and Reveal use `zacos9-finder-ctl open` and
  `zacos9-finder-ctl reveal`, as the app shell already does. Get Info can
  follow through the same command surface, or a small `platinum-shell-v1`
  request if the socket is too narrow.
- **Settings.** `~/.config/zacos9/bloodhound.conf` through `KeyValueFile`:
  last channel and scope, live results on/off, result limit. Saved searches
  stay in `find-searches.json`.
- **The channels.** `BloodhoundWindow::ChannelKind` and
  `BloodhoundWindow::rebuildChannels()` build the fixed set, read the user's
  channels from the sites, and place the tiles. `fillPeople()` reads the
  account list through NSS (`getpwent`; `ZACOS9_PASSWD` points at a file for
  tests) and makes each Home folder a result. `searchHelp()` runs
  `zacos9-help --search`. The icons are `PL_ICON_CHANNEL_*` and
  `PL_ICON_APP_BLOODHOUND` in `lib/icons.h`, drawn from
  `assets/icons/platinum-icons.picon`; `pl_icon_for_program()` gives the
  program and its aliases the paw-print icon.
- **The desktop shortcut.** `session/zacos9-desktop-places` creates
  `~/Desktop/Bloodhound (Search)` as a symbolic link to the installed
  `.desktop` file, once, and records it in
  `~/.config/zacos9/desktop-shortcuts`. The Sniffer draws the link as an
  application alias with Bloodhound's own icon.
- **Tests.** The existing `finder-find` unit tests cover criteria and
  results. `tests/bloodhound/test_controls.cpp` checks the fixed channels,
  People's account list and filtering, and that ZacOS Search runs the Help
  Center;
  `test_window.py` searches a temporary folder through the real compositor;
  the search-site parser tests cover OpenSearch, `.searchsite` and `.src`;
  `tests/finder/test_desktop_places.py` checks the desktop alias is seeded
  once and respects an item the user already has.

## Order

1. **Standalone window** (the first issue): own process, one window, Files
   channel, name search, results, open/reveal, keyboard. Shipped in 1.11.0.
2. **Files channel complete**: criteria, contents, Find Similar, saved
   searches, snippets, cancellation.
3. **Channels and the web**: Internet Search Sites, the Web channel, user
   channels, one shared site list with Search Internet.
4. **Modern polish**: live results, recents, index status, drag and drop,
   preview, accessibility, localization.
5. **The channel bar**: Sherlock 2's icons, the fixed channels, the ZacOS
   Help channel, the seed groups, and the desktop shortcut. In the tree for
   the next release.

Each slice is its own pull request. The engine move in slice 1 is the one
structural change; after that, features stay inside `shell/bloodhound/` and
the shared search library.
