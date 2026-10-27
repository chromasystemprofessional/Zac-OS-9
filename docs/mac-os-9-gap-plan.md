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
| File Exchange | **File Translator** |
| Network Browser | **Neighbourhood** |
| Classic | **Old Dog** (runs the old Mac software) |
| Energy Saver | **Nap Time** |
| Platinum (appearance) | **Silver Coat** |

The Extensions Manager kept its Mac OS 9 name: 0.1.54 briefly showed it as
Tricks Manager, but that was confusing beside the Extensions folder. Its sets
still live in `~/.config/zacos9/Tricks Sets` for compatibility.

The developer docs (`docs/*.md`, `CLAUDE.md`) still call them by their old
names in places; the code and what users see use the new ones.

---

## 1. Summary

ZacOS already covers the core of the Mac OS 9 desktop: Platinum windows
(close, zoom, collapse, outline drag), the menu bar with global menus, a
spatial Sniffer (icon, list and button views, labels, aliases, spring-loaded
folders, Get Info with Sharing, Put Away, Find by name), the Zac menu with
Control Panels, The Collar (the Control Strip), Appearance, Date & Time, Energy
Saver, Screen Saver with lock, Mouse, Keyboard (repeat, layouts, function
keys), Monitors, Sound, TCP/IP, File Sharing, Neighbourhood, Keychain (in The
Collar), Fetch Updates, Force Quit, Classic, and a Platinum login window.

Implemented since the initial inventory: contextual menus, application
switching and The Pack, the Zac menu, most Sniffer commands and window tricks,
indexed and saved searches, most control panels and several accessories,
Printer Picker, and the Help Center. Those features are tracked individually
below.

The remaining gaps are concentrated in the long tail: smart scrolling and
other Appearance options; a few print-window and desktop-printer behaviors; Walkies and Retriever; account kinds and a shared
documents area; Disk Vet; speech and several desk accessories; and scheduled
sleep/startup/shutdown. The Trash warning and Option override for locked items,
Notepad, Bark Recorder and safe mode are implemented. Several rows are
explicitly marked for verification rather than assumed complete. Phase 4's
independent Classic-window experiment is tracked separately in
`docs/classic-seamless.md`; it has not passed its feasibility gate.

Section 3 lists every feature; section 5 is the plan.

---

## 2. How the mapping works on Linux

| Mac OS 9 mechanism | ZacOS equivalent to use |
|---|---|
| Apple Menu Items folder | `ZacOS 9 Menu Items` (already feeds the Apple menu) |
| Startup Items / Shutdown Items | XDG autostart (`shell/files/autostart.cpp`); `zacos9-shutdown-items` runs the user's shutdown folder after apps close and before logout |
| Control Strip modules | The Collar's modules (`shell/collar/`) |
| Contextual Menu Items folder | System Folder ▸ Contextual Menu Items: plain `.desktop` application entries, filtered by `MimeType=` |
| Desktop database, type/creator | MIME types (shared-mime-info) and desktop entries; `shell/files/fileassoc.cpp` |
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

### §3.1 Windows (ch. 1)

| Feature | Book | Status | Notes |
|---|---|---|---|
| Close box, ⌘-W | p. 9 | ✅ | |
| Option-click close box / Option-⌘-W closes all of the app's windows | p. 10 | ✅ | Option-click closes that program's windows; Option-⌘-W closes all open Finder windows, including auxiliary windows such as Get Info and Find. |
| Title-bar stripes show the active window; drag to move | p. 10 | ✅ | |
| ⌘-click title → folder-hierarchy menu; Option-⌘-click closes the inner window too | p. 11 | ✅ | Sniffer windows; through the Macintosh view (… Home, Zacintosh HD) (0.1.54). No small folder icons in the menu yet. |
| ⌘-↑ opens the enclosing folder; Option-⌘-↑ also closes this one | p. 12 | ✅ | (0.1.54) |
| ⌘-drag an inactive window without bringing it forward | p. 12 | ✅ | Title bar or edge (0.1.54). |
| Double-click title to collapse (Appearance option) | p. 12 | ✅ | Option toggle in Appearance: see §3.10 |
| ⌘-double-click an inactive title to collapse it in place | p. 12 | ✅ | Falls out of ⌘-drag (0.1.54). |
| Zoom box: fit contents; Option-click zoom fills the screen | p. 12 | ✅ | Sniffer windows zoom to fit their icons or list (`platinum_shell_v1.set_standard_size`, v9); Option-click fills the screen; other programs' zoom fills it (0.1.54). |
| Collapse box; Option-click collapses all of the app's windows | p. 13 | ✅ | Only the program's own windows now (it was every window) (0.1.54). |
| Scroll bars; arrows together or at both ends (Appearance) | p. 13 | ✅ | Arrows at each end and a fixed thumb by default; Smart Scrolling (`smart-scrolling`) puts them together with a proportional thumb in `lib/widgets` (every ZacOS window and list), the Qt style, and GTK 3 (theme steppers, plus a marked block in `~/.config/gtk-3.0/gtk.css`; the session turns GTK's overlay scroll bars off). GTK 4 has no arrows. |
| ⌘-drag inside a Sniffer window scrolls in any direction (grabber hand) | p. 14 | ✅ | Control-click is Ctrl to clients too, so a Ctrl-press in a window that can scroll grabs when moved and shows the contextual menu when released still. |
| Drag window edges to move the window | p. 14 | ✅ | Headless `window-tricks` verifies Command-dragging an inactive window by its title or edge moves it without raising it. |
| Resize box with outline | p. 15 | ✅ | |
| **Proxy icon** in a Sniffer window's title: drag to move/copy the folder; drop onto it when collapsed | p. 15 | ✅ | zacos9-wm draws it (platinum-shell v12) and hands the press to the Sniffer, which drags the folder; Option copies. Home, disks, the Trash and the Macintosh view's folders show the icon but don't drag. A drag resting on a collapsed window's title bar springs it open for the drop. |
| Info strip ("7 items, 2.1 GB available") | p. 16 | ✅ | Shows the current item count and available space; `finder-selection` verifies the text shape against its fixture. |
| Window sounds (collapse, close, zoom) | p. 13 | ✅ | `compositor/src/window_sounds.c` |

### §3.2 Sniffer views and icons (ch. 1–2)

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
| Disclosure triangles; ⌘-→/⌘-← open/close them | p. 23 | ✅ | In list view, Command-Right expands and Command-Left collapses the selected folder. |
| Button view; drag a button by its name | p. 23 | ✅ | |
| **View Options** dialog (per view); Set to Standard Views | p. 24, 415 | ✅ | New folders inherit their parent window's view, else the standard views. |
| **Pop-up windows**: as Pop-up Window, tabs along the bottom of the screen, spring open on drag | p. 27 | ✅ | A frame style of `zacos9-wm` (`platinum-shell-v1` 14: `popup`, `set_popup_tab`, `popup_tab`, `popup_detach`): the tab opens and closes it, a click elsewhere or another window coming forward closes it, drag along to move, drag up to make it a window, a drag resting on the tab springs it open, its top corners resize it. The Sniffer remembers it and the tab's place per folder. Tab drawn to match the title bar, not measured. |
| Select by typing, Tab/Shift-Tab alphabetically, arrows, Shift-click, drag-select | p. 33 | ✅ | Finder type-select and alphabetical Tab/Shift-Tab are covered by `finder-selection`; arrow, Shift-click and drag selection are covered there too. |
| Rename by clicking the name; Undo while editing | p. 31 | ✅ | |
| Option-drag copies; ⌘-Option-drag makes an alias | p. 34–37 | ✅ | |
| Paste a picture over the icon in Get Info for a custom icon | p. 32 | ✅ | Get Info copies/pastes through the Clipboard and stores bounded 32x32 PNG metadata in `user.zacos9.custom-icon`; Finder icons refresh after changes. |
| Labels: seven, applied from File ▸ Label; names and colours editable in Preferences | p. 40 | ✅ | Colours from the 16-colour system palette rather than a full colour picker. |
| Spring-loaded folders; delay slider; Space opens at once | p. 35 | ✅ | On/off and delay in Preferences; Space during a drag opens the folder at once (`platinum-shell-v1` 15 `drag_spring`: programs get no keys during a drag, so `zacos9-wm` tells the one under it). |
| **Toy Box** (buttons in categories, pages; Option-drag between pages) | p. 43 | ✅ | Control Panels ▸ Toy Box; System Folder ▸ Toy Box Items; bulleted folders are pages. |
| Trash: Empty Trash…, warn-before-emptying option (the ellipsis disappears), locked items need Option | p. 46 | ✅ | Warn before emptying in the Trash's Get Info (`trash-warn`) ✅; without it Empty Trash has no ellipsis and doesn't ask, except before uninstalling applications. Locked items (can't be changed) stay, with their Put Away records, and Empty Trash says so even without the warning; Option-Empty Trash removes them too without asking. |
| Put Away takes items out of the Trash back to where they were | p. 412 | ✅ | Restores files using the recorded Trash origin; `finder-selection` verifies the file and origin record are both restored/removed. |
| **Encrypt** a file (password, optional Keychain) | p. 49, 412 | ✅ | File ▸ Encrypt… creates a separate GPG AES-256 encrypted copy after confirming a passphrase; Decrypt… verifies the passphrase and saves an owner-only output file. Original files are preserved. After encrypting, it offers to add the passphrase to the keychain (`secret-tool`, keyed by the encrypted file's path); Decrypt uses it without asking. Tested by `finder-crypto`. |
| Rebuild the desktop (generic icons) | p. 51 | — | No desktop database; MIME cache refresh is automatic. |

### §3.3 Sniffer menus (Appendix A)

| Menu item | Status | Notes |
|---|---|---|
| File ▸ New Folder, Open, Move To Trash, Close Window, Get Info, Label, Duplicate, Make Alias, Put Away, Find…, Show Original | ✅ | |
| File ▸ **Print** (⌘P) — prints the selected documents through their apps | ◐ | PDFs, PostScript, text and pictures go to the chosen printer with `lp`; other documents now open in their registered applications, where the user chooses Print. Desktop apps have no common API for asking an arbitrary application to print a document. |
| File ▸ Get Info ▸ General Information / Sharing / Memory submenu | ◐ | General and Sharing ✅; Memory — (out of scope) |
| File ▸ **Add To Favorites** | ✅ | Aliases in `~/.config/zacos9/Favorites` (System Folder ▸ Favorites); also on icons' and disks' contextual menus (0.1.54). |
| File ▸ **Encrypt / Decrypt** | ✅ | Symmetric GPG encryption and decryption; see 3.2. |
| File ▸ **Search Internet…** (⌘H) | ✅ | Choose DuckDuckGo, Google or Bing, enter words, and open the URL-encoded query in the user's default browser. |
| File ▸ **Page Setup…**, **Print Window/Print Desktop…** | ✅ | Page Setup: paper (US Letter, US Legal, A4, A5, B5; Letter by default where US units are used), orientation, scale 50–200%, in `desktop.conf`. Print Window draws the whole window (every row or icon, not just what's scrolled into view; a list with its headings) on white with nothing selected; Print Desktop the desktop's icons, cut to where they are. Laid out on PDF pages with the name, date and page number, shrunk to the page's width if wider, sent to your printer with `lp`. No Print dialog (copies, page range): one copy of everything. |
| Edit ▸ Undo, Cut, Copy, Paste, Clear, Select All | ◐ | Edit menu sends keys to apps ✅; Command-C in the Sniffer copies selected item names as newline-separated text. Undo, Cut and Paste remain application-specific or unimplemented in the Sniffer. |
| Edit ▸ **Show Clipboard** | ✅ | `shell/files/clippings.cpp`: a window that follows the clipboard (text or picture), "Clipboard contents: …" below. |
| Edit ▸ **Preferences…** (General: Puppy Mode, spring-loaded, grid; Views: standard views; Labels) | ✅ | General ▸ Puppy Mode ▸ Use Puppy Mode (`puppy-mode`). |
| View ▸ as Icons/Buttons/List, Clean Up, Arrange | ✅ | |
| View ▸ **as Window / as Pop-up Window** | ✅ | See §3.2. |
| View ▸ **Sort List** (in list view), **Reset Column Positions**, **View Options…** | ✅ | Sort List, Reset Column Positions and View Options… ✅. |
| Special ▸ Empty Trash…, Erase Disk…, Sleep, Restart, Shut Down | ✅ | Erase Disk (USB disks) makes FAT32, and says so; other formats are in Disk Setup's Erase. |
| Special ▸ **Eject** (⌘E) | ✅ | Every selected disk, local or network; replaces Disconnect Network Drive and Unmount Disk (0.1.54). |
| Special ▸ **Log Out** (⇧⌘Q) | ✅ | Asks, quits the programs (each may offer to save), then ends the session; a program still open after 30 s cancels it (0.1.54). The menu doesn't show ⇧⌘Q yet (the painter has no ⇧ glyph). |
| Help ▸ **Help Center**, **Show Hint Bubbles**, **ZacOS Help** | ✅ | Help Center and ZacOS Help (⌘-?); Hint Bubbles in Qt tooltips, the menu bar's own menus (`shell/menubar/hints.c`) and every ZacOS panel's controls. GTK programs keep their own tooltips. |

### §3.4 Contextual menus (ch. 4 p. 64)

| Feature | Status | Notes |
|---|---|---|
| Control-click (and right-click) on icons, window backgrounds, the desktop, the Trash, disks | ✅ | The Sniffer asks; the menu bar opens it at the pointer (press-drag-release, or click for a sticky menu), as its own menus (0.1.54). ⌘-click opens one too (⌘ reaches apps as Control). |
| Icon menu: Open, Get Info, Label, Duplicate, Make Alias, Add To Favorites, Move To Trash, Print | ✅ | Open, Print (when the selection has a document, as File ▸ Print would print it), Move To Trash, Get Info, Label ▸, Duplicate, Make Alias, Add To Favorites; disks add Add To Favorites, Eject and Erase Disk…, the Trash Empty Trash…. |
| Window/desktop menu: New Folder, View as …, Clean Up, Arrange, Sort List, View Options, Change Desktop Background | ✅ | New Folder, View ▸, Clean Up and Arrange ▸ (icon view) or Sort List ▸ (list view), View Options…, Get Info, Close Window; the desktop's Change Desktop Background…. |
| Find Similar Items, Index Selection | ✅ | File menu; see 3.13. |
| Attach a Folder Trick | ✅ | On a folder icon, a window background or the desktop: Attach a Folder Trick ▸ (the Folder Tricks folder's programs, then Open Folder Tricks Folder) and Remove a Folder Trick ▸. See 3.12. |
| Summarize Text to Clipboard | — | Out of scope (or an LLM-free extractive summary later). |
| Add-on commands from a folder (Contextual Menu Items) | ✅ | Desktop entries in System Folder ▸ Contextual Menu Items (`~/.config/zacos9/Contextual Menu Items`) appear below the menu's own commands when every item clicked fits their `MimeType=` (none: anything; `inode/directory` for folders and window backgrounds; `image/*`; `all/allfiles`), and run through GIO with the items. Desktop-entry `Actions=` aren't listed separately. |
| Contextual menus in other apps | ✅ | The apps' own. |

### §3.5 Zac menu (ch. 5)

| Feature | Status | Notes |
|---|---|---|
| About This Computer (system and memory summary) | ✅ | Reports processor, graphics, total and available memory, and swap when the system exposes those values; it does not attribute memory per application. |
| Small icons before every item (Mac OS 9's Apple menu) | ✅ | A program's own (its desktop entry's Icon=, through the icon theme), else ZacOS's 16 px set: folders for submenus, a control panel's own kind in Control Panels, the generic application or document; a dimmed item's icon dimmed (`shell/menubar/appicon.c`, `launch.c`, `main.c`). Every control panel has its own original 16 px icon (`cp-*` in `assets/icons/platinum-icons.picon`; `build_icons.py` doubles a 16 px-only icon for 32 px). |
| Items from a folder; aliases; folders sprout submenus | ✅ | Top-level and nested `.desktop` launchers are sorted into the Zac menu; symlink aliases to files/folders open in the Sniffer; real directories containing entries become submenus (depth limited to four). Broken links, symlinked directories with newline paths, and deeper nesting are not traversed. |
| **Recent Applications / Recent Documents / Recent Servers** | ✅ | Programs whose windows appeared; `recently-used.xbel` (the Sniffer adds what it opens); servers Neighbourhood connected to, reconnected with `--connect` (0.1.54). |
| **Favorites** submenu (and Add To Favorites) | ✅ | (0.1.54) |
| **Zac Menu Options** panel: submenus on/off, how many recent items | ✅ | Per-category recent counts and the option to show items in submenus or directly (category-prefixed). |
| Control Panels submenu | ✅ | Every ZacOS panel, Keychain Access, Walkies, Retriever, Speech, Numbers and Text among them. |
| Printer Picker | ✅ | See 3.15. |
| Notepad, Calculator, Dog Ears | ✅ | See 3.18. |
| Stash | ✅ | See 3.18. |
| Key Tags | ✅ | See 3.18. |
| Jigsaw Puzzle, Plotter | ✅ | See 3.18. |
| Bark Recorder | ✅ | See 3.18. |
| Pedigree | ✅ | See 3.18. |
| Neighbourhood | ✅ | |
| Remote Access Status | — | Dial-up; out of scope. |
| Bloodhound | ◐ | Find with criteria and contents; Internet search is available from the Sniffer's File menu. See 3.13. |
| Automated Tasks (scripts) | ✅ | See 3.12. |
| Force Quit (ZacOS addition) | ✅ | |

### §3.6 Using programs (ch. 6)

| Feature | Status | Notes |
|---|---|---|
| Application menu at the right: lists apps, Hide, Hide Others, Show All, current app's name | ✅ | |
| Ribbed handle to show/hide the app name in the menu bar | ✅ | Drag it left for the name, right for the program's own icon (`shell/menubar/appicon.c`: its desktop entry's icon from the icon theme, hicolor or pixmaps, through gdk-pixbuf; the generic application icon if none loads); a click switches (`app-menu-name`). Handle not measured. |
| **⌘-Tab / ⇧⌘-Tab** cycles programs | ✅ | By name, the Sniffer among them; hidden programs come back (0.1.54). |
| **Option-click the desktop / Option-choose an app** hides the one you leave | ✅ | Desktop ✅ (0.1.54); Option-selecting an app from the Application menu hides the previous app, while leaving the Sniffer desktop available. |
| **Pack palette** (tear off the Application menu; vertical/horizontal, small/large icons, names on/off; drop documents on tiles to open them) | ✅ | `zacos9-pack`, a layer-shell palette above the windows; zoom box (names), Option-zoom (large), Shift-Option-zoom (across), collapse and close boxes; reopens at login. Not done: dragging its edge to set the width. |
| **F-keys launch** programs/documents/folders (Keyboard panel ▸ Function Keys); pressing again brings it forward | ✅ | `compositor/src/fkeys.c`; only assigned keys are taken; "Use F1 through F12 as function keys" makes it ⌘-F-key. |
| ⌘-period cancels a launch | — | |
| Option-double-click an app closes its window | ✅ | Asks the compositor to close the app's front window without quitting the app. |
| "Application not found" → File Translator offers apps that can open the type | ✅ | File Translator panel and Open With. |
| Type/creator codes | — | MIME types instead. |

### §3.7 Memory (ch. 7)

Application memory sizes don't apply (Linux gives each program what it asks
for), and **About This Computer** keeps showing honest memory use. The
**Memory** control panel has the rest, as Linux has it; see `docs/memory.md`.

| Feature | Status | Notes |
|---|---|---|
| Disk Cache | ✅ | Shown, not set: the system sizes it (Cached + Buffers − Shmem). |
| Virtual Memory on/off and size | ✅ | `/swapfile` on the startup disk, sizes from 1 GB to what leaves 2 GB free; applies at once, not at restart. Turning it off is refused while memory has no room for what it holds. Other swap (a partition) is listed and left alone. Plus a checkbox for the out-of-memory guard (earlyoom). |
| Compressed Memory | ✅ | Not in Mac OS 9 (RAM Doubler's idea): a zram swap device, zstd-compressed, by systemd-zram-generator. On/Off and use up to a quarter, half, three quarters or all of memory; off until turned on. |
| RAM Disk with Save on Shut Down | ✅ | A tmpfs at `/run/zacos9-ramdisk`, up to half of memory, on the desktop as "RAM Disk"; `zacos9-ramdisk.service` makes it at startup and saves it at shut down. |
| Application memory sizes (Get Info ▸ Memory) | — | Linux manages memory per program. |

### §3.8 Open and Save dialogs (ch. 8)

| Feature | Status | Notes |
|---|---|---|
| Open/Save through the ZacOS file-chooser portal | ✅ | `shell/finder/filechooserportal.cpp` |
| Keyboard navigation (type-select, arrows, ⌘-↑, ⌘-D desktop, ⌘-N new folder) | ◐ | `filechooser-locations` verifies type-to-select and arrow navigation in the Open dialog. The remaining shortcuts still need targeted verification against p. 127. |
| Shortcuts, **Favorites** and **Recent** pop-ups (Navigation Services) | ✅ | Favorite folders are offered as locations (0.1.54); Recent Folders lists the ten latest usable folders scoped to the active disk. Dropping a folder onto a dialog navigates there only if it belongs to the selected location. |
| Drag a folder onto the dialog to jump there | ✅ | Accepts a single local folder only when it is inside the chooser's selected disk/location. |
| General Controls ▸ Documents: default save folder | ✅ | Folder set by the application (the list of disks, as before), last folder used in the program, or Documents; the latter two open straight in the folder, with Other Locations… |

### §3.9 Moving data (ch. 9)

| Feature | Status | Notes |
|---|---|---|
| Cut/Copy/Paste, drag and drop between apps | ✅ | |
| **Clipping files**: drag text or pictures to the desktop or a folder | ✅ | `shell/files/clippings.cpp`. Text → "First words clipping.txt", a picture → "picture clipping.png", a link → an Internet location "Title.url" ([InternetShortcut]; opens in the browser). Ordinary files other programs open; they land where dropped, have their own icons and kinds, open in a viewer window, and dragged into a document carry their text or picture (an Internet location its address). |
| Show Clipboard window | ✅ | See §3.3. |
| Publish and Subscribe | — | |
| Exchanging files with Windows (File Translator, PC disks) | ✅ | |

### §3.10 Look and feel (ch. 3) and control panels (ch. 12)

| Control panel / feature | Status | Notes |
|---|---|---|
| **Appearance**: Themes, Color (accent, highlight), Desktop (patterns, pictures), Sound (sound sets) | ✅ | Wallpaper placement: Fit, Fill, Stretch, Center and Tile; each monitor is rendered to its own geometry. Options controls title-bar collapse and scroll-arrow placement. |
| Appearance ▸ **Fonts** (Large System Font, Small System Font, Views font and size, Smooth all fonts) | ✗ | P2 M — the bitmap fonts are fixed; offer the Views size and smoothing for apps (GTK/Qt settings). |
| Appearance ▸ **Options** (smart scrolling, double-click title to collapse) | ✅ | Options tab: "Double-click title bar to collapse windows" (`title-collapse`) and Smart Scrolling (see Scroll bars in §3.1). |
| **Zac Menu Options** | ✅ | Recent counts and the choice to show each recent category as a submenu or as category-prefixed items. |
| AppleTalk | — | |
| ColorSync | — | colord is out of scope for now. |
| **The Collar** panel (show/hide, hot key, font) | ✅ | The Collar panel. |
| **Date & Time**: date/time, time zone, network time, clock options | ✅ | Menu-bar date formats (month-day-year, day-month-year, ISO year-month-day) and optional hourly ZacOS sound are set in the Date & Time panel. |
| DialAssist, Modem, Remote Access | — | |
| **Nap Time**: sleep, display sleep | ✅ | Schedule: Start up / Shut down, every day, weekdays or weekends at a time (`/etc/zacos9/schedule` through `zacos9-accounts-helper schedule`): shut-down is a generated systemd timer, start-up the RTC wake alarm, set again at every shut-down by `zacos9-wakealarm.service`. |
| **Extensions Manager** (sets, item information) | ✅ | Login items, user services and curated extensions with item information and on/off; extension changes take effect after restart. The Set pop-up saves the current login-item/service choices as a named set (`~/.config/zacos9/Tricks Sets`), applies one, or deletes it. |
| File Translator | ✅ | |
| File Sharing | ✅ | |
| **Retriever** (folder pairs) | ✅ | `zacos9-retriever`: folder pairs (`~/.config/zacos9/retriever.conf`), Synchronize Now copies newer files each way with rsync (newest wins, nothing deleted), and says so. Not automatic yet. |
| **General Controls**: show desktop in background, Toy Box at startup, shut-down warning, insertion-point and menu blinking, Documents folder for Open/Save | ✅ | `zacos9-controlpanel general`. Show Desktop, Menu Blinking, Insertion Point Blinking and Documents done. Show Toy Box at system startup ✅; Shut Down Warning (fsck covers it) and Folder Protection (already dead in Mac OS 9) left out. |
| **Internet** (email, web, default apps, downloads folder) | ✅ | `zacos9-controlpanel internet`: default browser and mail program (GIO, `mimeapps.list`), downloads folder (`user-dirs.dirs`). The Mac's personal details, news and home-page settings have no system-wide place on Linux. |
| **Keyboard**: repeat rate/delay | ✅ | |
| Keyboard ▸ **layouts** and the keyboard menu (flag at the right of the menu bar, ⌘-Space to switch) | ✅ | Up to four ticked in the Keyboard panel (xkb's group limit), else Debian's `/etc/default/keyboard`; an abbreviation (US, US-DV) instead of a flag; ⌘-Option-Space option. `compositor/src/kblayout.c`, `shell/menubar/kbmenu.c`. |
| Keyboard ▸ **Function Keys** | ✅ | See §3.6. |
| **Keychain Access** | ✅ | `zacos9-keychain`: the keychains (Secret Service collections), Unlock/Lock, Make Default, and each one's items by name with Remove; The Collar's Keychain module opens it. Passwords aren't shown. |
| **Toy Box** | ✅ | See §3.2. |
| **Walkies** (snapshots of network, sound, time zone, printer, startup items) | ✅ | `zacos9-walkies`: places (`~/.config/zacos9/Walkies`) remembering the NetworkManager connection, sound output and volume, time zone, default printer and Extensions Manager set; Switch To restores them (nmcli, pactl, timedatectl, lpoptions, the set's login items and user services). |
| **Memory** (disk cache, virtual memory, Compressed Memory, RAM Disk) | ✅ | See §3.7. |
| **Monitors**: resolution, pixel size | ✅ | Several screens: Arrangement (drag the screens), the main display (menu bar and icons), each screen's resolution and pixel size, Mirror Displays, Menu Bar on Every Display (`compositor/src/prefs.c`); the desktop's pattern on the others (`secondarydesktop.cpp`); pop-up tabs on their own screen. Colour depth — . |
| **Mouse**: tracking, double-click | ✅ | Options: Mouse Tracks (`compositor/src/tracks.c`: six arrows behind the moving pointer, going 50 ms apart) and Thick I-Beam (a `text-thick` cursor zacos9-wm shows for programs that ask for "text" by cursor-shape-v1; Qt 6 programs ask that way and get it (checked with Scribble); ones that draw their own cursor, such as GTK 3, keep the thin one). |
| **Users** | ✅ | See 3.14. |
| **Numbers** / **Text** (number and sort formats, language) | ✅ | `zacos9-controlpanel numbers` / `text`: the system's UTF-8 locales by their own names, with a live sample, kept in `~/.config/locale.conf` (read by `zacos9-session` at login); a locale not yet made is made by `zacos9-accounts-helper locale`. |
| Media Settings (CD AutoPlay) | ✅ | Sniffer Preferences ▸ General ▸ Media: "Play audio CDs and video DVDs when inserted" (`media-autoplay`, on): the default player for the disc's content type opens it; programs on a disc never start. |
| **Fetch Updates** | ✅ | Scheduled checks to verify. |
| **Sound**: alert sounds, output volume, input source | ✅ | Alert sound, output selection, volume/mute, input-source selection, and left/right balance for stereo outputs. |
| **Speech** (voice, talking alerts) | ✅ | The Speech control panel (`zacos9-controlpanel speech`). See 3.16. |
| **Startup Disk** | ✅ | `zacos9-controlpanel startupdisk`: GRUB's entries (other versions behind Show every version), the one it starts with selected; choosing one sets it through `zacos9-startupdisk-helper` (polkit; GRUB_DEFAULT=saved the first time), Look for Other Systems turns os-prober on, Restart. "Old Dog starts up with:" picks which of Old Dog's disks with a System Folder the Mac starts from (`~/.config/zacos9/classic.conf`, read by `zacos9-classic`). |
| **TCP/IP** | ✅ | |
| **Trackpad** (tap to click, drag, drag lock) | ✅ | `zacos9-controlpanel trackpad`: tracking and double-click speed (shared with Mouse), Use Trackpad for Clicking / Dragging / Drag Lock, set on libinput touchpads by zacos9-wm (`input.c`). |
| **Web Sharing** (personal web page, on/off) | ✅ | Internet panel serves `~/Web Pages` on port 8080 only when enabled; requests are confined to that folder. |
| **Accessibility** (Sticky Keys, Slow Keys, Mouse Keys) and **Zoom** (zoom) | ✅ | `zacos9-controlpanel accessibility`, done in zacos9-wm (`compositor/src/access.c`). Sticky Keys: latch, lock (twice), Shift ×5 toggles, menu bar shows held keys (platinum-shell-v1 v13 `sticky_keys`). Slow Keys: acceptance delay (physical keyboards only). Mouse Keys: keypad moves/clicks, 0 holds, . lets go, ⌘-Shift-Clear on. Zoom: CloseView-style, 2–16× about the pointer, ⌘-Option-K, ⌘-Option-↑/↓. No colour inversion (wlroots' render pass can't invert). |

### §3.11 The Collar (The Collar, ch. 4 p. 68)

| Module | Status | Notes |
|---|---|---|
| Sound volume, Sound source | ✅ | Output and input selection, with stereo balance; tested by `sound-output`. |
| Monitor resolution, brightness | ✅ | |
| Bit depth, TV/Video Mirroring | — / ✅ | Monitors ▸ Mirror Displays (`mirror`): every screen at the same place. |
| Energy (sleep now), battery | ✅ | |
| File Sharing (on/off, who's connected) | ✅ | `shell/collar/filesharing.cpp`: shown once File Sharing is set up; on/off through the sharing helper (the servers that were on come back); visitors by address from `ss` (no root needed, so no user names). |
| Keychain | ✅ | |
| Walkies | ✅ | See above. |
| AppleTalk, Remote Access, AirPort | ◐ | Network module with Wi-Fi ✅; dial-up — (out of scope) |
| CD audio (play, tracks, eject) | ✅ | As a media module over MPRIS (`shell/collar/media.cpp`): what's playing, Play/Pause, Next/Previous Track, Show the player, a choice of player when several run. Eject belongs to the Sniffer. |
| Printer selector | ✅ | `shell/collar/printing.cpp`: shown once CUPS knows a printer; yours ticked, choosing another makes it yours; Open Print Queue, Open Printer Picker. |
| Web Sharing | ✅ | See 5.7. |
| Speakable Items | — | |
| Option-drag to move The Collar and its modules; ⌘-Ctrl-S hot key | ✅ | |

### §3.12 Automation (ch. 10)

| Feature | Status | Notes |
|---|---|---|
| AppleScript recording and Script Editor | — | No equivalent language; don't imitate. |
| **Folder Tricks** (run a script when items are added to a folder) | ✅ | Programs in System Folder ▸ Folder Tricks (`~/.config/zacos9/Folder Tricks`), attached from the contextual menu, run as `TRICK FOLDER ITEM...` in the folder whenever items arrive, once the folder has been quiet for a second; hidden files and partial downloads (.part, .crdownload…) count when renamed. Attachments in `folder-tricks.json`; the Sniffer watches (QFileSystemWatcher) while it runs. Only "items added" (not opening, closing or removing); a folder that doesn't exist when the Sniffer starts isn't watched until a trick is attached again. |
| **Scripts in the Zac menu** (Automated Tasks) | ✅ | Executable regular files in `~/.config/zacos9/ZacOS 9 Menu Items/Automated Tasks` appear in an alphabetical submenu and run when chosen; non-executable files are omitted. |
| Scriptable Sniffer (open, label, comments from scripts) | ✅ | `zacos9-finder-ctl open|reveal|label|comment|command …` (`--help`) over the Sniffer's socket. |

### §3.13 Search: Bloodhound (ch. 15)

| Feature | Status | Notes |
|---|---|---|
| Find by name, results as a Sniffer view | ✅ | |
| Criteria: dates, comments, size, kind, label, version, locked, empty/shared folder, invisible, custom icon | ✅ | `shell/finder/findcriteria.cpp`: where (all disks, Home, the front folder) and up to six rows that must all match: name (contains, starts/ends with, is, is not, doesn't contain), date modified/created (today, past week/month, before/after a date), size, kind, label, lock, folder empty/shared, custom icon (is/is not), comments (contains, is/is not, doesn't contain), invisible (is/is not; host dotfiles) and application package version (contains, is/is not, doesn't contain). Hidden entries are traversed only for a query with an invisible criterion. |
| **Search contents** with an index; schedule indexing; Index Selection | ✅ | `shell/finder/findindex.cpp`: "contents include" asks localsearch (tinysparql full-text query, kept to the chosen place; the other rows sift its answers); File ▸ Index Selection adds folders to its index. Scheduling is localsearch's own. |
| Find Similar Items | ✅ | The document's most distinctive words, any of them, ranked by the index. |
| Save a search; several searches at once | ✅ | Save named criteria in the Find dialog; Saved Searches opens each in its own Items Found window. |
| Internet channels (web search with plug-ins) | ✅ | The Sniffer's File ▸ Search Internet offers DuckDuckGo, Google and Bing and opens the URL-encoded query in the default browser. |

### §3.14 Users and login (ch. 17)

| Feature | Status | Notes |
|---|---|---|
| Login window with a user list and password | ✅ | `zacos9-login` (0.1.54); the chosen person's picture from the Users panel (accountsservice's icons folder), or a blank face with their initial. |
| **Log Out** (Special menu), automatic logout or lock after idle | ✅ | Log Out ✅ (0.1.54); lock after idle ✅ (Screen Saver) |
| **Users panel**: add/remove accounts, picture, can change password, can log in, can manage accounts | ✅ | `zacos9-controlpanel users` over accountsservice (D-Bus, its own polkit rules: your name and picture freely, the rest with an administrator's password through the session's polkit agent); passwords hashed in the panel by crypt(3). You can't delete, demote or lock yourself. |
| Account kinds: Normal, **Limited** (chosen apps only), **Puppy Mode** (the simplified desktop for children) | ✅ | Limited ✅: Users ▸ Open… ▸ "Use only chosen programs" and Programs…, kept as malcontent's app filter (an allowlist of program paths and Flatpak refs, in accountsservice, changed only by administrators); `lib/appfilter.c` reads it at login. The Sniffer's Applications folder and its launches (Open, Open With, aliases, Favorites, Toy Box) and the Zac menu's items and Recent Applications leave out or refuse other programs; ZacOS's own (`zacos9-*`) are always allowed. Not a security boundary, and said so. F-keys aren't filtered. Puppy Mode ◐: the Sniffer preference (Simple Finder): the menus and contextual menus keep New Folder, Open, Close Window, Put Away, Find, Edit, the views, Clean Up, Arrange, Empty Trash, Eject, Sleep, Restart, Shut Down, Log Out and Help (one list, `pl_puppy_allows` in `lib/settings.c`); other commands and their ⌘-keys beep. An administrator sets it for a person (Users ▸ Open… ▸ Puppy Mode; `/etc/zacos9/puppy/USER` through `zacos9-accounts-helper`), and then it's on whatever their own preference says. |
| Global options: welcome message, guest account, speak your name, alternate password | ✅ | Users ▸ Options…: Allow guests to log in (a "guest" account with no password, its home emptied and refilled from /etc/skel at each login by `zacos9-session`) and the login window's welcome message (`/etc/zacos9/welcome`), through `zacos9-accounts-helper` (polkit `org.zacos9.accounts`). Voice login — . |
| Shared Documents folder | ✅ | `/home/Shared`, made by the package (mode 1777: anyone adds, only an item's owner removes), shown on the startup disk as Shared Documents. |
| Fast user switching | — | Mac OS 9 didn't have it. |

### §3.15 Disks, printing and fonts (ch. 18–19)

| Feature | Status | Notes |
|---|---|---|
| Network volumes drawn as such (AppleShare's look) | ✅ | The `network-disk` icon: a drive on a network line. |
| Disks on the desktop, Put Away/eject, drag a disk to the Trash to eject | ✅ | Network volumes (0.1.54) and USB disks eject when dragged to the Trash. |
| Erase Disk with format choice | ✅ | Erase Disk: FAT32 for USB disks; Disk Setup erases with a choice of ext4, FAT32, exFAT or NTFS. |
| **Disk Vet** (verify/repair) | ✅ | Disk Setup's First Aid tab (`zacos9-disksetup`): the volumes UDisks2 can check, Verify (Filesystem.Check) and Repair (Filesystem.Repair) with polkit's password; an open volume is closed first (the programs using it, from `/proc`, are named and quit if the user agrees: `inuse.cpp`) and opened again after, the startup disk excepted; the drive's SMART health (ATA and NVMe: passing or failing, temperature, hours). **Mount automatically** (a checkbox per volume, on unless turned off; `~/.config/zacos9/disks.conf` by UUID, `shell/files/mountpolicy.h`): the Sniffer mounts such volumes at login and when connected, tries a failed one again 3 s later, then read-only through UDisks (a volume a crash left marked in use, as NTFS is), saying so; only then is it unreadable, which may offer Initialize. A volume open read-only shows a notice, and Repair closes it, repairs it (UDisks: `ntfsfix` for NTFS) and opens it normally. |
| **Disk Setup** (partition, initialize) | ✅ | `zacos9-disksetup` Partition tab: a drive's layout (a bar and a list), New/Delete/Erase/Resize partitions (ext4, FAT32, exFAT, NTFS; resizing ext4 and NTFS) and New Table (GPT/MBR), queued until Apply, which says what it will erase; all through UDisks2 and polkit. Never the live medium's drive, a partition in use, the running system's drive as a whole or its EFI System partition (`shell/disksetup/plan.cpp`). The installer's Select Disk has Prepare Disk… (it opens here) and lists ext4 partitions as places to install. `diskinit.cpp` still initializes new disks. |
| Disk Copy (mount images) | ✅ | Mac disk images, GPT/MBR images (zacos9-mac-mount), and CD/DVD images (`.iso`, ISO 9660): a read-only loop device through `udisksctl`, mounted on the desktop like any disk. |
| **Printer Picker** (pick a printer; AppleShare servers) | ✅ | `zacos9-printerpicker`, in the Zac menu: the printers CUPS knows and finds by itself (IPP Everywhere, `lpstat -e`; cups-browsed is kept off the system, its queues broke), with model, location and status; clicking one makes it yours (`lpoptions -d`, no password); Create Desktop Printer, Print Queue, Settings… (system-config-printer). Servers stay in the Neighbourhood. |
| **Desktop printer icons**: drag documents to print, queue window (pause, reorder, remove), default printer (bold outline) | ◐ | `NAME.printer` files on the desktop (`[Desktop Printer]` Queue=…) with a printer icon: documents dropped on one print there, opening one opens `zacos9-printqueue` (printer status, documents, Hold/Resume, Remove, Stop/Start Printing through cups-pk-helper and a polkit rule for administrators; drops print). Print Next (CUPS priority 100) moves a waiting job ahead; the default printer's desktop icon has Mac OS 9's bold outline (`lpstat -d`, read in the background every 10 s). |
| Print Queue / non-modal print errors | ✅ | Print errors (the Sniffer's and the queue window's) are alerts that don't stop you working. |
| Page Setup / Print dialogs | ✅ | Apps use their own (GTK/Qt). |
| Fonts folder (drop to install) | ✅ | Drop files into the backed `Fonts` folder; after successful copies the Sniffer runs `fc-cache -f` asynchronously, with a visible alert if the cache update fails. Like Mac OS 9's, it holds a few dozen fonts: the free ones that stand in for Mac OS 9's (Nimbus Sans for Helvetica, Nimbus Roman for Times, P052 for Palatino, C059 for New York, Z003 for Apple Chancery, Linux Libertine for Hoefler Text, DejaVu Sans for Geneva, DejaVu Sans Mono for Monaco, Liberation for the Internet Explorer fonts), once each, and the user's own; every other font is in its **Language Kits** folder. In a ZacOS session programs see only the Fonts folder's fonts and the user's own (`share/fontconfig/zacos9-fonts.conf`, set by `zacos9-session` as `FONTCONFIG_FILE`; the user's own accepted by file in `~/.config/zacos9/fonts-own.conf`, which the Sniffer keeps up to date). A font's icon is a suitcase with "A" on it in the font itself (at 32 px). |
| FontSync, ColorSync | — | |

### §3.16 Sound, movies and speech (ch. 20–21)

| Feature | Status | Notes |
|---|---|---|
| Alert sounds; record your own alert sound | ✅ | Record a WAVE file with Bark Recorder, then add it in Appearance ▸ Sound; personal recordings are stored under `~/.local/share/zacos9/sounds`. |
| **Bark Recorder** (record/play) | ✅ | `zacos9-barkrecorder`: records to WAVE with `pw-record` and plays with `pw-play`; stopping and closing are asynchronous. Missing audio tools or backend errors are reported. |
| QuickTime Player | ✅? | Any installed player opens movies; a Platinum player is not planned. |
| Startup/Shutdown sounds (put a sound in Startup Items) | ✅ | Optional `startup` and `shutdown` samples from the selected sound theme; theme import supports both. |
| **Text-to-speech**: voices, Talking Alerts, read selected text | ✅ | Scribble ▸ Speech ▸ Speak Selection uses `spd-say` with the Speech panel's voice (the base voices in your language) and rate (`speech-voice`, `speech-rate`); Talking Alerts (`talking-alerts`) speak an alert's message when it's left unanswered for 2.5 s; Try Voice. |
| Speech recognition (Speakable Items) | — | |

### §3.17 Help (Appendix A)

| Feature | Status | Notes |
|---|---|---|
| **Help Center** (browser-like, searchable HTML help) | ✅ | `zacos9-help` (`shell/help/`): Back, Forward, Help Center and a search field above the page; the Help Center lists every book in `share/zacos9/help` (a folder with an index.html); search ranks every page (title words count most) with the best sentence and a relevance bar. ZacOS Help: 14 pages (basics, the Sniffer, files, Find, Zac menu, programs, The Collar, Old Dog, control panels, accessibility, networks, shortcuts, troubleshooting). |
| **Hint Bubbles** (Show Hint Bubbles) | ✅ | Qt tooltips use the Platinum bubble style; the menu bar draws a bubble beside the item under the pointer in its own menus (Zac, Sniffer, Application, Help; programs' global menus have none), tail pointing into the item, on the menu's left when there's no room. Panels: every panelkit control has a `hint` (`hintAreas` for lists and the like), answered as the window's tooltip by `panelHintEvent`; all the ZacOS panels have their text. Help ▸ Show Hint Bubbles (`hint-bubbles`) turns them all on and off. Not measured. |
| ZacOS Help from the Sniffer | ✅ | Help ▸ Help Center and ZacOS Help (⌘-?) when the Sniffer is in front. |

### §3.18 Desk accessories and utilities (ch. 5, 13)

| Program | Status | Suggested approach |
|---|---|---|
| **Dog Ears** | ✅ | `zacos9-dogears`, in the Zac menu: coloured note windows saved as you type (`dog-ears.json`), New Note, Import/Export Text, Color menu; closing a note offers to save its text. |
| **Notepad** | ✅ | `zacos9-notepad`: one window with eight pages, previous/next page commands, and automatic per-page persistence. |
| **Stash** | ✅ | `zacos9-stash`, in the Zac menu: one clip per page (text or a picture) in `~/.local/share/zacos9/Stash`, leafed through with the scroll bar or arrow keys; Paste and drops (text, pictures, picture and text files) add a page after the one in view; Copy and dragging out take a copy; Cut and Clear remove a page; Undo reverses the last change. No sounds, movies or styled text. |
| **Calculator** | ✅ | `zacos9-calculator`, in the Zac menu; typed or clicked; ⌘C/⌘V. |
| **Key Tags** | ✅ | `zacos9-keytags`, in the Zac menu: the main keys of an extended keyboard drawn with what each types in the layout in use (read from xkb with zacos9-wm's model and options), following ⌘-Space and the keyboard menu; Layout menu for the others in use. Holding Shift or Alt Gr, or clicking Shift or Option in the picture, shows their characters; dead keys are outlined. Typing or clicking keys fills a line to copy from. Option in the picture means xkb's third level (Alt Gr): Linux reads the left Option key as Alt. No Font menu. |
| **Scribble** | ◐ | `zacos9-scribble`: plain text (UTF-8 or Latin-1; CR, CR LF or LF kept), New/Open/Save/Save As, Find/Find Again, Font and Size menus; Speech ▸ Speak Selection and Stop Speaking. Styled text remains. |
| **Pedigree** | ✅ | `zacos9-pedigree`, in the Zac menu and behind the Sniffer's System Information: tabs System Profile (system, hardware, battery, displays, network) and Devices and Volumes (disks and their volumes, USB, PCI, Bluetooth, sound), outlines opened by their triangles; File > Save Report, Edit > Copy, View > Refresh/Open All/Close All. Read from /sys, /proc, os-release, dpkg and pci.ids/usb.ids, no password: memory slots and serials only root can read are left out. |
| Plotter, Jigsaw Puzzle | ✅ | `zacos9-plotter`: y = f(x) with functions, implied multiplication and zoom (`plotter/expr.cpp`, tested). `zacos9-jigsaw`: a picture (Happy Zac, or Open Picture…) in tabbed pieces, three sizes; pieces snap home. |
| AppleCD Audio Player, DVD Player, Apple Video Player | — | Use regular media apps; The Collar's media module covers control. |
| Disk Copy, Disk Vet, Disk Setup | see §3.15 | |

### §3.19 System folder, startup and troubleshooting (ch. 11, App. B–C)

| Feature | Status | Notes |
|---|---|---|
| Startup Items | ✅ | XDG autostart in the Macintosh view |
| **Shutdown Items** | ✅ | Executable files in `~/.local/share/zacos9/Shutdown Items` run alphabetically, with a 30-second per-item limit, after apps close and before logout. |
| Extension parade at startup | ✅ | `boot/` |
| Shift at startup: safe mode | ✅ | Hold Shift while choosing Log In; the session starts without running Startup Items. Core ZacOS session services remain enabled. |
| "Your computer did not shut down properly" + disk check | — | systemd/fsck handle it. |
| Custom/clean install, Installer | ✅ | ZacOS installer. |

---

## 4. Out of scope (and why)

- **Application memory sizes** — Linux gives each program the memory it asks
  for. (Virtual memory, the disk cache, Compressed Memory and a RAM Disk are
  in Memory: §3.7.)
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

**Numbering, for whoever picks up work.** A work package is `N.M`
(milestone N, package M), and that number is how commits, `PROJECT_STATUS.md`
and other agents name it ("gap plan 4.3"). The inventory's sections are
cited as `§3.x` so they can't be mistaken for packages. Every open row in
the inventory (✗ or ◐) ends with **→ N.M**, the package that finishes it;
a row with no arrow is done or out of scope. Take a package whose box isn't
✅, check its inventory rows (search for `→ N.M`), and when it lands: mark
the package ✅ (◐ with what's left, if not all of it), update those rows'
status and notes, and drop or move their arrows. New work gets the next
free number in the right milestone; numbers are never reused or shifted.

### Milestone 1 — Daily-use parity (P1)

| # | Work package | Components | Effort | Tests |
|---|---|---|---|---|
| 1.1 ✅ | **⌘-Tab / ⇧⌘-Tab** app cycling, and Option-click desktop or Option-choose an app to hide the one you leave | `compositor/src/hotkey.c`, `shell/menubar/toplevels.c` | S | Headless: three clients, ⌘-Tab order and focus; hidden state after Option-switch |
| 1.2 ✅ | **Sniffer contextual menus** for icons, window backgrounds, the desktop, the Trash and disks; Control-click and right-click | `shell/finder/` (popup surface), `lib/menudraw` | M | Headless screenshot of each menu; each command reaches `Finder::command` |
| 1.3 ✅ | **Special ▸ Eject (⌘E)** and **Log Out (⇧⌘Q)**, with "quit your programs first" | `shell/menubar/menus.c`, `session/` | S | VM: Log Out returns to the login window |
| 1.4 ✅ | **Window tricks**: Option-click close/collapse for all of an app's windows, ⌘-drag a background window, Option-zoom fills the screen | `compositor/src/input.c`, `view.c`, `frame.c` | S | Headless compositor tests (`tests/compositor/`) |
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
| 3.7 ✅ | Appearance ▸ **Options** tab: double-click title bar to collapse, and **Smart Scrolling** (arrows together, proportional thumb) in ZacOS's own windows, Qt and GTK 3 programs | `shell/panels/appearance.cpp`, `lib/widgets.c`, Qt style, GTK 3 theme, compositor | S + L |
| 3.8 ✅ | Reset Column Positions (was already done with 2.17) | `shell/finder/folderwindow.cpp` | — |
| 3.9 ✅ | **Login window** shows the chosen person's picture | `shell/greeter/`, panelkit | S |
| 3.10 ✅ | Mouse panel: **Mouse Tracks** and **Thick I-Beam** | compositor `tracks.c`, cursor theme, `controlpanel.cpp` | S |

### Milestone 4 — Startup, session and system settings (P3)

This inventory's milestone numbers are distinct from the Classic integration
Phase 4 in `ROADMAP.md`. Completion below includes the latest session updates;
partial entries retain the remaining work described in the inventory above.

| # | Work package | Components | Effort |
|---|---|---|---|
| 4.1 ✅ | **Shutdown Items** and startup/shutdown sounds | `session/`, System Folder | S |
| 4.2 ✅ | **Shift held at login**: a session without startup items | `session/` | S |
| 4.3 ✅ | **Extensions Manager sets**: on/off sets for startup items and user services | `lib/extensions.c`, panel | M |
| 4.4 ✅ | **Nap Time** scheduled start-up and shut-down | `rtcwake` + systemd timer, polkit helper | M |
| 4.5 ✅ | **Date & Time** formats and hourly chimes | `controlpanel.cpp` | S + S |
| 4.6 ✅ | **Numbers / Text** panels (locale) | `localectl`, `locale.conf` | S |
| 4.7 ✅ | **Sound**: input source, speaker balance, record an alert sound (Appearance ▸ Sound ▸ Add Recording… ▸ Record…); input source in The Collar's Sound menu | panels, `shell/collar/` | S each |
| 4.8 ✅ | **CD/media autoplay** as a Sniffer setting | Sniffer | S |

### Milestone 5 — Accounts, privacy and sharing (P3)

| # | Work package | Components | Effort |
|---|---|---|---|
| 5.1 ✅ | **Limited accounts**: allowed programs enforced by the Sniffer and menu bar (not a security boundary; say so) | Users panel, Sniffer, menu bar | L |
| 5.2 ✅ | **Puppy Mode** (Simple Finder): the Sniffer preference, and set for a person by an administrator | Sniffer, Users panel, `system/accounts/` | L |
| 5.3 ✅ | **Guest account** and a welcome message | Users panel, login window | M + S |
| 5.4 ✅ | **Shared Documents** (`/home/Shared`, sticky) | `system/sharing/` | S |
| 5.5 ✅ | **Encrypt / Decrypt**: GPG passphrase-protected copies, the passphrase kept in the keychain if wanted | Sniffer | M |
| 5.6 ✅ | **Keychain Access** panel: list items, Lock/Unlock | `shell/panels/` | M |
| 5.7 ✅ | **Web Sharing**: a static server for `~/Web Pages` behind a switch | `system/sharing/`, panel | S |
| 5.8 ✅ | **Retriever** (folder pairs: two-way rsync, newest wins, with a warning) | new app | M |

### Milestone 6 — Help, automation and speech (P3)

| # | Work package | Components | Effort |
|---|---|---|---|
| 6.1 ✅ | **Hint Bubbles**: drawn by the menu bar over its menus, and by our panels from their tooltips | menu bar, panelkit | M |
| 6.2 ✅ | **Folder Tricks**: inotify watcher, scripts, "Attach a Folder Trick" in the contextual menu | Sniffer | M |
| 6.3 ✅ | **Automated Tasks** in the Zac menu | `Menu Items` | S |
| 6.4 ✅ | **`zacos9-finder-ctl`**: a documented CLI over the Sniffer's socket | Sniffer | S |
| 6.5 ✅ | **Text-to-speech**: Scribble reads selected text; voice and rate; Talking Alerts | speech-dispatcher / espeak-ng | M |
| 6.6 ✅ | **Search**: saved searches, several at once, File ▸ Search Internet, and Internet channels in the Find window's "Find items" pop-up | `findwindow.cpp` | S + M + S |

### Milestone 7 — Disks, fonts and printing (P3)

| # | Work package | Components | Effort |
|---|---|---|---|
| 7.1 ✅ | **Disk Vet**: fsck of unmounted volumes, SMART status | `zacos9-disk-helper` | M |
| 7.2 ✅ | **Fonts folder**: drop to install (copy, `fc-cache`) | Sniffer | S |
| 7.3 ✅ | **Print Window / Print Desktop** and Page Setup | Sniffer, `shell/print/` | M |
| 7.4 ✅ | Printing leftovers: non-modal print errors, job reordering, the default printer's bold outline | `shell/print/` | S |
| 7.5 ✅ | **Custom icons by pasting** in Get Info | Sniffer, gio metadata | M |
| 7.6 ✅ | **Disk Setup** partitioning | disk helper | L |
| 7.7 ✅ | Video mirroring on/off (after 9.9, several screens) | compositor | M |

### Milestone 8 — Desk accessories and extras (P3)

| # | Work package | Effort |
|---|---|---|
| 8.1 ✅ | **Notepad** | S |
| 8.2 ✅ | **Key Tags** (current layout, modifier states) | S |
| 8.3 ✅ | **Bark Recorder** (`pw-record`) | S |
| 8.4 ✅ | **Stash** (pages of pictures and text) | M |
| 8.5 ✅ | **Walkies** (snapshots of network, sound, time zone, printer, startup items) | L |
| 8.6 ✅ | Plotter, Jigsaw Puzzle | optional |
| 8.7 ✅ | **Memory**: virtual memory, the disk cache, Compressed Memory (zram), a RAM Disk, the out-of-memory guard | optional |

### Milestone 9 — Leftovers and checks from Milestones 1–2 (P2)

What earlier packages left open, and their "to verify" notes. Work these
alongside the others; most are small.

| # | Work package | Components | Effort |
|---|---|---|---|
| 9.1 ✅ | **Option-⌘-W** closes all of the program's windows (Option-click on the close box already does) | `compositor/src/input.c`, menu bar | S |
| 9.2 ✅ | **Contextual Menu Items folder**: `.desktop` file-manager actions in `~/.local/share/zacos9/contextual`, offered in the Sniffer's contextual menus (shares a format with Folder Tricks, 6.2) | Sniffer, menu bar | S |
| 9.3 ✅ | **Zac menu**: folders in Menu Items sprout submenus (verify), and Zac Menu Options' submenus on/off | `shell/menubar/launch.c`, `shell/panels/` | S |
| 9.4 ✅ | **Appearance ▸ Fonts**: the Views font size and font smoothing, for Qt and GTK programs (the bitmap system fonts stay fixed) | `appearance.cpp`, Qt style, GTK settings | M |
| 9.5 ✅ | **Find criteria**: comments, version, invisible, custom icon | `shell/finder/findcriteria.cpp` | S |
| 9.6 ✅ | **Printing through programs**: File ▸ Print, and Print in an icon's contextual menu, open other documents in their program with its print action | Sniffer, `shell/print/` | M |
| 9.7 ✅ | **Menu and Pack details**: a ⇧ glyph so Log Out shows ⇧⌘Q; dragging the Pack's edge sets its width | `lib/menudraw.c`, `shell/pack/` | S |
| 9.8 ✅ | **Scribble**: styled text, and Speech (after 6.5) | `shell/scribble/` | M |
| 9.9 ✅ | **Several screens**: arrangement in Monitors, the desktop and menu bar per screen, pop-up tabs on any screen; then video mirroring (7.7) | compositor, Monitors panel, Sniffer | L |
| 9.10 ✅ | **Checks** (the inventory's "to verify"): ⌘-→/⌘-← on disclosure triangles; Edit ▸ Copy on icons copies their names; local disks eject when dragged to the Trash; Erase Disk's formats; desktop picture placement; scheduled update checks; ISO/IMG images in Disk Copy. Confirm each, fix what fails, update its row | various | S each |

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

### Suggested next sessions

1. **Hardware checks** of what was built and only tested in VMs or headless: Disk Setup on a real disk, Nap Time's wake alarm, CD/DVD AutoPlay, recording an alert sound, several real screens.
