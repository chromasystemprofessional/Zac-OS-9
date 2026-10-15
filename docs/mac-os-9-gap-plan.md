# Mac OS 9 feature gaps: inventory and implementation plan

Source: *Mac OS 9: The Missing Manual* (David Pogue, 2000), the scanned PDF
in `upload assets/` (reference only — it isn't shipped or committed). Every
feature the book documents was checked against this tree on 2026-10-13
(code, `PROJECT_STATUS.md`, `docs/`). Chapter and page numbers ("ch. 1
p. 11") are the book's own.

**Naming.** This plan uses ZacOS's own names throughout (see the glossary
below). Apple's names appear only in the glossary, in the Mac OS 9 column of
section 2, and where a feature is out of scope, to identify the original.

**Legend.** Status: ✅ done · ◐ partial · ✗ missing · — deliberately not
built (see "Out of scope"). Priority: **P1** used every day, or expected by
anyone who knew Mac OS 9 · **P2** valued, used weekly · **P3** nice to have.
Effort: **S** about a session · **M** 2–4 sessions · **L** 5+ sessions.

---

## Glossary: ZacOS names

The rule: distinctive names Apple coined are replaced; plain descriptive
words (Calculator, Favorites, General Controls, Date & Time, contextual
menus, pop-up windows, Help Center…) stay, as any desktop would use them.
New names follow the dog theme ZacOS already has (Happy Zac, Sniffer,
Zacintosh HD, The Collar, New Tricks, Fetch Updates) but must still say
what the thing does.

| Mac OS 9 name | ZacOS name | Status | Why this name |
|---|---|---|---|
| Finder | **Sniffer** | shipped (app menu) | sniffs out your files |
| Macintosh HD | **Zacintosh HD** | shipped | |
| Apple (🍎) menu | **Zac menu** | shipped as the Happy Zac menu | |
| Apple Menu Options | **Zac Menu Options** | new | |
| Control Strip | **The Collar** | shipped | |
| Software Update | **Fetch Updates** | shipped | |
| Sherlock 2 | **Bloodhound** | new | the search dog |
| Find Similar Files | **Find Similar Items** | new | descriptive |
| Launcher | **Toy Box** | new | where favourite things are kept |
| Application Switcher palette | **The Pack** | new | every running app, together |
| Location Manager | **Walkies** | new | switch settings when you go somewhere |
| File Synchronization | **Retriever** | new | brings files back and forth |
| Folder Actions | **Folder Tricks** | new | pairs with New Tricks |
| Multiple Users | **Users** | new | descriptive |
| Simple Finder / "Panels" accounts | **Puppy Mode** | new | the simplified desktop for children |
| Stickies | **Dog Ears** | new | dog-eared notes |
| Note Pad | **Notepad** | new | generic |
| Scrapbook | **Stash** | new | where a dog keeps its finds |
| Key Caps | **Key Tags** | new | dog tags, for keys |
| SimpleText | **Scribble** | new | |
| SimpleSound | **Bark Recorder** | new | |
| Apple System Profiler | **Pedigree** | new | the machine's papers |
| Disk First Aid | **Disk Vet** | new | |
| Drive Setup | **Disk Setup** | new | descriptive |
| Chooser | **Printer Picker** | new | descriptive |
| PrintMonitor | **Print Queue** | new | descriptive |
| Balloon Help | **Hint Bubbles** | new | |
| Mac Help | **ZacOS Help** | new | |
| Easy Access / Close View | **Accessibility** / **Zoom** | new | descriptive |
| QuickTime Settings | **Media Settings** | new | descriptive |
| Graphing Calculator | **Plotter** | new | |

**Renamed in 0.1.54** (shown names only; program names, settings keys and
the `~/Classic` folder are unchanged):

| Mac OS 9 name | ZacOS name |
|---|---|
| Extensions Manager | **Tricks Manager** |
| File Exchange | **File Translator** |
| Network Browser | **Neighbourhood** |
| Classic | **Old Dog** (runs the old Mac software) |
| Energy Saver | **Nap Time** |
| Platinum (appearance) | **Silver Coat** |

The developer docs (`docs/*.md`, `CLAUDE.md`) still call them by their old
names in places; the code and what users see use the new ones.

---

## 1. Summary

ZacOS already covers the core of the Mac OS 9 desktop: Platinum windows
(close, zoom, collapse, outline drag), the menu bar with global menus, a
spatial Sniffer (icon, list and button views, labels, aliases, spring-loaded
folders, Get Info with Sharing, Put Away, Find by name), the Zac menu with
Control Panels, The Collar (the Control Strip), Appearance, Date & Time, Energy
Saver, Screen Saver with lock, Mouse, Keyboard (repeat only), Monitors,
Sound, TCP/IP, File Sharing, Neighbourhood, Keychain (in The Collar),
Fetch Updates, Force Quit, Classic, and a Platinum login window.

The biggest gaps, in the order they'd be noticed:

1. **Contextual menus** (Control-click anywhere in the Sniffer and on the
   desktop) — none yet.
2. **Switching programs from the keyboard** — no ⌘-Tab, no Option-click to
   hide, no Pack palette, no F-key launching.
3. **Recent Applications / Documents / Servers and Favorites** in the Zac
   menu, and the Zac Menu Options control panel.
4. **Sniffer menu commands still missing**: Print, Add To Favorites, Encrypt,
   Search Internet, Page Setup, Print Window; Show Clipboard and Preferences;
   as Pop-up Window, Reset Column Positions, View Options; Eject; Log Out;
   the whole Help menu.
5. **Window tricks**: ⌘-click title for the folder-path menu, Option-click
   close/collapse for all of an app's windows, ⌘-drag a background window,
   the draggable proxy icon, Option-click zoom to fill the screen.
6. **Search by content** (Bloodhound's index), Find Similar Items, and richer
   Find criteria.
7. **General Controls, Zac Menu Options, Keyboard layouts and F-keys,
   Startup Disk, Walkies, Users, Speech, Accessibility**
   control panels.
8. **Desk accessories**: Dog Ears, Notepad, Stash, Calculator, Key
   Tags, Scribble, Bark Recorder, Pedigree.
9. **Printing**: Printer Picker, desktop printers, print from the Sniffer.
10. **Help**: Help Center and Hint Bubbles.

Section 3 lists every feature; section 5 is the plan.

---

## 2. How the mapping works on Linux

| Mac OS 9 mechanism | ZacOS equivalent to use |
|---|---|
| Apple Menu Items folder | `ZacOS 9 Menu Items` (already feeds the Apple menu) |
| Startup Items / Shutdown Items | XDG autostart (`shell/finder/autostart.cpp`); a Shutdown Items folder run by `zacos9-session` on logout |
| Control Strip modules | The Collar's modules (`shell/collar/`) |
| Contextual Menu Items folder | A Finder plug-in folder of `.desktop` actions (`Actions=` / FileManager actions spec) |
| Desktop database, type/creator | MIME types (shared-mime-info) and desktop entries; `shell/finder/fileassoc.cpp` |
| Extensions / Extensions Manager | systemd user units, autostart entries and the extension catalog (`lib/extensions.c`) |
| AppleScript / folder actions | D-Bus + shell/Python scripts; inotify for folder actions |
| Sherlock index | localsearch/tracker3 or recoll (a package dependency), queried over D-Bus or its CLI |
| Keychain | Secret Service (gnome-keyring / KeePassXC); already in The Collar |
| Multiple Users | Unix accounts, `accountsservice`, polkit; greetd for login |
| Chooser / desktop printers | CUPS (`lpstat`, `lp`, IPP), Avahi for discovery |
| Location Manager | NetworkManager connections + ZacOS settings snapshots |
| PlainTalk speech | speech-dispatcher / espeak-ng (text-to-speech); speech recognition is out of scope |
| Help Center | HTML help in `share/zacos9/help`, a Platinum help viewer |

---

## 3. Feature inventory

### 3.1 Windows (ch. 1)

| Feature | Book | Status | Notes |
|---|---|---|---|
| Close box, ⌘-W | p. 9 | ✅ | |
| Option-click close box / Option-⌘-W closes all of the app's windows | p. 10 | ◐ | Option-click ✅ (0.1.54); Option-⌘-W still to do. P2 S |
| Title-bar stripes show the active window; drag to move | p. 10 | ✅ | |
| ⌘-click title → folder-hierarchy menu; Option-⌘-click closes the inner window too | p. 11 | ✅ | Sniffer windows; through the Macintosh view (… Home, Zacintosh HD) (0.1.54). No small folder icons in the menu yet. |
| ⌘-↑ opens the enclosing folder; Option-⌘-↑ also closes this one | p. 12 | ✅ | (0.1.54) |
| ⌘-drag an inactive window without bringing it forward | p. 12 | ✅ | Title bar or edge (0.1.54). |
| Double-click title to collapse (Appearance option) | p. 12 | ✅ | Option toggle in Appearance: see 3.10 |
| ⌘-double-click an inactive title to collapse it in place | p. 12 | ✅ | Falls out of ⌘-drag (0.1.54). |
| Zoom box: fit contents; Option-click zoom fills the screen | p. 12 | ✅ | Sniffer windows zoom to fit their icons or list (`platinum_shell_v1.set_standard_size`, v9); Option-click fills the screen; other programs' zoom fills it (0.1.54). |
| Collapse box; Option-click collapses all of the app's windows | p. 13 | ✅ | Only the program's own windows now (it was every window) (0.1.54). |
| Scroll bars; arrows together or at both ends (Appearance) | p. 13 | ◐ | An arrow at each end and a fixed-size thumb (Mac OS 9 without Smart Scrolling). Smart Scrolling (arrows together, proportional thumb) would change `lib/widgets` scroll bars and every caller's thumb arithmetic, the Qt style and the GTK theme: P3 L, not S. |
| ⌘-drag inside a Sniffer window scrolls in any direction (grabber hand) | p. 14 | ✅ | Control-click is Ctrl to clients too, so a Ctrl-press in a window that can scroll grabs when moved and shows the contextual menu when released still. |
| Drag window edges to move the window | p. 14 | ✅? | Verify. |
| Resize box with outline | p. 15 | ✅ | |
| **Proxy icon** in a Sniffer window's title: drag to move/copy the folder; drop onto it when collapsed | p. 15 | ✅ | zacos9-wm draws it (platinum-shell v12) and hands the press to the Sniffer, which drags the folder; Option copies. Home, disks, the Trash and the Macintosh view's folders show the icon but don't drag. A drag resting on a collapsed window's title bar springs it open for the drop. |
| Info strip ("7 items, 2.1 GB available") | p. 16 | ✅? | Verify. |
| Window sounds (collapse, close, zoom) | p. 13 | ✅ | `compositor/src/window_sounds.c` |

### 3.2 Sniffer views and icons (ch. 1–2)

| Feature | Book | Status | Notes |
|---|---|---|---|
| Icon, list, button views per window, remembered | p. 17 | ✅ | |
| **Small-icon view** (names beside icons) | p. 18 | ✅ | View Options ▸ Icon Size; buttons too. |
| Clean Up; ⌘-drag snaps one icon to the grid | p. 18 | ✅ | Windows and the desktop. |
| Arrange by name/date/size/kind/label | p. 19 | ✅ | |
| **Always snap to grid**, **Keep Arranged** (view options) | p. 18–20 | ✅ | Windows and the desktop; Arrange changes the order kept. |
| Grid spacing Tight / Wide (Preferences) | p. 18 | ✅ | Windows, buttons and the desktop. |
| List view: column headings sort; reverse-sort pyramid | p. 16, 23 | ✅ | The pyramid sits above the vertical scroll bar; the direction is kept per folder. |
| Columns: Date Modified, Date Created, Size, Kind, Label, Comments, Version — chosen per window | p. 20–22 | ✅ | Comments are Get Info's (`user.xdg.comment`); Version is an application's package version, "n/a" otherwise. List icons standard, small or tiny (12 px generic shapes). |
| Drag column headers to reorder; drag dividers to resize; dates/sizes shorten as the column narrows | p. 22 | ✅ | Name stays first. Dates: long, medium, "10/1/26, 3:42 PM", "10/1/26". |
| Reset Column Positions | p. 415 | ✅ | View menu, list view. |
| Use relative dates ("Today", "Yesterday"); Calculate folder sizes | p. 24 | ✅ | Folder sizes are added up on another thread and filled in as they finish. |
| Disclosure triangles; ⌘-→/⌘-← open/close them | p. 23 | ◐ | Triangles ✅; keys to verify. |
| Button view; drag a button by its name | p. 23 | ✅ | |
| **View Options** dialog (per view); Set to Standard Views | p. 24, 415 | ✅ | New folders inherit their parent window's view, else the standard views. |
| **Pop-up windows**: as Pop-up Window, tabs along the bottom of the screen, spring open on drag | p. 27 | ✅ | A frame style of `zacos9-wm` (`platinum-shell-v1` 14: `popup`, `set_popup_tab`, `popup_tab`, `popup_detach`): the tab opens and closes it, a click elsewhere or another window coming forward closes it, drag along to move, drag up to make it a window, a drag resting on the tab springs it open, its top corners resize it. The Sniffer remembers it and the tab's place per folder. Tab drawn to match the title bar, not measured. |
| Select by typing, Tab/Shift-Tab alphabetically, arrows, Shift-click, drag-select | p. 33 | ✅? | Verify Tab order. |
| Rename by clicking the name; Undo while editing | p. 31 | ✅ | |
| Option-drag copies; ⌘-Option-drag makes an alias | p. 34–37 | ✅ | |
| Paste a picture over the icon in Get Info for a custom icon | p. 32 | ✗ | Write `metadata::custom-icon` (gio) or a `.directory`. P3 M |
| Labels: seven, applied from File ▸ Label; names and colours editable in Preferences | p. 40 | ✅ | Colours from the 16-colour system palette rather than a full colour picker. |
| Spring-loaded folders; delay slider; Space opens at once | p. 35 | ✅ | On/off and delay in Preferences; Space during a drag opens the folder at once (`platinum-shell-v1` 15 `drag_spring`: programs get no keys during a drag, so `zacos9-wm` tells the one under it). |
| **Toy Box** (buttons in categories, pages; Option-drag between pages) | p. 43 | ✅ | Control Panels ▸ Toy Box; System Folder ▸ Toy Box Items; bulleted folders are pages. |
| Trash: Empty Trash…, warn-before-emptying option (the ellipsis disappears), locked items need Option | p. 46 | ✅ | Warn before emptying in the Trash's Get Info (`trash-warn`) ✅; without it Empty Trash has no ellipsis and doesn't ask, except before uninstalling applications. Locked items (can't be changed) stay, with their Put Away records, and Empty Trash says so even without the warning; Option-Empty Trash removes them too without asking. |
| Put Away takes items out of the Trash back to where they were | p. 412 | ✅? | Verify for files from the Trash. |
| **Encrypt** a file (password, optional Keychain) | p. 49, 412 | ✗ | `age`/gpg with the password kept in Secret Service; double-click asks. P3 M |
| Rebuild the desktop (generic icons) | p. 51 | — | No desktop database; MIME cache refresh is automatic. |

### 3.3 Sniffer menus (Appendix A)

| Menu item | Status | Notes |
|---|---|---|
| File ▸ New Folder, Open, Move To Trash, Close Window, Get Info, Label, Duplicate, Make Alias, Put Away, Find…, Show Original | ✅ | |
| File ▸ **Print** (⌘P) — prints the selected documents through their apps | ◐ | PDFs, PostScript, text and pictures go to your printer with `lp`; other documents get an alert to print from their own program (opening the app with its print action is still to do). |
| File ▸ Get Info ▸ General Information / Sharing / Memory submenu | ◐ | General and Sharing ✅; Memory — (out of scope) |
| File ▸ **Add To Favorites** | ✅ | Aliases in `~/.config/zacos9/Favorites` (System Folder ▸ Favorites); also on icons' and disks' contextual menus (0.1.54). |
| File ▸ **Encrypt** | ✗ | See 3.2. P3 |
| File ▸ **Search Internet…** (⌘H) | ✗ | Opens the search window on its Internet channel. P3 S |
| File ▸ **Page Setup…**, **Print Window/Print Desktop…** | ✗ | Render the window with the existing painter to PDF, then print. P3 M |
| Edit ▸ Undo, Cut, Copy, Paste, Clear, Select All | ◐ | Edit menu sends keys to apps ✅; in the Sniffer: copying icons copies their names (p. 414), to verify. |
| Edit ▸ **Show Clipboard** | ✅ | `shell/finder/clippings.cpp`: a window that follows the clipboard (text or picture), "Clipboard contents: …" below. |
| Edit ▸ **Preferences…** (General: Puppy Mode, spring-loaded, grid; Views: standard views; Labels) | ◐ | All but Puppy Mode (Simple Finder), which waits for account kinds (P3). |
| View ▸ as Icons/Buttons/List, Clean Up, Arrange | ✅ | |
| View ▸ **as Window / as Pop-up Window** | ✅ | See 3.2. |
| View ▸ **Sort List** (in list view), **Reset Column Positions**, **View Options…** | ◐ | Sort List, Reset Column Positions and View Options… ✅. |
| Special ▸ Empty Trash…, Erase Disk…, Sleep, Restart, Shut Down | ✅ | Erase Disk: format choices (Mac OS Extended, DOS/FAT, exFAT) to verify. |
| Special ▸ **Eject** (⌘E) | ✅ | Every selected disk, local or network; replaces Disconnect Network Drive and Unmount Disk (0.1.54). |
| Special ▸ **Log Out** (⇧⌘Q) | ✅ | Asks, quits the programs (each may offer to save), then ends the session; a program still open after 30 s cancels it (0.1.54). The menu doesn't show ⇧⌘Q yet (the painter has no ⇧ glyph). |
| Help ▸ **Help Center**, **Show/Hide Hint Bubbles**, **ZacOS Help** | ◐ | Help Center and ZacOS Help (⌘-?) ✅; Hint Bubbles wait for 3.17. |

### 3.4 Contextual menus (ch. 4 p. 64)

| Feature | Status | Notes |
|---|---|---|
| Control-click (and right-click) on icons, window backgrounds, the desktop, the Trash, disks | ✅ | The Sniffer asks; the menu bar opens it at the pointer (press-drag-release, or click for a sticky menu), as its own menus (0.1.54). ⌘-click opens one too (⌘ reaches apps as Control). |
| Icon menu: Open, Get Info, Label, Duplicate, Make Alias, Add To Favorites, Move To Trash, Print | ◐ | Open, Move To Trash, Get Info, Label ▸, Duplicate, Make Alias ✅; disks add Eject and Erase Disk…, the Trash Empty Trash…. Add To Favorites and Print wait for those features. |
| Window/desktop menu: New Folder, View as …, Clean Up, Arrange, Sort List, View Options, Change Desktop Background | ◐ | New Folder, View ▸, Clean Up, Arrange ▸, Get Info, Close Window; the desktop's Change Desktop Background… ✅. Sort List (list view) and View Options… ✅ too. |
| Find Similar Items, Index Selection | ✅ | File menu; see 3.13. |
| Attach a Folder Trick | ✗ | Needs folder tricks (3.12). P3 |
| Summarize Text to Clipboard | — | Out of scope (or an LLM-free extractive summary later). |
| Add-on commands from a folder (Contextual Menu Items) | ✗ | `.desktop` file-manager actions in `~/.local/share/zacos9/contextual`. P2 S |
| Contextual menus in other apps | ✅ | The apps' own. |

### 3.5 Zac menu (ch. 5)

| Feature | Status | Notes |
|---|---|---|
| About This Computer (memory bars per app) | ✅ | Memory per app is shown? Verify. |
| Items from a folder; aliases; folders sprout submenus | ◐ | Items ✅; folder submenus to verify. P2 S |
| **Recent Applications / Recent Documents / Recent Servers** | ✅ | Programs whose windows appeared; `recently-used.xbel` (the Sniffer adds what it opens); servers Neighbourhood connected to, reconnected with `--connect` (0.1.54). |
| **Favorites** submenu (and Add To Favorites) | ✅ | (0.1.54) |
| **Zac Menu Options** panel: submenus on/off, how many recent items | ◐ | How many of each ✅ (0.1.54); submenus are always on. |
| Control Panels submenu | ✅ | |
| Printer Picker | ✅ | See 3.15. |
| Calculator, Key Tags, Notepad, Stash, Dog Ears, Bark Recorder, Jigsaw Puzzle, Plotter | ✗ | See 3.18. |
| Pedigree | ✗ | See 3.18. |
| Neighbourhood | ✅ | |
| Remote Access Status | — | Dial-up; out of scope. |
| Bloodhound | ◐ | Find with criteria and contents; Internet channels still to do. See 3.13. |
| Automated Tasks (scripts) | ✗ | See 3.12. |
| Force Quit (ZacOS addition) | ✅ | |

### 3.6 Using programs (ch. 6)

| Feature | Status | Notes |
|---|---|---|
| Application menu at the right: lists apps, Hide, Hide Others, Show All, current app's name | ✅ | |
| Ribbed handle to show/hide the app name in the menu bar | ✅ | Drag it left for the name, right for the program's own icon (`shell/menubar/appicon.c`: its desktop entry's icon from the icon theme, hicolor or pixmaps, through gdk-pixbuf; the generic application icon if none loads); a click switches (`app-menu-name`). Handle not measured. |
| **⌘-Tab / ⇧⌘-Tab** cycles programs | ✅ | By name, the Sniffer among them; hidden programs come back (0.1.54). |
| **Option-click the desktop / Option-choose an app** hides the one you leave | ✅ | Desktop (0.1.54); Option-choosing a program in the Application menu (the menu bar reads Option from its keyboard while the menu is open). |
| **Pack palette** (tear off the Application menu; vertical/horizontal, small/large icons, names on/off; drop documents on tiles to open them) | ✅ | `zacos9-pack`, a layer-shell palette above the windows; zoom box (names), Option-zoom (large), Shift-Option-zoom (across), collapse and close boxes; reopens at login. Not done: dragging its edge to set the width. |
| **F-keys launch** programs/documents/folders (Keyboard panel ▸ Function Keys); pressing again brings it forward | ✅ | `compositor/src/fkeys.c`; only assigned keys are taken; "Use F1 through F12 as function keys" makes it ⌘-F-key. |
| ⌘-period cancels a launch | — | |
| Option-double-click an app closes its window | ✅ | Any icon in a Sniffer window, as Mac OS 9: it opens and the window it was in closes. |
| "Application not found" → File Translator offers apps that can open the type | ✅ | File Translator panel and Open With. |
| Type/creator codes | — | MIME types instead. |

### 3.7 Memory (ch. 7) — out of scope

Application memory sizes, virtual memory, the disk cache and RAM disks
don't apply. **About This Computer** should keep showing honest memory use.
A zram/swap switch in a Memory panel is a possible P3.

### 3.8 Open and Save dialogs (ch. 8)

| Feature | Status | Notes |
|---|---|---|
| Open/Save through the ZacOS file-chooser portal | ✅ | `shell/finder/filechooserportal.cpp` |
| Keyboard navigation (type-select, arrows, ⌘-↑, ⌘-D desktop, ⌘-N new folder) | ✅? | Verify against p. 127. |
| Shortcuts, **Favorites** and **Recent** pop-ups (Navigation Services) | ✅ | Favorite folders are offered as locations (0.1.54); a Recent button on the location list and the file dialog lists recently-used.xbel's documents and folders (Zac Menu Options' count): a document opens its folder with it selected. No separate Shortcuts pop-up: the location list is that. |
| Drag a folder onto the dialog to jump there | ✅ | It didn't: the dialog now takes a dropped folder (or file, selected) and opens again in the location holding it. |
| General Controls ▸ Documents: default save folder | ✅ | Folder set by the application (the list of disks, as before), last folder used in the program, or Documents; the latter two open straight in the folder, with Other Locations… |

### 3.9 Moving data (ch. 9)

| Feature | Status | Notes |
|---|---|---|
| Cut/Copy/Paste, drag and drop between apps | ✅ | |
| **Clipping files**: drag text or pictures to the desktop or a folder | ✅ | `shell/finder/clippings.cpp`. Text → "First words clipping.txt", a picture → "picture clipping.png", a link → an Internet location "Title.url" ([InternetShortcut]; opens in the browser). Ordinary files other programs open; they land where dropped, have their own icons and kinds, open in a viewer window, and dragged into a document carry their text or picture (an Internet location its address). |
| Show Clipboard window | ✅ | See 3.3. |
| Publish and Subscribe | — | |
| Exchanging files with Windows (File Translator, PC disks) | ✅ | |

### 3.10 Look and feel (ch. 3) and control panels (ch. 12)

| Control panel / feature | Status | Notes |
|---|---|---|
| **Appearance**: Themes, Color (accent, highlight), Desktop (patterns, pictures), Sound (sound sets) | ✅ | Desktop picture placement — Tile / Center / Scale / Position Automatically — to verify. |
| Appearance ▸ **Fonts** (Large System Font, Small System Font, Views font and size, Smooth all fonts) | ✗ | P2 M — the bitmap fonts are fixed; offer the Views size and smoothing for apps (GTK/Qt settings). |
| Appearance ▸ **Options** (smart scrolling, double-click title to collapse) | ◐ | Options tab ✅ with "Double-click title bar to collapse windows" (`title-collapse`; it was always on). Smart Scrolling not done: see Scroll bars in 3.1. |
| **Zac Menu Options** | ◐ | Recent counts ✅ (0.1.54); submenus always on. |
| AppleTalk | — | |
| ColorSync | — | colord is out of scope for now. |
| **The Collar** panel (show/hide, hot key, font) | ✅ | The Collar panel. |
| **Date & Time**: date/time, time zone, network time, clock options | ✅ | **Date/Time Formats** and **hourly chimes** missing. P3 S each |
| DialAssist, Modem, Remote Access | — | |
| **Nap Time**: sleep, display sleep | ✅ | **Scheduled start-up and shut-down** missing (`rtcwake` + a systemd timer, via a helper). P3 M |
| **Tricks Manager** (sets, item information) | ◐ | Catalog exists (`lib/extensions.c`); on/off sets for autostart items and user services to plan. P3 M |
| File Translator | ✅ | |
| File Sharing | ✅ | |
| **Retriever** (folder pairs) | ✗ | rsync two-way with newest-wins and a warning. P3 M |
| **General Controls**: show desktop in background, Toy Box at startup, shut-down warning, insertion-point and menu blinking, Documents folder for Open/Save | ✅ | `zacos9-controlpanel general`. Show Desktop, Menu Blinking, Insertion Point Blinking and Documents done. Show Toy Box at system startup ✅; Shut Down Warning (fsck covers it) and Folder Protection (already dead in Mac OS 9) left out. |
| **Internet** (email, web, default apps, downloads folder) | ✅ | `zacos9-controlpanel internet`: default browser and mail program (GIO, `mimeapps.list`), downloads folder (`user-dirs.dirs`). The Mac's personal details, news and home-page settings have no system-wide place on Linux. |
| **Keyboard**: repeat rate/delay | ✅ | |
| Keyboard ▸ **layouts** and the keyboard menu (flag at the right of the menu bar, ⌘-Space to switch) | ✅ | Up to four ticked in the Keyboard panel (xkb's group limit), else Debian's `/etc/default/keyboard`; an abbreviation (US, US-DV) instead of a flag; ⌘-Option-Space option. `compositor/src/kblayout.c`, `shell/menubar/kbmenu.c`. |
| Keyboard ▸ **Function Keys** | ✅ | See 3.6. |
| **Keychain Access** | ◐ | The Collar's Keychain module ✅; a control panel listing items, Lock/Unlock — use Seahorse or build one. P3 M |
| **Toy Box** | ✅ | See 3.2. |
| **Walkies** (snapshots of network, sound, time zone, printer, startup items) | ✗ | P3 L |
| Memory | — | |
| **Monitors**: resolution, pixel size | ✅ | Several screens and arrangement still on the roadmap; colour depth — . |
| **Mouse**: tracking, double-click | ✅ | Options: Mouse Tracks (`compositor/src/tracks.c`: six arrows behind the moving pointer, going 50 ms apart) and Thick I-Beam (a `text-thick` cursor zacos9-wm shows for programs that ask for "text" by cursor-shape-v1; Qt 6 programs ask that way and get it (checked with Scribble); ones that draw their own cursor, such as GTK 3, keep the thin one). |
| **Users** | ✅ | See 3.14. |
| **Numbers** / **Text** (number and sort formats, language) | ✗ | locale via `localectl`/`~/.config/locale.conf`. P3 S |
| Media Settings (CD AutoPlay) | ✗ | Audio CD/media autoplay as a Sniffer setting. P3 S |
| **Fetch Updates** | ✅ | Scheduled checks to verify. |
| **Sound**: alert sounds, output volume, input source | ◐ | Alert sound and output ✅; **Input** source and **Speaker balance** missing. P3 S |
| **Speech** (voice, talking alerts) | ✗ | See 3.16. |
| **Startup Disk** | ✅ | `zacos9-controlpanel startupdisk`: GRUB's entries (other versions behind Show every version), the one it starts with selected; choosing one sets it through `zacos9-startupdisk-helper` (polkit; GRUB_DEFAULT=saved the first time), Look for Other Systems turns os-prober on, Restart. "Old Dog starts up with:" picks which of Old Dog's disks with a System Folder the Mac starts from (`~/.config/zacos9/classic.conf`, read by `zacos9-classic`). |
| **TCP/IP** | ✅ | |
| **Trackpad** (tap to click, drag, drag lock) | ✅ | `zacos9-controlpanel trackpad`: tracking and double-click speed (shared with Mouse), Use Trackpad for Clicking / Dragging / Drag Lock, set on libinput touchpads by zacos9-wm (`input.c`). |
| **Web Sharing** (personal web page, on/off) | ✗ | A small static server for `~/Web Pages` behind a switch. P3 S |
| **Accessibility** (Sticky Keys, Slow Keys, Mouse Keys) and **Zoom** (zoom) | ✅ | `zacos9-controlpanel accessibility`, done in zacos9-wm (`compositor/src/access.c`). Sticky Keys: latch, lock (twice), Shift ×5 toggles, menu bar shows held keys (platinum-shell-v1 v13 `sticky_keys`). Slow Keys: acceptance delay (physical keyboards only). Mouse Keys: keypad moves/clicks, 0 holds, . lets go, ⌘-Shift-Clear on. Zoom: CloseView-style, 2–16× about the pointer, ⌘-Option-K, ⌘-Option-↑/↓. No colour inversion (wlroots' render pass can't invert). |

### 3.11 The Collar (The Collar, ch. 4 p. 68)

| Module | Status | Notes |
|---|---|---|
| Sound volume, Sound source | ◐ | Volume ✅; input source P3 S |
| Monitor resolution, brightness | ✅ | |
| Bit depth, TV/Video Mirroring | — / ✗ | Mirroring on/off belongs with multi-monitor work. P3 |
| Energy (sleep now), battery | ✅ | |
| File Sharing (on/off, who's connected) | ✅ | `shell/collar/filesharing.cpp`: shown once File Sharing is set up; on/off through the sharing helper (the servers that were on come back); visitors by address from `ss` (no root needed, so no user names). |
| Keychain | ✅ | |
| Walkies | ✗ | With 3.10. |
| AppleTalk, Remote Access, AirPort | ◐ | Network module with Wi-Fi ✅; dial-up — |
| CD audio (play, tracks, eject) | ✅ | As a media module over MPRIS (`shell/collar/media.cpp`): what's playing, Play/Pause, Next/Previous Track, Show the player, a choice of player when several run. Eject belongs to the Sniffer. |
| Printer selector | ✅ | `shell/collar/printing.cpp`: shown once CUPS knows a printer; yours ticked, choosing another makes it yours; Open Print Queue, Open Printer Picker. |
| Web Sharing | ✗ | With 3.10. |
| Speakable Items | — | |
| Option-drag to move The Collar and its modules; ⌘-Ctrl-S hot key | ✅ | |

### 3.12 Automation (ch. 10)

| Feature | Status | Notes |
|---|---|---|
| AppleScript recording and Script Editor | — | No equivalent language; don't imitate. |
| **Folder Tricks** (run a script when items are added to a folder) | ✗ | P3 M. inotify watcher in the Sniffer; scripts in `~/.local/share/zacos9/folder-actions`; attach from the contextual menu. |
| **Scripts in the Zac menu** (Automated Tasks) | ✗ | P3 S. Executable files in `Menu Items/Automated Tasks` run when chosen (already mostly works through Menu Items). |
| Scriptable Sniffer (open, label, comments from scripts) | ◐ | The Sniffer socket protocol could get a documented CLI (`zacos9-finder-ctl label …`). P3 S |

### 3.13 Search: Bloodhound (ch. 15)

| Feature | Status | Notes |
|---|---|---|
| Find by name, results as a Sniffer view | ✅ | |
| Criteria: dates, comments, size, kind, label, version, locked, empty/shared folder, invisible, custom icon | ◐ | `shell/finder/findcriteria.cpp`: where (all disks, Home, the front folder) and up to six rows that must all match: name (contains, starts/ends with, is, is not, doesn't contain), date modified/created (today, past week/month, before/after a date), size, kind, label, lock, folder empty/shared. Comments, version, invisible and custom icon not yet. |
| **Search contents** with an index; schedule indexing; Index Selection | ✅ | `shell/finder/findindex.cpp`: "contents include" asks localsearch (tinysparql full-text query, kept to the chosen place; the other rows sift its answers); File ▸ Index Selection adds folders to its index. Scheduling is localsearch's own. |
| Find Similar Items | ✅ | The document's most distinctive words, any of them, ranked by the index. |
| Save a search; several searches at once | ✗ | P3 S |
| Internet channels (web search with plug-ins) | ✗ | P3 M. One search box over a few engines; opens the browser. |

### 3.14 Users and login (ch. 17)

| Feature | Status | Notes |
|---|---|---|
| Login window with a user list and password | ✅ | `zacos9-login` (0.1.54); the chosen person's picture from the Users panel (accountsservice's icons folder), or a blank face with their initial. |
| **Log Out** (Special menu), automatic logout or lock after idle | ✅ | Log Out ✅ (0.1.54); lock after idle ✅ (Screen Saver) |
| **Users panel**: add/remove accounts, picture, can change password, can log in, can manage accounts | ✅ | `zacos9-controlpanel users` over accountsservice (D-Bus, its own polkit rules: your name and picture freely, the rest with an administrator's password through the session's polkit agent); passwords hashed in the panel by crypt(3). You can't delete, demote or lock yourself. |
| Account kinds: Normal, **Limited** (chosen apps only), **Puppy Mode** (the simplified desktop for children) | ✗ | P3 L. Limited: allowed-apps list enforced by the Sniffer and menu bar (not a security boundary — say so). |
| Global options: welcome message, guest account, speak your name, alternate password | ◐ | Welcome message P3 S; guest account P3 M; voice login — . |
| Shared Documents folder | ✗ | `/home/Shared` with a group. P3 S |
| Fast user switching | — | Mac OS 9 didn't have it. |

### 3.15 Disks, printing and fonts (ch. 18–19)

| Feature | Status | Notes |
|---|---|---|
| Disks on the desktop, Put Away/eject, drag a disk to the Trash to eject | ◐ | Network volumes eject via the Trash (0.1.54); local disks to verify. P2 S |
| Erase Disk with format choice | ◐ | Verify formats. |
| **Disk Vet** (verify/repair) | ✗ | fsck of unmounted volumes through `zacos9-disk-helper`; SMART status. P3 M |
| **Disk Setup** (partition, initialize) | ◐ | `diskinit.cpp` initializes; partitioning — P3 L |
| Disk Copy (mount images) | ✅ | Mac disk images; ISO/IMG via udisks to verify. |
| **Printer Picker** (pick a printer; AppleShare servers) | ✅ | `zacos9-printerpicker`, in the Zac menu: the printers CUPS knows and finds (IPP Everywhere, `lpstat -e`), with model, location and status; clicking one makes it yours (`lpoptions -d`, no password); Create Desktop Printer, Print Queue, Settings… (system-config-printer). Servers stay in the Neighbourhood. |
| **Desktop printer icons**: drag documents to print, queue window (pause, reorder, remove), default printer (bold outline) | ◐ | `NAME.printer` files on the desktop (`[Desktop Printer]` Queue=…) with a printer icon: documents dropped on one print there, opening one opens `zacos9-printqueue` (printer status, documents, Hold/Resume, Remove, Stop/Start Printing through cups-pk-helper and a polkit rule for administrators; drops print). Reordering jobs and the default's bold outline are still to do. |
| Print Queue / non-modal print errors | ✗ | Notification-style messages (Mac OS 9's yellow box, p. 2). P3 S |
| Page Setup / Print dialogs | ✅ | Apps use their own (GTK/Qt). |
| Fonts folder (drop to install) | ✗ | Drop onto `Fonts` copies to `~/.local/share/fonts` and runs fc-cache. P3 S |
| FontSync, ColorSync | — | |

### 3.16 Sound, movies and speech (ch. 20–21)

| Feature | Status | Notes |
|---|---|---|
| Alert sounds; record your own alert sound | ◐ | Choosing ✅; recording one (Sound ▸ Add…) P3 S |
| **Bark Recorder** (record/play) | ✗ | P3 S (pw-record). |
| QuickTime Player | ✅? | Any installed player opens movies; a Platinum player is not planned. |
| Startup/Shutdown sounds (put a sound in Startup Items) | ✗ | With Startup/Shutdown Items. P3 S |
| **Text-to-speech**: voices, Talking Alerts, read selected text | ✗ | P3 M. speech-dispatcher/espeak-ng. |
| Speech recognition (Speakable Items) | — | |

### 3.17 Help (Appendix A)

| Feature | Status | Notes |
|---|---|---|
| **Help Center** (browser-like, searchable HTML help) | ✅ | `zacos9-help` (`shell/help/`): Back, Forward, Help Center and a search field above the page; the Help Center lists every book in `share/zacos9/help` (a folder with an index.html); search ranks every page (title words count most) with the best sentence and a relevance bar. ZacOS Help: 14 pages (basics, the Sniffer, files, Find, Zac menu, programs, The Collar, Old Dog, control panels, accessibility, networks, shortcuts, troubleshooting). |
| **Hint Bubbles** (Show Hint Bubbles) | ✗ | P3 M. Bubbles drawn by the menu bar over its menus, and by our own panels from their tooltips; other apps' tooltips stay as they are. |
| ZacOS Help from the Sniffer | ✅ | Help ▸ Help Center and ZacOS Help (⌘-?) when the Sniffer is in front. |

### 3.18 Desk accessories and utilities (ch. 5, 13)

| Program | Status | Suggested approach |
|---|---|---|
| **Dog Ears** | ✅ | `zacos9-dogears`, in the Zac menu: coloured note windows saved as you type (`dog-ears.json`), New Note, Import/Export Text, Color menu; closing a note offers to save its text. |
| **Notepad** | ✗ | P3 S. Paged notes in one window. |
| **Stash** | ✗ | P3 M. Pages of pictures/text; drag in and out. |
| **Calculator** | ✅ | `zacos9-calculator`, in the Zac menu; typed or clicked; ⌘C/⌘V. |
| **Key Tags** | ✗ | P3 S. Shows the current xkb layout, Option/Shift states. |
| **Scribble** | ◐ | `zacos9-scribble`: plain text (UTF-8 or Latin-1; CR, CR LF or LF kept), New/Open/Save/Save As, Find/Find Again, Font and Size menus. Styled text and Speech not done. |
| **Pedigree** | ✅ | `zacos9-pedigree`, in the Zac menu and behind the Sniffer's System Information: tabs System Profile (system, hardware, battery, displays, network) and Devices and Volumes (disks and their volumes, USB, PCI, Bluetooth, sound), outlines opened by their triangles; File > Save Report, Edit > Copy, View > Refresh/Open All/Close All. Read from /sys, /proc, os-release, dpkg and pci.ids/usb.ids, no password: memory slots and serials only root can read are left out. |
| Plotter, Jigsaw Puzzle | ✗ | P3 — optional fun. |
| AppleCD Audio Player, DVD Player, Apple Video Player | — | Use regular media apps; The Collar's media module covers control. |
| Disk Copy, Disk Vet, Disk Setup | see 3.15 | |

### 3.19 System folder, startup and troubleshooting (ch. 11, App. B–C)

| Feature | Status | Notes |
|---|---|---|
| Startup Items | ✅ | XDG autostart in the Macintosh view |
| **Shutdown Items** | ✗ | Run on logout before the session ends. P3 S |
| Extension parade at startup | ✅ | `boot/` |
| Shift at startup: Extensions Off (safe mode) | ✗ | Hold Shift at login → a session without autostart items. P3 S |
| "Your computer did not shut down properly" + disk check | — | systemd/fsck handle it. |
| Custom/clean install, Installer | ✅ | ZacOS installer. |

---

## 4. Out of scope (and why)

- **Application memory sizes, virtual memory, disk cache, RAM disk** — Linux
  manages memory.
- **AppleTalk, LocalTalk, DialAssist, Modem, Remote Access (dial-up)** — no
  hardware or networks left that need them; AFP over TCP/IP is covered.
- **Publish and Subscribe, type/creator codes, desktop database rebuild,
  Extensions conflicts, PRAM** — no equivalent mechanism.
- **QuickTime registration, TV mirroring, colour depth, iTools** — obsolete.
- **AppleScript and speech recognition** — no faithful Linux equivalent;
  folder actions and scripts cover the useful part.
- **Copying Apple software, sounds, icons or names** — never (see CLAUDE.md).

---

## 5. Implementation plan

Order: what is used daily first, then what makes ZacOS feel complete, then
the long tail. Each package is one feature per session (CLAUDE.md rule 5),
lands with tests in the style already used (C/Qt unit tests, headless
`zacos9-wm` + `grim` UI tests, VM tests through `scripts/dev-boot.sh`), and
updates `PROJECT_STATUS.md` and the relevant `docs/` page.

### Milestone 1 — Daily-use parity (P1)

| # | Work package | Components | Effort | Tests |
|---|---|---|---|---|
| 1.1 ✅ | **⌘-Tab / ⇧⌘-Tab** app cycling, and Option-click desktop or Option-choose an app to hide the one you leave | `compositor/src/hotkey.c`, `shell/menubar/toplevels.c` | S | Headless: three clients, ⌘-Tab order and focus; hidden state after Option-switch |
| 1.2 ✅ | **Sniffer contextual menus** for icons, window backgrounds, the desktop, the Trash and disks; Control-click and right-click | `shell/finder/` (popup surface), `lib/menudraw` | M | Headless screenshot of each menu; each command reaches `Finder::command` |
| 1.3 ✅ | **Special ▸ Eject (⌘E)** and **Log Out (⇧⌘Q)**, with "quit your programs first" | `shell/menubar/menus.c`, `session/` | S | VM: Log Out returns to the login window |
| 1.4 ✅ | **Window tricks**: Option-click close/collapse for all of an app's windows, ⌘-drag a background window, Option-zoom fills the screen | `compositor/src/input.c`, `view.c`, `frame.c` | S | Headless compositor tests (`compositor/tests/`) |
| 1.5 ✅ | **Folder-path menu** on ⌘-click of a Sniffer window's title, ⌘-↑ / Option-⌘-↑ | compositor → Sniffer via `platinum-shell-v1` (new event: title clicked with modifiers), Sniffer menu | M | Headless: ⌘-click title shows the path; choosing opens the parent |
| 1.6 ✅ | **Favorites**: File ▸ Add To Favorites, Zac menu Favorites, Favorites in Open/Save | `shell/finder/`, `shell/menubar/launch.c`, file-chooser portal | S | Unit: alias written; menu lists it |
| 1.7 ✅ | **Recent Applications / Documents / Servers** in the Zac menu, with counts from a new Zac Menu Options panel | menu bar (launch record), `recently-used.xbel`, Neighbourhood, `shell/panels/` | M | Unit: list ordering and limits |
| 1.8 ✅ | **General Controls** panel: show desktop in background, menu blinking, insertion-point blink rate, Documents folder for Open/Save, Toy Box at startup | `shell/panels/controlpanel.cpp`, menu bar, file-chooser portal, Qt/GTK cursor blink settings | M | Unit per setting; visual check |
| 1.9 ✅ | **Keyboard layouts** and the keyboard menu (flag/abbreviation at the menu bar's right, ⌘-Space to switch) | Keyboard panel, `compositor/src/prefs.c` (xkb), menu bar | M | Headless: switch layout, typed key changes |
| 1.10 ✅ | **Sniffer Preferences** (General, Views, Labels) and **View Options** (icon size, snap to grid, Keep Arranged, list columns, relative dates, folder sizes, Set to Standard Views) | `shell/finder/` | M | Unit: settings persist per window; screenshots |

### Milestone 2 — The rest of the classic desktop (P2)

| # | Work package | Components | Effort |
|---|---|---|---|
| 2.1 ✅ | **Pack palette** (tear-off from the Application menu; layouts; drop documents on tiles) | new layer-shell surface in the menu bar | M |
| 2.2 ✅ | **F-key launching** (Keyboard ▸ Function Keys; pressing again brings it forward) | Keyboard panel, compositor hotkeys | M |
| 2.3 ✅ | **Pop-up windows** (View ▸ as Pop-up Window; tabs at the screen bottom; spring open during drags) | `zacos9-wm` frame style + Sniffer | L |
| 2.4 ✅ | **Proxy icon** in Sniffer title bars (drag to move/copy; drop onto it when collapsed) | compositor frame + `platinum-shell-v1` + Sniffer drag source | M |
| 2.5 ✅ | **Clippings** and **Show Clipboard** | Sniffer drop handling, clipping viewer | M |
| 2.6 ✅ | **Toy Box** (button view of `Toy Box Items`, categories as pages) | Sniffer | M |
| 2.7 ✅ | **Search**: more Find criteria now; then content search with an index (localsearch or recoll), Index Selection, Find Similar | `shell/finder/findwindow.cpp`, a search backend wrapper; packaging dependency | M + L |
| 2.8 ✅ | **Printing**: Printer Picker, desktop printers with queue windows, File ▸ Print for documents, default printer in The Collar | new `shell/print/` over CUPS; Sniffer | L |
| 2.9 ✅ | **Startup Disk** panel (GRUB entries, other OSes, Classic) | `shell/panels/`, polkit helper | M |
| 2.10 ✅ | **Internet** panel (default browser, mail app, downloads folder) | `shell/panels/`, `mimeapps.list`, xdg-user-dirs | S |
| 2.11 ✅ | **Trackpad** and **Accessibility** (Sticky/Slow/Mouse Keys, zoom) | prefs.c (libinput, xkb a11y), panels | M |
| 2.12 ✅ | **Desk accessories I**: Calculator, Dog Ears, Scribble | new small Qt apps on panelkit | S + M + M |
| 2.13 ✅ | **System Information** (Pedigree) | Qt app + read-only helper | M |
| 2.14 ✅ | **Users** panel (accounts, pictures, passwords, administrators) | panel over accountsservice + polkit | L |
| 2.15 ✅ | **Help Center** with ZacOS help pages | `shell/help/`, `share/zacos9/help/` | L |
| 2.16 ✅ | **Collar modules**: media playback (MPRIS), File Sharing on/off and visitors | `shell/collar/` | S each |
| 2.17 ✅ | Sniffer list-view columns: choose, reorder, resize, shortening dates/sizes, Reset Column Positions | `shell/finder/folderwindow.cpp` | M |

### Milestone 3 — Finishing touches (P3)

Small gaps left in features that already ship.

| # | Work package | Components | Effort |
|---|---|---|---|
| 3.1 ✅ | **⌘-drag scrolls** a Sniffer window with the grabber hand (a Ctrl-press waits: moved, it scrolls; still, it's a Control-click) | `shell/finder/folderwindow.cpp` | S |
| 3.2 ✅ | **Space opens a spring-loaded folder** at once (`platinum-shell-v1` 15 `drag_spring`: programs get no keys during a drag) | compositor `input.c`, `platinum_shell.c`; Sniffer | S |
| 3.3 ✅ | **Trash: Warn before emptying** in the Trash's Get Info; locked items stay unless Option-Empty Trash | Sniffer, menu bar | S |
| 3.4 ✅ | **Option-choosing a program** in the Application menu hides the one you leave | `shell/menubar/` | S |
| 3.5 ✅ | **Option-double-click** closes the window an icon was in; the **ribbed handle** switches the Application menu between the program's name and its own icon (dragged or clicked) | Sniffer, menu bar, `lib/menudraw`, `appicon.c` | S |
| 3.6 ✅ | Open/Save: **Recent** button; a **folder dropped** on the dialog opens there | `shell/finder/filechooserportal.cpp` | S |
| 3.7 ◐ | Appearance ▸ **Options** tab with double-click title bar to collapse ✅; **Smart Scrolling** (arrows together, proportional thumb) not done: it reaches every scroll bar, the Qt style and the GTK theme (L) | `shell/panels/appearance.cpp`, compositor | S + L |
| 3.8 ✅ | Reset Column Positions (was already done with 2.17) | `shell/finder/folderwindow.cpp` | — |
| 3.9 ✅ | **Login window** shows the chosen person's picture | `shell/greeter/`, panelkit | S |
| 3.10 ✅ | Mouse panel: **Mouse Tracks** and **Thick I-Beam** | compositor `tracks.c`, cursor theme, `controlpanel.cpp` | S |

### Milestone 4 — Startup, session and system settings (P3)

| # | Work package | Components | Effort |
|---|---|---|---|
| 4.1 | **Shutdown Items** and startup/shutdown sounds | `session/`, System Folder | S |
| 4.2 | **Shift held at login**: a session without startup items | `session/` | S |
| 4.3 | **Tricks Manager sets**: on/off sets for startup items and user services | `lib/extensions.c`, panel | M |
| 4.4 | **Nap Time** scheduled start-up and shut-down | `rtcwake` + systemd timer, polkit helper | M |
| 4.5 | **Date & Time** formats and hourly chimes | `controlpanel.cpp` | S + S |
| 4.6 | **Numbers / Text** panels (locale) | `localectl`, `locale.conf` | S |
| 4.7 | **Sound**: input source, speaker balance, record an alert sound; input source in The Collar | panels, `shell/collar/` | S each |
| 4.8 | **CD/media autoplay** as a Sniffer setting | Sniffer | S |

### Milestone 5 — Accounts, privacy and sharing (P3)

| # | Work package | Components | Effort |
|---|---|---|---|
| 5.1 | **Limited accounts**: allowed programs enforced by the Sniffer and menu bar (not a security boundary; say so) | Users panel, Sniffer, menu bar | L |
| 5.2 | **Puppy Mode** (Simple Finder) and its Sniffer preference | Sniffer | L |
| 5.3 | **Guest account** and a welcome message | Users panel, login window | M + S |
| 5.4 | **Shared Documents** (`/home/Shared` with a group) | `sharing/` | S |
| 5.5 | **Encrypt** (File and contextual menus): age or gpg, password in Secret Service | Sniffer | M |
| 5.6 | **Keychain Access** panel: list items, Lock/Unlock | `shell/panels/` | M |
| 5.7 | **Web Sharing**: a static server for `~/Web Pages` behind a switch | `sharing/`, panel | S |
| 5.8 | **Retriever** (folder pairs: two-way rsync, newest wins, with a warning) | new app | M |

### Milestone 6 — Help, automation and speech (P3)

| # | Work package | Components | Effort |
|---|---|---|---|
| 6.1 | **Hint Bubbles**: drawn by the menu bar over its menus, and by our panels from their tooltips | menu bar, panelkit | M |
| 6.2 | **Folder Tricks**: inotify watcher, scripts, "Attach a Folder Trick" in the contextual menu | Sniffer | M |
| 6.3 | **Automated Tasks** in the Zac menu | `Menu Items` | S |
| 6.4 | **`zacos9-finder-ctl`**: a documented CLI over the Sniffer's socket | Sniffer | S |
| 6.5 | **Text-to-speech**: voices, Talking Alerts, read selected text | speech-dispatcher / espeak-ng | M |
| 6.6 | **Search**: saved searches, several at once, Internet channels, File ▸ Search Internet (⌘H) | `findwindow.cpp` | S + M + S |

### Milestone 7 — Disks, fonts and printing (P3)

| # | Work package | Components | Effort |
|---|---|---|---|
| 7.1 | **Disk Vet**: fsck of unmounted volumes, SMART status | `zacos9-disk-helper` | M |
| 7.2 | **Fonts folder**: drop to install (copy, `fc-cache`) | Sniffer | S |
| 7.3 | **Print Window / Print Desktop** and Page Setup | Sniffer, `shell/print/` | M |
| 7.4 | Printing leftovers: non-modal print errors, job reordering, the default printer's bold outline | `shell/print/` | S |
| 7.5 | **Custom icons by pasting** in Get Info | Sniffer, gio metadata | M |
| 7.6 | **Disk Setup** partitioning | disk helper | L |
| 7.7 | Video mirroring on/off (with multi-monitor work) | compositor | M |

### Milestone 8 — Desk accessories and extras (P3)

| # | Work package | Effort |
|---|---|---|
| 8.1 | **Notepad** | S |
| 8.2 | **Key Tags** (current layout, modifier states) | S |
| 8.3 | **Bark Recorder** (`pw-record`) | S |
| 8.4 | **Stash** (pages of pictures and text) | M |
| 8.5 | **Walkies** (snapshots of network, sound, time zone, printer, startup items) | L |
| 8.6 | Plotter, Jigsaw Puzzle | optional |
| 8.7 | A Memory panel's zram/swap switch | optional |

### Cross-cutting work

- **Menu bar commands from apps.** Several items (Print, Log Out with "quit
  programs first") need a polite way to ask apps to quit or print: the
  existing global-menu plumbing (`appmenu.c`) can trigger an app's own
  Quit/Print items when present, and fall back to closing windows.
- **A Sniffer plug-in folder** for contextual commands and folder tricks,
  sharing one `.desktop`-actions format.
- **Settings.** New options go into `~/.config/zacos9/desktop.conf` with
  live reload, as the existing panels do.
- **Reference measurements.** Each new window/panel is drawn with
  `panelkit` and measured against period screenshots where available
  (`docs/reference/`); the book's figures are low-resolution scans, good
  for layout and wording, not pixels.
- **Trademarks.** Agree ZacOS names before shipping each feature (e.g. a
  dog-themed name for the search window, the Toy Box, Dog Ears).

### Suggested next three sessions

1. **6.1 Hint Bubbles** — seen everywhere; the Help Center was left
   waiting for it.
2. **4.1 + 4.2 Shutdown Items and the Shift-at-login safe session** —
   both small, both in `session/`.
3. **Smart Scrolling** (3.7's remainder) or **5.1 Limited accounts** —
   the next large pieces; each wants a short plan first.
