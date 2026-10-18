# Project Status

Updated: 2026-10-17

## Completed

| Commit | Feature |
|---|---|
| *(uncommitted)* | **Milestones 7 and 9 finished** (gap plan 9.4, 9.6–9.8, 9.10; 9.9 and 7.7, several screens and mirroring, were already built: the plan's rows are brought up to date). ⇧⌘Q drawn for Log Out (a ⇧ glyph in `lib/menudraw.c`); the Pack's names column set by dragging its right edge. Appearance ▸ Options ▸ Fonts: programs' text size and smoothing (gsettings for GTK, the Platinum Qt style). Scribble: a Style menu (Plain, Bold, Italic, Underline); styled documents saved as HTML. Office documents print through LibreOffice (`--pt`). Checks: USB disks eject when dragged to the Trash; `.iso` images mount read-only (`udisksctl loop-setup`); Erase Disk is FAT32-only (Disk Setup has the rest); wallpaper placement fine; Software Update checks daily (`zacos9-update --background` from a user timer; Check daily turns it off). Tests: `erase-disk-menu`, `scribble-speech` |
| *(uncommitted)* | **Disk Setup and printing leftovers** (gap plan 7.1, 7.4, 7.6). `zacos9-disksetup` (`shell/disksetup/`), all through UDisks2 and polkit: First Aid (verify, repair, drive health) and Partition (a drive's layout; New, Delete, Erase, Resize partitions and New Table, queued until Apply, which names what it will erase; the live medium's drive, partitions in use, the running system's drive as a whole and its EFI partition are refused: `plan.cpp`). The installer's Select Disk: Prepare Disk… opens it, and ext4 partitions are offered as places to install. Read against this machine's real disks; partitioning checked by hand in the dev-boot VM with a scratch disk (`scripts/dev-boot.sh --disk`), 2026-10-17: it works. Print queue: Print Next (`lp -q 100`); print errors no longer modal; the default printer's desktop icon has a bold outline. Tests: `diskvet-udisks` |
| *(uncommitted)* | **Milestone 4 finished** (gap plan 4.3, 4.4, 4.6–4.8). Tricks Manager's Set pop-up: save, apply and delete named on/off sets of login items and user services (`shell/finder/tricksets.cpp`). Nap Time's Schedule: start up and shut down daily, on weekdays or weekends at a time (`zacos9-accounts-helper schedule`: a systemd timer, and the RTC wake alarm set at every shut-down by `zacos9-wakealarm.service`). Numbers and Text control panels (`~/.config/locale.conf`, read at login; locales made by `zacos9-accounts-helper locale`). Appearance ▸ Sound ▸ Add Recording… records from the microphone (`pw-record`, 10 s at most). The Collar's Sound menu lists sound inputs. Audio CDs and video DVDs play when inserted (Sniffer Preferences ▸ Media). Tests: `tricks-sets`, `accounts-helper`. Not tried on hardware: the wake alarm, a real CD/DVD, recording |
| *(uncommitted)* | **Milestone 6 finished: `zacos9-finder-ctl`, Speech, Find's Internet channels** (gap plan 6.4–6.6). `zacos9-finder-ctl` (`shell/finderctl/`): open, reveal, label, comment and any menu command over the Sniffer's socket (new `reveal-path`, `label-path`, `comment-path` commands). The Speech control panel (`zacos9-controlpanel speech`): voice (base voices in your language, from `spd-say -L`), rate, Try Voice, Talking Alerts; Scribble and `pl_speak` use them, and alerts left unanswered for 2.5 s say their message. The Find window's "Find items" pop-up adds "on the Internet with DuckDuckGo/Google/Bing". Tests: `finder-ctl`, `settings` |
| *(uncommitted)* | **Keychain for Encrypt, Keychain Access, Retriever** (gap plan 5.5, 5.6, 5.8 — completes Milestone 5). Encrypt offers to keep the passphrase in the keychain (`secret-tool`, by the encrypted file's path) and Decrypt finds it there (`shell/finder/cryptofiles.cpp`). `zacos9-keychain` (`shell/panels/keychainaccess.cpp`, `shell/collar/keychain.cpp`): keychains with Unlock/Lock and Make Default, their items by name, Remove; The Collar opens it before Seahorse. `zacos9-retriever` (`shell/retriever/`): folder pairs kept alike by rsync both ways, newest wins, nothing deleted. New dependencies: `libsecret-tools`, `rsync`. Tests: `finder-crypto`, `collar-keychain`, `retriever-sync` |
| *(uncommitted)* | **Puppy Mode per person, Guest account, welcome message, Shared Documents** (gap plan 5.2–5.4; `accounts/zacos9-accounts-helper` + polkit `org.zacos9.accounts`, `shell/panels/users.cpp`, `lib/settings.c`, `shell/greeter/main.cpp`, `session/zacos9-session`, `shell/finder/vfs.cpp`, `debian/zacos9.postinst`). Users ▸ Open… ▸ Puppy Mode locks it on for a person (`/etc/zacos9/puppy/USER`). Users ▸ Options…: Allow guests to log in ("guest", no password, home emptied from /etc/skel at each login) and the welcome message the login window shows. `/home/Shared` (1777) made by the package, shown on the startup disk (registry version 13). Tests: `users-panel` (helper through a stand-in pkexec), `erase-disk-menu`. Not yet tried in the VM: guest login through greetd with an empty password |
| *(uncommitted)* | **Puppy Mode** (Mac OS 9's Simple Finder; gap plan 5.2, part; `lib/settings.c` `pl_puppy_allows`, `shell/menubar/menus.c`, `shell/finder/finder.cpp`, `folderwindow.cpp`, `viewoptionsdialog.cpp`). Sniffer Preferences ▸ General ▸ Use Puppy Mode (`puppy-mode`): the Sniffer's menus and contextual menus keep only the essential commands (one shared list), and the others and their ⌘-keys beep. Test: `erase-disk-menu` (menus, contextual menu, the list). Remaining: set and locked for a person by an administrator |
| *(uncommitted)* | **Limited accounts** (Mac OS 9's Multiple Users; gap plan 5.1; `lib/appfilter.c`, `shell/panels/users.cpp`, `shell/finder/appdb.cpp`, `main.cpp`, `shell/menubar/launch.c`). Users ▸ Open… ▸ "Use only chosen programs" and Programs… (a checklist of installed programs) set malcontent's app filter over accountsservice (an allowlist; not limited = an empty blocklist); administrators only, through polkit. At login the Sniffer and menu bar read it: the Applications folder, Zac menu and Recent Applications show only allowed programs, and launching another shows "You don't have permission to use …". ZacOS's own programs are always allowed; F-keys aren't filtered; not a security boundary (the panel and help say so). New dependency: `malcontent`. Tests: `appfilter`, `users-panel` (fake accountsservice with the AppFilter property). Not yet tried on a real system with malcontent installed |
| *(uncommitted)* | **Hint Bubbles in control panels** (gap plan 6.1, part; `shell/panels/panelkit.cpp`, `controlpanel.cpp`). panelkit controls carry a `hint`; `PanelHost` and `ControlPanel` answer tooltip events with the hint under the pointer (`panelHintEvent`), which the Platinum style draws as a bubble and swallows with Show Hint Bubbles off. Text for every ZacOS panel: the eleven in `zacos9-controlpanel`, and TCP/IP, Appearance (per tab), Function Keys, Users, File Sharing, Date & Time, Startup Disk, The Collar, Bluetooth and File Exchange. Completes gap plan 6.1. Test: `panel-hints` |
| *(uncommitted)* | **Hint Bubbles over the menu bar's menus** (gap plan 6.1, part; `shell/menubar/hints.c`, `main.c`). With Help ▸ Show Hint Bubbles on, the item under the pointer in the Zac, Sniffer, Application and Help menus gets a pale yellow bubble beside the menu, tail pointing into the item (left of the menu when there's no room); programs' global menus have none. Tests: `menu-hints` (text, wrapping, shape), `session-ui` (bubble shown, gone with the setting off). Remaining: panelkit controls in our panels |
| *(0.1.64)* | **Network Browser: not this computer** (`shell/network/discovery.cpp`, `netbrowser.cpp`, `network/afp/main.c`). Its own servers aren't listed (Bonjour on lo and the LAN, WS-Discovery, AppleTalk), and connecting to it by address, name or AppleTalk name from the list, Connect to Server or Recent Servers is refused with an alert. Test: `discovery`; checked live (the list without ChromaSystem Pro Server) and headless (the alert for `--connect afp 192.168.2.110`) |
| *(0.1.63)* | **AFP over AppleTalk** (`network/afp/asp.c`, `dsi.c`, `shell/network/discovery.cpp`; `docs/network.md`). Our own ASP/ATP/NBP client on the kernel's DDP sockets, for Macs that share only over AppleTalk (Mac OS 8.6 and earlier): `zacos9-afp appletalk-servers`, and SERVER "appletalk:NAME" (looked up by name each time) for info, volumes and mount. The Network Browser lists "NAME (AppleTalk)" unless the server is also on TCP/IP. FPWrite now goes by the server's last-written offset. Checked live against a Power Mac 7500: lookup, info, guest login, volumes, mount, size; then through the Network Browser from the installed package, the owner's Randnum login and its Macintosh HD on the desktop with files copied both ways. Test: `discovery`. Not done: the server going away under a mount, zones/routers |
| *(0.1.62)* | **File Sharing for Mac OS 8.1 and earlier** (`sharing/zacos9-sharing-helper`, `shell/panels/filesharing.cpp`). The Chooser finds the server over AppleTalk: `appletalk = yes` in afp.conf where atalkd is installed (now Recommended), atalkd started before netatalk. "Allow clear-text passwords" becomes "Allow Mac OS 8.1 and earlier to connect": Randnum (`uams_randnum.so`) instead of clear text, with a Short Password of up to 8 printable ASCII characters (`zacos9-sharing-helper macpassword`, written to /etc/netatalk/afppasswd directly so it never shows in `ps`), since those Macs can't send longer ones; `status` reports `macpassword set`. sharing.conf's key is now `oldmacs` (`cleartext` still read). PanelEdit gains `maxLength`. Checked live against a Power Mac 7500 by hand (afp.conf edits); the helper's file format checked against afppasswd's own in a user namespace; the panel seen headless (snapshot.sh) reading the short password as set. Not done: the panel's own Save path run end to end |
| *(0.1.62)* | **Network Browser: printers aren't Windows computers** (`shell/network/discovery.cpp`). The WS-Discovery Probe was empty, so WSD printers and scanners answered and showed as "ADDRESS (Windows)" (two Brother printers on a real LAN). It now probes for `pub:Computer` and drops replies without it (`wsDiscoveryIsComputer`). `mergeServers`: no Windows entry for a computer sharing over AFP; Windows computers known only by address take Bonjour's name for it, else reverse mDNS (`avahi-resolve -a`). Test: `discovery`; checked live on that LAN (printers gone, both AFP servers stay). Not checked: a real Windows computer or wsdd2 answering the typed probe |
| *(release validation)* | **Sound fixture concurrency**. Concurrent input polling and output commands exposed stale rewrites and truncated JSON in the fake sound server. Read-only queries no longer write state; fixture mutations are serialized and atomic, test-driven hot-plug state uses QSaveFile, and timeout probes finish before changing failure modes. Ten consecutive sound-output runs pass. This changes test infrastructure, not live audio behavior. |
| *(release integration)* | **Latest session updates integrated with main**. Preserves Smart Scrolling, drag-time Space delivery, locked-item Trash handling, application-menu icons and the AFP discovery fix alongside the new Finder, accessories, sound and session features. Trash Get Info and Finder Preferences share `trash-warn`; the new close-app-window request is appended in Platinum protocol v16, preserving existing request opcodes. Hint Bubbles switches off on its first click. Debian requires GnuPG for encryption and recommends speech-dispatcher for Scribble speech. Classic remains feasibility-only; no production guest color capture or app handoff is claimed. |
| `3634055` | **Network Browser AFP discovery timeout** (`shell/network/discovery.cpp`). AFP disappeared because the broad Avahi browse resolved unrelated services past the 1.5-second UI budget; AFP and SMB now have concurrent focused queries under the shared deadline. A delayed fake-Avahi test verifies late resolved records. |
| `c9449e9` | **Smart Scrolling** (Appearance ▸ Options; gap plan 3.7 — completes Milestone 3; `lib/widgets.c`, `shell/qtstyle/zacos9style.cpp`, `share/themes/ZacOS9/gtk-3.0/gtk.css`, `shell/panels/appearance.cpp`, `session/zacos9-session`). `smart-scrolling`: both arrows together at the end and a scroll box proportional to what shows; off by default (an arrow at each end, a fixed box). `pl_scrollbar` gains `together`/`thumb_len`; `sb_smart()`, `sb_thumb_span()`, `sb_track_start()` replace the fixed arithmetic in the Sniffer, Find results, Toy Box, Pedigree and panel lists; the Qt style takes the same geometry from page step and range. GTK 3: the theme now has Platinum arrow buttons and a fixed slider; Smart Scrolling adds a marked block to `~/.config/gtk-3.0/gtk.css` (programs started afterwards); the session sets `GTK_OVERLAY_SCROLLING=0` so GTK 3 programs show real scroll bars. GTK 4 has no arrows. Tests: `widgets`, `finder-desktop` (Qt style), `custom-themes` (gtk.css block) |
| `6f21fff` | **Application menu handle: the program's own icon, and dragging** (gap plan 3.5; `shell/menubar/appicon.c`). The desktop entry's icon from the icon theme (Adwaita), hicolor or pixmaps via gdk-pixbuf, else the generic one; drag left for the name, right for the icon. New dependencies: `libgdk-pixbuf-2.0-dev` (build), `librsvg2-common`. Test: `session-ui` |
| `1467fa8` | **Trash: locked items stay, unless Option-Empty Trash** (gap plan 3.3; `shell/finder/finder.cpp`, `shell/menubar/menus.c`). Locked items (can't be changed; a locked folder keeps what it holds) stay with their Put Away records, and Empty Trash says so even without the warning; Option-Empty Trash (`empty-trash-option`) unlocks and removes them without asking (uninstalls still ask). Test: `finder-desktop` |
| `7180009` | **Thick I-Beam checked in a Qt program** (gap plan 3.10). Qt 6 asks for the text cursor through cursor-shape-v1, so Scribble gets the thick I-beam; `mouse-tracks` compares the pointer's ink over its text both ways |
| `0cbff6b` | **Mouse Tracks and Thick I-Beam** (Mac OS 9 PowerBooks' Mouse panel; gap plan 3.10; `compositor/src/tracks.c`, `assets/cursors/platinum-cursors.pcur`, `shell/panels/controlpanel.cpp`). The Mouse panel gains an Options group. Mouse Tracks (`mouse-tracks`): zacos9-wm leaves the theme's arrow at the pointer's last six places, 10 px apart, each going 50 ms after the one before; none while dragging a window or locked. Thick I-Beam (`thick-ibeam`): a new `text-thick` cursor (two-pixel stem) shown when a program asks for "text" through cursor-shape-v1. Not done: programs that draw their own cursor image (GTK 3) keep the thin I-beam; not checked with a real program. Tests: `mouse-tracks` (headless trail, gone at rest, none when off), `cursor-theme` |
| `dd5afb5` | **Login window: the chosen person's picture** (gap plan 3.9; `shell/greeter/main.cpp`, panelkit `accountPicture`). 48 px at the window's top right, from accountsservice's icons folder (readable by anyone; `ZACOS9_ACCOUNT_ICONS` for tests), else a blank face with the initial; the Users panel draws pictures the same way now. Checked headless with and without a picture |
| `8fb55f9` | **Appearance ▸ Options** (gap plan 3.7; `shell/panels/appearance.cpp`, `compositor/src/input.c`, `prefs.c`). A seventh tab with "Double-click title bar to collapse windows" (`title-collapse`, not part of a theme); zacos9-wm collapsed on every title double-click before. Panel 60 px wider; `zacos9-appearance options`. Not done: Smart Scrolling (arrows together, proportional thumb) — the scroll bars are Mac OS 9's without it; doing it touches every scroll bar, the Qt style and the GTK theme (L). Test: `window-tricks` |
| `15061f1` | **Open and Save dialogs: Recent, dropped folders** (Navigation Services; gap plan 3.6; `shell/finder/filechooserportal.cpp`). A Recent button on the location list and in the file dialog: recently-used.xbel's documents and folders, newest first, as many as Zac Menu Options' recent documents; a document opens its folder with it selected. A folder (or file) dropped on the dialog opens there. Both reopen the dialog in the location that holds the item, through the startup disk's links. Test: `filechooser-requests` |
| `53a790b` | **The Application menu's ribbed handle** (gap plan 3.5; `lib/menudraw.c`, `shell/menubar/`). Three ridges before the Application menu's title; a click switches between the program's name and an icon (`app-menu-name`). The icon is the generic application icon: the program's own needs icon lookup and image loading in the menu bar. A click, not Mac OS 9's drag. Test: `session-ui` (which now writes desktop.conf by rename, as the panels do) |
| `c3e2b81` | **Option-double-click** closes the window an icon was in (gap plan 3.5; `shell/finder/folderwindow.cpp`). Test: `finder-selection` |
| `b74a469` | **Option-choosing a program in the Application menu hides the one you leave** (gap plan 3.4; `shell/menubar/main.c`, `menus.c`). The menu bar reads Option from the keyboard it has while a menu is open (Alt's bit from the keymap; cleared when the keyboard leaves). Test: `session-ui` |
| `ef156d1` | **Trash: Warn before emptying** (gap plan 3.3; `shell/finder/infowindow.cpp`, `finder.cpp`, `shell/menubar/menus.c`). The Trash's Get Info: Kind "trash", Where "Desktop", no Sharing, and the checkbox (`trash-warn`). Off: Empty Trash has no ellipsis and doesn't ask, except before uninstalling queued applications. Not done: Option to force locked items (a read-only folder in the Trash makes Empty Trash say it couldn't empty completely). Tests: `finder-desktop`, `erase-disk-menu` |
| `d4a50f9` | **Space opens a spring-loaded folder at once** (gap plan 3.2; `protocols/platinum-shell-v1.xml` 15, `compositor/src/input.c`, `platinum_shell.c`, `shell/finder/`). wlroots' drag grab keeps keys from programs, so zacos9-wm takes Space during a drag and sends `drag_spring` to the program under it; the Sniffer opens the waiting folder without the delay. Test: `spring-space` (headless) |
| `8fc6455` | **⌘-drag scrolls a Sniffer window** with the grabber hand (Missing Manual p. 14; gap plan 3.1; `shell/finder/folderwindow.cpp`). Clients see ⌘ and Control both as Ctrl, so a Ctrl-press in a window that can scroll waits: moved, it scrolls with a closed hand; released still, it's the contextual menu (shown on release there). Test: `finder-selection` |
| *(in progress)* | **Phase 4 Classic integration feasibility** (`docs/classic-seamless.md`, `emulation/seamless/`). The original guest probe/verifier exercise bounded independent monochrome QuickDraw capture; host tooling now strictly validates future RGB PPM references/candidates. This is not guest color evidence or a production guest protocol/app-open handoff: color/GWorld capture, general drawing coverage, lifecycle and input gates remain open. |
| `c417474` | **Custom icons by pasting** (gap plan 3.2; `shell/finder/infowindow.cpp`, `items.cpp`). Click an item's icon in Get Info, paste a Clipboard picture, copy the current icon, or press Delete to restore it. A validated, 32x32 PNG is stored in a bounded user xattr and Finder views refresh after changes; unsupported filesystems report failure. Tests: `finder-vfs` (round-trip, transparent scaling, invalid/oversized input, item rendering and clear). |
| `c417474` | **Spring-loaded Space shortcut** (gap plan 3.2; `shell/finder/finder.cpp`). During a drag, Space opens the hovered spring-loaded folder immediately; it closes with the other folders opened for that drag. Test: `finder-selection`. |
| `c417474` | **Find custom-icon criterion** (gap plan 3.13; `shell/finder/findcriteria.cpp`). Find now filters items with/without a valid custom icon using “custom icon is/is not”; tested against persisted custom icon metadata in `finder-find`. |
| `c417474` | **Find comments criterion** (gap plan 3.13; `shell/finder/findcriteria.cpp`). Find searches `user.xdg.comment` with contains/is/is not/doesn't contain comparisons, bounds attribute reads, and includes regression coverage in `finder-find`. |
| `c417474` | **Find invisible criterion** (gap plan 3.13; `shell/finder/findcriteria.cpp`). Find can include host-hidden dotfiles when “invisible is/is not” is selected; ordinary searches retain their prior hidden-file behavior. Covered by `finder-find`. |
| `c417474` | **Find application version criterion** (gap plan 3.13; `shell/finder/findcriteria.cpp`). Find matches the owning package version of registered applications, without treating documents or apps lacking a package version as matches. `finder-find` uses a private desktop entry and fake package database. |
| `c417474` | **Saved Find searches** (gap plan 3.13; `shell/finder/findcriteria.cpp`, `findwindow.cpp`). Named criteria persist atomically in `~/.config/zacos9/find-searches.json`; Saved Searches runs each saved query in its own Items Found window. `finder-find` covers round-trip, sorted listing and multiple concurrent results. |
| `c417474` | **Option-Command-W closes every Finder window** (gap plan 3.1). Closes all Finder top-level windows, including hidden and auxiliary windows, while leaving the desktop open. `finder-selection` covers multiple folder windows and a hidden auxiliary window. |
| `c417474` | **List-view folder disclosure shortcuts** (gap plan 3.2). Command-Right expands and Command-Left collapses a selected folder inline; `finder-selection` covers both keys. |
| `c417474` | **Option-select to hide the previous app** (gap plan 3.6). Choosing an app from the Application menu while holding Option hides the previously active app before activating the selection; `menu-app-switch` covers the active-app selection policy. |
| `c417474` | **Option-double-click an application to close its front window** (gap plan 3.6). Added `platinum-shell-v1` request 15; the compositor closes one mapped window without quitting the app. `window-tricks` verifies it closes only the front one from a two-window application. |
| `c417474` | **Command-drag to scroll Finder windows** (gap plan 3.1). Dragging the contents pans vertically or horizontally without selecting or moving items; Command-click still opens the contextual menu on release if no drag begins. `finder-selection` covers both axes and release cleanup. |
| `c417474` | **Sound input and speaker balance** (gap plan 3.10). Sound panel lists selectable `pactl` input sources (excluding monitor sources) and adds a left/right balance slider for stereo outputs; per-channel volume updates preserve balance. The fixture covers discovery, source switching, balance endpoints and volume. Test: `sound-output`. |
| `c417474` | **Startup and shutdown sounds** (gap plan 3). The selected sound theme may provide `startup` and `shutdown` samples; ZacOS plays them when the desktop starts and after applications close for logout. Custom sound theme import supports both optional events. |
| `c417474` | **Web Sharing** (gap plan 3.10; Internet control panel, `session/zacos9-web-sharing`). A checkbox serves `~/Web Pages` on port 8080 only while enabled; the helper manages the user session's HTTP server and refuses paths resolving outside that folder. Test: `web-sharing` (start/status/stop, HTTP content, symlink escape). |
| `c417474` | **Date formats and hourly menu-bar chime** (Date & Time; gap plan 3.10). The date shown when clicking the clock can be month-day-year, day-month-year or ISO order; an optional ZacOS chirp plays once at each hour boundary. Both settings persist in `desktop.conf`. |
| `c417474` | **Fonts folder install** (gap plan 3.15; `shell/finder/fileops.cpp`). Dropping files in the user Fonts folder refreshes fontconfig asynchronously after successful copies and reports cache failures. |
| `c417474` | **Search Internet** (gap plan 3; Sniffer File menu). Choose DuckDuckGo, Google or Bing, enter words, and open the encoded query in the default browser. |
| `c417474` | **Shutdown Items** (gap plan 3.19; `session/zacos9-shutdown-items`, `shell/menubar/logout.c`). On logout, after applications close, executable files in `~/.local/share/zacos9/Shutdown Items` run alphabetically with a 30-second timeout each; then the login session ends. Test: `shutdown-items` |
| `c417474` | **Notepad** (gap plan 3.18; `shell/notepad/`, Zac menu). Eight pages of notes persist automatically in `~/.config/zacos9/notepad.ini`; previous/next page commands and keyboard editing. Test: `notepad` |
| `c417474` | **Stash** (the Scrapbook; gap plan 3.18; `shell/stash/`, Zac menu). One clip per page, text or a picture, kept in order in `~/.local/share/zacos9/Stash` (a file per clip and an index); scroll bar and arrow keys leaf through; the strip below shows "3 of 12" and the kind, size and dimensions or characters. Paste and drops (text, pictures, picture and text files from the Sniffer) add a page after the one in view; Copy and dragging the page out give a copy; Cut/Clear remove it; Undo reverses the last change. zacos9-wm keeps its own window size. Not done: sounds, movies, styled text. Test: `stash` |
| `c417474` | **Key Tags** (Key Caps; gap plan 3.18, Milestone 3 package 3.1; `shell/keytags/`, Zac menu). The main keys of an extended keyboard drawn with each key's character in the layout in use, read from xkb with zacos9-wm's model and options (built directly, so an unknown layout fails instead of showing the default); follows ⌘-Space through `platinum-shell-v1` keyboard_layouts (`platinumKeyboardLayout()` / `platinumOnKeyboardLayout()` in `shell/finder/platinumshell.cpp`); Layout menu for the other layouts in use. Shift and Alt Gr held, or Shift/Option clicked down in the picture, show their levels; dead keys outlined; typing or clicking fills a line to copy. Not done: Font menu; checked offscreen, not in a live session. Test: `key-tags` (us and de levels, dead keys, held and clicked modifiers, typing by clicking) |
| `c417474` | **Print in the icon contextual menu** (gap plan 3.4, Milestone 3 package 3.2; `shell/menubar/menus.c`, `shell/finder/finder.cpp`). Print follows Open, as in the File menu, and appears only when the selection has a document File ▸ Print would print (not only folders, printers or the Macintosh view's items): the Sniffer's state line gains `documents=`. The rest of package 3.2 (Add To Favorites on icons, Sort List and View Options on windows) was already in place; the gap plan rows are corrected. Tests: `erase-disk-menu` (menu items), `finder-desktop` (state for a folder and a document) |
| `c417474` | **Folder Tricks and Contextual Menu Items** (Folder Actions and contextual-menu plug-ins; gap plan 3.4/3.12, Milestone 3 package 3.3; `shell/finder/foldertricks.cpp`, `contextitems.cpp`, `finder.cpp`, `shell/menubar/menus.c`, `finderlink.c`). Programs in System Folder ▸ Folder Tricks attach to a folder from its icon's, window's or the desktop's contextual menu (Attach a Folder Trick ▸, with Open Folder Tricks Folder; Remove a Folder Trick ▸); the Sniffer watches attached folders and, once one has been quiet for a second, runs each trick detached as `TRICK FOLDER ITEM...` in the folder; hidden files and partial downloads count once renamed; attachments in `~/.config/zacos9/folder-tricks.json`. Desktop entries in System Folder ▸ Contextual Menu Items appear at the bottom of contextual menus when every item clicked fits their `MimeType=` (folders and window backgrounds as `inode/directory`, `image/*` families, `all/all`, `all/allfiles`), launched through GIO. The Sniffer sends `tricks` and `commands` lines before `context`. Not done: other Folder Action triggers (opening, closing, removing items), desktop-entry `Actions=` as separate commands; not checked in a live session. Tests: `finder-folder-tricks` (attach/remember/remove, a run for arrivals together, hidden and partial files, nothing for removals, MIME matching, launching), `finder-desktop` (the folder's menu lines, attach and remove through commands), `erase-disk-menu` (the menus) |
| `c417474` | **Page Setup, Print Window and Print Desktop** (Sniffer File menu; gap plan 3.3, Milestone 3 package 3.4; `shell/finder/printwindow.cpp`, `FolderWindow::printImage`, `Desktop::printImage`, `Finder::printWindow`). Page Setup… is a Platinum dialog (paper, orientation, scale) kept in `desktop.conf` (`page-paper`, `page-orientation`, `page-scale`). Print Window… draws the front window's whole contents with its own painters, tile by tile (every icon or row, a list with its column headings), on white with nothing selected; Print Desktop… the desktop's icons, cropped. QPdfWriter lays it out (name and date above, "Page N of M" below; shrunk to the page width; as many pages as it takes down) and `lp` prints it on your printer, named for the window; the File menu's Print shares its "no printer chosen" alert. Not done: a Print dialog (copies, page range). Checked offscreen (the dialog and a page rendered); not on a real printer. Tests: `finder-print-window` (Page Setup kept; page counts across, down, scaled, landscape; failures), `finder-desktop` (Print Desktop and Print Window reach stand-in `lp` as named PDFs), `erase-disk-menu` (the File menu items) |
| `c417474` | **Bark Recorder** (gap plan 3.16; `shell/barkrecorder/`). Records WAVE audio with `pw-record` and plays it with `pw-play`; both processes run asynchronously, stop cleanly, and report failures. Test: `bark-recorder` with fake PipeWire tools. |
| `c417474` | **Shift-at-login safe mode** (gap plan 3.19; login greeter and `session/zacos9-session`). Holding Shift while choosing Log In marks the session to skip Startup Items; core session services still start. Tests: `greetd`, `safe-mode-session`. |
| `c417474` | **Trash protection and confirmation** (gap plan 3.2; Finder Preferences > General). Confirmation is on by default and configurable; the menu ellipsis disappears when it is off. Locked files/folders are preserved unless Option is held while choosing Empty Trash. `finder-selection`, `test-desktop`, and `test-erase-menu` cover the setting and override. |
| `c417474` | **Recent Folders in Open/Save** (gap plan 3.8; `shell/finder/filechooserportal.cpp`). Remembers the ten most recently used folders, filters them to the active location, and supports dropping a folder onto the dialog to navigate there; tests cover ordering, deduplication, limits, and path boundaries. |
| `c417474` | **Finder selection and Open dialog keyboard navigation** (gap plan 3.2/3.8). Finder type-select, alphabetical Tab/Shift-Tab, and arrow selection have focused coverage, as do the info-strip contents and Open-dialog type-to-select/arrow navigation. Tests: `finder-selection`, `filechooser-locations`. |
| `c417474` | **Password-protected file encryption** (gap plan 3.2/3.3; `shell/finder/cryptofiles.cpp`). File ▸ Encrypt… and Decrypt… use GPG's AES-256 symmetric encryption asynchronously, confirm new passphrases, preserve originals, save through owner-only atomic output, and report failures. GPG is included in the live image. Keychain integration is not included. Test: `finder-crypto` (round-trip, output permissions, wrong-passphrase rejection). |
| `c417474` | **Print opens documents in their associated apps** (gap plan 3.3; `shell/finder/finder.cpp`). Types not directly printable by CUPS now open in their registered applications; the Sniffer explains that Print must be chosen there because desktop applications have no common print-request API. |
| `c417474` | **Window-edge movement verification** (gap plan 3.1; `compositor/tests/test_window_tricks.py`). `window-tricks` verifies Command-dragging an inactive window by its title or edge moves it without raising it. |
| `c417474` | **Tile wallpaper placement** (gap plan 3.10; Appearance panel). Adds repeat-at-natural-size desktop wallpaper alongside Fit, Fill, Stretch, and Center; pixel tests cover tile rendering. |
| `c417474` | **Appearance Options** (gap plan 3.10). Double-click collapse remains on by default and can be disabled; scroll-bar arrows can be grouped at one end or left at opposite ends. The compositor and shared widget drawing honor the same persisted preferences. |
| `c417474` | **Recorded alert sounds** (gap plan 3.16; Appearance > Sound). WAVE recordings made with Bark Recorder can be added as personal alert sounds; imports are bounded, signature-checked and saved atomically. Tests cover bad/duplicate file rejection and playback from the personal sound folder. |
| `c417474` | **Scribble Speak Selection** (gap plan 3.16/3.18). Speech > Speak Selection sends the selected text to `spd-say` asynchronously; Stop Speaking interrupts it, and missing tools/non-zero exits are reported. `scribble-speech` verifies the exact selected text reaches the speech process. |
| `c417474` | **Zac Menu Options submenu preference** (gap plan 3.10). Recent Applications, Documents and Servers can be shown as submenus (default) or as category-prefixed menu items; the choice and existing per-category counts are read live by the menu bar. Test: `menu-recent`. |
| `c417474` | **Finder Edit > Copy** (gap plan 3.3). Command-C copies the selected visible item names as newline-separated text, without changing file-move or paste behavior. Covered by `finder-selection`. |
| `c417474` | **Verified Put Away for trashed files** (gap plan 3.2). `finder-selection` moves a fixture to the Trash, writes its origin record, and checks that Put Away restores the original and removes both trash entries. |
| `c417474` | **Hierarchical ZacOS 9 Menu Items** (gap plan 3.5). Top-level and nested `.desktop` items are sorted into the Zac menu; real directories with entries become submenus (maximum nesting depth four), and symlink aliases to files/folders open in the Sniffer. `menu-launch-feedback` covers nested launchers, file/folder aliases, empty-folder filtering, and the four-level recursion boundary. |
| `c417474` | **Automated Tasks** (gap plan 3.12; `shell/menubar/launch.c`). Executable regular files in `~/.config/zacos9/ZacOS 9 Menu Items/Automated Tasks` appear in an alphabetical Zac menu submenu; other files are ignored and command paths are shell-quoted. Test: `menu-launch-feedback` (menu contents and path quoting) |
| `c417474` | **Hint Bubbles (partial)** (gap plan 3; `shell/qtstyle/`, Sniffer Help menu). Qt tooltips now use a pale yellow Platinum bubble with a dark frame; Help ▸ Show Hint Bubbles toggles them globally for Qt apps using the ZacOS style and persists in `desktop.conf`. ZacOS Help updated. Menu-bar and non-Qt balloon help remain. Test: `qtstyle-tooltips` |
| `ce53674` | **LibreOffice's menus in the menu bar** (`appstore/zacos9-appstore-helper`, `assets/store/catalog.json`; see `docs/app-integration.md`). Debian's `libreoffice` has no toolkit backend unless a desktop task adds one, so it ran on `gen` (plain X11 via Xwayland): its own menu bar in the window, menus that didn't open, nothing in the global menu. Installing any `libreoffice*` package through the Software window now adds `libreoffice-gtk3`, which exports LibreOffice's menus itself over the existing GTK 3 Wayland path; the catalog entry lists both, so an install without it shows as not installed and Install repairs it. Checked headless 4 runs in a row with the session's `zacos9-appmenu` module: Writer's File … Help in the menu bar, File opening with its items and shortcuts (one earlier run never mapped a window, not reproduced). No new test: the helper's install path needs root and apt |
| `6d58327` | **Pop-up windows** (Missing Manual p. 27; gap plan 2.3; `compositor/src/decor.c`, `view.c`, `input.c`, `platinum_shell.c`, `shell/finder/folderwindow.cpp`). View ▸ as Window / as Pop-up Window: a Sniffer window becomes a tab along the bottom of the screen, a new `zacos9-wm` frame style (`platinum-shell-v1` 14: style `popup`, `set_popup_tab`, events `popup_tab` and `popup_detach`, added after `sticky_keys` so older opcodes stay put) in its own layer above ordinary windows. A click on the tab opens it, the body standing on the screen's bottom edge with the tab on top; a click on the tab again, on the desktop, or another window coming forward closes it (one open at a time); the tab drags along the bottom, and dragged 40 px up the window turns into an ordinary one carried on by its title bar; a drag resting on the tab springs it open for the drop and it closes after; its top corners resize it. Closed tabs always draw as active. The Sniffer keeps `popup` and `tab-x` per folder (its window position and size stay for as Window), reopens it as a pop-up, and the menu bar checks the View items from the state line (`popup=`, `popups=`). Tab drawn to match the title bar, not measured against Mac OS 9. ZacOS Help's Sniffer page explains them. Tests: `decor` (pop-up hit areas, painting, closed height), `popup-windows` (headless: open, close by clicking elsewhere, tab click, moving and remembering the tab, reopening as a pop-up, spring-open drop, detaching). Not done: several screens (a tab stays on the screen it was made on), keyboard focus to a pop-up without opening it |
| `80741d1` | **Trash fixes** (`shell/finder/desktop.cpp`, `finder.cpp`, `appdb.cpp`). Dragging anything onto the Desktop's Trash icon did nothing since 0.1.54: the drop handler kept every drop there for ejecting network volumes; only a drag of network volumes is kept now. Empty Trash no longer stops at a queued application it won't uninstall (now a system utility, or its install source changed): it goes back to Applications with a note and the rest empties. Application origins are never put on a launcher wrapper (`Exec=/usr/bin/env …` had queued Sunshine as `dpkg:coreutils`; apt refused to remove it), and a timed-out `dpkg -S` is no longer cached as "no package". Test: `finder-desktop` (the Trash drop now goes through `Desktop::dropEvent`, which the old test skipped; an app that can't be uninstalled is put back and the Trash empties) |
| `44effc6` | **Users control panel** (Mac OS 9's Multiple Users, Missing Manual ch. 17; gap plan 2.14 — the last of Milestone 2; `shell/panels/users.cpp`, `zacos9-controlpanel users`, in Control Panels). The people who use this computer, by name with their kind (Administrator / Standard) and "(you)"; the chosen one's picture, short name, home folder. New User… (name, a short name made from it, password twice, administrator), Open… (name, picture with Choose…, a new password or blank to keep it, Can administer this computer, Can log in), Delete… (Keep Home Folder / Delete Everything). Through accountsservice on the system bus, asynchronously with interactive authorization allowed, so its own polkit rules decide: your name and picture freely, the rest with an administrator's password through the session's polkit agent (lxqt-policykit); a refusal changes nothing and says why. Passwords go hashed by crypt(3) with the system's preferred method. You can't delete, demote or lock yourself. System accounts aren't listed. The list is read again after each change and whenever the window comes forward (no change signals: no moc in the panels). New dependencies: `accountsservice`, `libcrypt-dev` to build. ZacOS Help's Control Panels page describes it. Checked headless against a stand-in. Not checked on a real system (accountsservice isn't installed here; a real account change wants a VM). Not done: Limited and Panels accounts, the login window showing pictures. Test: `users-panel` (a stand-in accountsservice on a private bus: names, hashing, the list, New User, Open, yourself protected, a refused delete, Delete Everything) |
| `01590a2` | **Aliases no longer jump to unrelated files after an upgrade** (`shell/finder/alias.cpp`). A Debian upgrade (VS Code, Firefox ESR, ChatGPT) replaced their desktop entries with new files, and the MIME database regenerated seconds later reused the freed inode numbers; the Sniffer's search for a moved original matched the number alone and silently repointed three desktop aliases at `/usr/share/mime/magic` and `globs2`. Records now keep the original's birth time (statx) and a match needs it too; old records without one only match an item that kept its name. A file replaced at its own path on the same filesystem (a save, an upgrade) is accepted as the original, as Mac OS 9 fell back to the path; a folder there is still asked about, as before. Test: `alias` (a reused inode number with another birth time, an old record, a replaced file accepted; tmpfs's coarse clock noted) |
| `5c542df` | **Printing: Printer Picker, desktop printers, print queues, File ▸ Print, The Collar's printer** (Missing Manual ch. 19; gap plan 2.8; `shell/print/`, `shell/collar/printing.cpp`). CUPS through its own programs in the C locale (no libcups headers here): `lpstat -e/-d`, `lpoptions -p/-d`, `lp`, `cancel`, `ipptool` with our own Get-Jobs request (as the user, so CUPS shows job names). **Printer Picker** (`zacos9-printerpicker`, Zac menu): printers known and found, model/location/status; clicking one makes it yours (`lpoptions -d`, no password); Create Desktop Printer; Print Queue; Settings… (system-config-printer). **Desktop printers**: `NAME.printer` files (`[Desktop Printer]` Queue=…) with a new printer icon and kind, the extension hidden; documents dropped on one print there; opening one opens **`zacos9-printqueue`**: printer status, documents waiting, Hold/Resume, Remove, Stop/Start Printing (cups-pk-helper on the system bus; `share/polkit/50-zacos9-printing.rules` lets local administrators without a password, as the desktop has no polkit agent), drops print; it looks again every 2 s. **Sniffer**: File ▸ Print (⌘-P) sends PDFs, PostScript, text and pictures to your printer; others get an alert to print from their own program; no printer chosen offers Printer Picker. **The Collar**: a Printer module once CUPS knows a printer, yours ticked, choosing another makes it yours, Open Print Queue, Open Printer Picker. ZacOS Help gains a Printing page. Checked headless against this machine's CUPS (Brother MFC-9130CW): Printer Picker, Create Desktop Printer, the icon, its queue, a held job shown then removed from the window; nothing printed. Not done: reordering jobs, the default printer's bold outline, printing other documents through their programs, Print Window/Desktop. Tests: `printing` (parsers, desktop printer files, Printer Picker and a queue against stand-in CUPS programs), `printing-authorization` (the polkit rule), `collar-media-sharing` (the printer module); the Collar tests now see no printers from the computer running them |
| `bd79b59` | **Help Center and ZacOS Help** (Mac Help; gap plan 2.15; `shell/help/`, `zacos9-help`, `share/zacos9/help/zacos9/`). A Platinum toolbar (Back, Forward, Help Center, search field and Search, Return searches) above a QTextBrowser. The Help Center lists the books in the help folder (a folder per book with an index.html; title and `<meta name="description">`), ZacOS Help first; search ranks every page of every book by the words (all of them if any page has all, else the most; title words count most; "print" finds "printing"), with the sentence that has most of them and a five-block relevance bar. Made pages use a `zacos9-help:` scheme, so Back/Forward cover them too; web links open in the browser. `zacos9-help [BOOK|PAGE]`, `--search WORDS`. ZacOS Help: 14 pages written from what ships (basics and keys, the Sniffer, files, Find, the Zac menu, programs, The Collar, Old Dog, control panels, accessibility, networks, keyboard shortcuts, troubleshooting). The Sniffer's menus gain Help ▸ Help Center and ZacOS Help (⌘-?); the Sniffer now takes ⌘-? and ⌘-F (File ▸ Find showed ⌘F but the key did nothing). Installed to `share/zacos9/help`. Checked headless: Help menu, ZacOS Help, a search, following a result. Not done: full Hint Bubbles (menu-bar and non-Qt balloon help), help for other programs' own books. Test: `help-center` (books, ranking, excerpts, the made pages, history; every shipped page titled, described, linked, every link resolving) |
| `aceb24e` | **Pedigree** (Apple System Profiler; gap plan 2.13; `shell/pedigree/`, `zacos9-pedigree`). Tabs System Profile (System overview: ZacOS version from dpkg, distribution, kernel, computer name, user, time since startup, installed packages; Hardware overview: machine and kind from DMI, firmware, processor, cores/threads, speed, memory, swap, battery charge and condition; Displays: each graphics card from pci.ids with its driver and connected monitors, named from their EDID, with resolution; Network overview: interfaces, kind, status, hardware and IP addresses) and Devices and Volumes (disks with model, capacity, kind, partitions with size, mount point, format, free space, through LUKS/LVM holders; USB devices named from usb.ids; PCI devices; Bluetooth adapters; sound cards). Outlines open by their triangles, double-click, or Right/Left; sections start open. File > Save Report (text), Edit > Copy (a row, or a section with what's in it), View > Refresh, Open All, Close All. No root and no helper: what only root can read (memory slots, serial numbers) is left out. In the Zac menu's default accessories; the Sniffer's System Information opens it (the old summary window only if it can't be started). The disclosure triangle moved from `folderwindow.cpp` into panelkit (`panelDisclosure`). Checked headless on this MacBook Pro (both tabs, opening a disk, the exported menus). Test: `pedigree` (fixture tree: every section, ids parsing, EDID, sizes, the outline, the report) |
| `f492240` | **Startup Disk: Old Dog's startup disk** (gap plan 2.9, done; `shell/panels/startupdisk.cpp`, `shell/classic/zacos9-classic`). Below the GRUB list, "Old Dog starts up with:" offers the disks `zacos9-classic --check` would attach that have a System Folder (a blessed folder on an HFS or HFS+ volume, bare or in an Apple partition map; HFS volumes by their own name, CDs marked). The choice goes to `startup-disk=` in `~/.config/zacos9/classic.conf` (other lines kept); the launcher attaches that disk first and, for a CD, adds `--bootdriver -62`. A chosen disk that's gone is ignored. If Old Dog isn't set up, the launcher's own message is shown and the pop-up is dimmed. Checked against the real Mac OS 9 CD and installed disk. Not checked: SheepShaver actually starting from the CD when the installed disk is also attached (needs a Classic run); clicking the pop-up in a headless session (clicks didn't reach any control there, older ones included). Tests: `startupdisk-classic` (launcher order, `--bootdriver`, stale choice), `startupdisk-panel` (fake images and launcher) |
| `772c9a8` | **Find with criteria and contents; Find Similar Items; Index Selection** (Missing Manual ch. 15; gap plan 2.7; `shell/finder/findcriteria.cpp`, `findindex.cpp`, `findwindow.cpp`). Find: "Find items [on all disks / in my Home folder / in the front folder] whose" and up to six rows (More Choices / Fewer Choices) that must all match: name, contents, date modified/created, size, kind, label, lock, folder (empty/shared). Contents ask localsearch through tinysparql's full-text search (kept to the chosen place), and the other rows sift its answers; if the index isn't answering, Items Found says so. Items Found's header reads as the search. File ▸ Find Similar Items: the selected document's most distinctive words, any of them, ranked; File ▸ Index Selection: the selected folders (or the front window's) into localsearch's index. Test: `finder-find` (every criterion against real files; the SPARQL and its parsing; searches with a fake tinysparql); looked at headless |
| `f66f44b` | **Startup Disk** (Missing Manual p. 268; gap plan 2.9; `zacos9-controlpanel startupdisk`, `startupdisk/zacos9-startupdisk-helper`). The systems GRUB can start (ZacOS, others os-prober found, the firmware's settings), with icons, the current one selected; other kernels and recovery mode behind "Show every version". Choosing one makes it the startup system: the helper (Python, as root through pkexec; polkit lets local administrators without a password) accepts only ids it lists, switches GRUB to GRUB_DEFAULT=saved and regenerates grub.cfg the first time, then grub-set-default. Look for Other Systems sets GRUB_DISABLE_OS_PROBER=false and regenerates; Restart asks, then restarts. Old Dog's startup disk: see above. Tests: `startupdisk-helper` (a Debian-style grub.cfg: ids through submenus, quoting, defaults by number/id/title, set, detect), `startupdisk-panel` (fake helper) |
| `25d860d` | **Clippings and Show Clipboard** (Missing Manual pp. 168–170; gap plan 2.5; `shell/finder/clippings.cpp`). Text, a picture or a link dragged from another program onto the desktop or a folder window becomes a clipping where it was dropped: "First words clipping.txt", "picture clipping.png" (" 2" before the extension when taken), or an Internet location "Title.url" ([InternetShortcut]; the browser's title from text/x-moz-url). New icons (clipping, Internet location) and kinds ("text clipping", "picture clipping", "Internet location"). Opening a clipping shows it in a window; an Internet location opens its address in the default browser (`appOpenUrl`). Dragged out, a clipping carries its text or picture (an Internet location its address) alongside the file. Edit ▸ Show Clipboard: a window following the clipboard, "Clipboard contents: text/picture/none". Test: `finder-clippings`; looked at headless |
| `ba23095` | **Collar modules: media and File Sharing** (gap plan 2.16; `shell/collar/media.cpp`, `filesharing.cpp`). Media (Mac OS 9's CD Strip, for any MPRIS player): shown while a player runs, after Sound Set; a disc icon, coloured while playing; the title and artist, Play/Pause, Next/Previous Track, Show the player, and which player when several run. File Sharing: shown once set up in its panel, after Network; on (Mac/Windows) or off, Turn File Sharing On/Off through the sharing helper (turning on brings back the servers that were on), connected computers by address from `ss` (no root, no password prompts while polling), Open File Sharing Control Panel; a figure on the folder while anyone is connected. Tests: `collar-media-sharing` (fake MPRIS player on a private bus; fake sharing.conf, ss and helper); the other Collar tests keep out the computer's own players and sharing settings |
| `319b709` | **Trackpad and Accessibility panels** (Missing Manual pp. 237–239; gap plan 2.11). Trackpad (`zacos9-controlpanel trackpad`): tracking and double-click speed shared with Mouse; Use Trackpad for Clicking / Dragging / Drag Lock (`trackpad-tap`, `trackpad-drag`, `trackpad-drag-lock`), applied by zacos9-wm to libinput touchpads. Accessibility (`zacos9-controlpanel accessibility`; zacos9-wm `compositor/src/access.c`): **Sticky Keys** (a modifier pressed alone applies to the next key or click, twice locks it, Shift ×5 toggles; merged into the modifiers clients and zacos9-wm's shortcuts see; the menu bar shows held keys, locked in brackets, via platinum-shell-v1 v13 `sticky_keys`), **Slow Keys** (acceptance delay; physical keyboards only, so the menu bar's virtual keystrokes aren't delayed), **Mouse Keys** (keypad 1–9 move with initial delay and acceleration, 5 clicks, 0 holds, . lets go; ⌘-Shift-Clear on, Clear off), **Zoom** (CloseView-style 2–16× about the pointer so it stays put, ⌘-Option-K, ⌘-Option-↑/↓; `output.c` magnifies the scene's frame into another swapchain buffer). No colour inversion. Both panels in Control Panels (Sniffer, Zac menu). Tests: `access` (Sticky state machine, zoom steps and box), `accessibility-session` (Sticky latch/lock/Shift×5, Mouse Keys move/click/hold, zoom on/off by screenshot); looked at headless. Slow Keys has no automated test (virtual keyboards skip it) |
| `b3022d3` | **The Pack** (Mac OS 9's Application palette, Missing Manual pp. 103–104; gap plan 2.1; `shell/pack/`): drag the Application menu down past its bottom and let go to tear it off. A layer-shell palette above the windows, a tile per running program (wlr-foreign-toplevel; names and icons from desktop files; the Sniffer always there). Click brings forward (platinum-shell `activate_app`), Option-click hides the one left, documents dropped on a tile open with it. Title bar or ⌘-drag moves it; zoom box names on/off, Option-zoom large icons, Shift-Option-zoom across; collapse; close. Kept in `pack.ini`; reopened at login (`pack-open`); one per session. Tests: `pack-apps`, `pack-session` |
| `648939b` | **Function keys** (Missing Manual p. 105; gap plan 2.2): Keyboard panel ▸ Function Keys… assigns an application, document or folder to F1–F12 (Choose… from installed apps, Document…, Folder…, or drop onto a row; `fkey-N` in desktop.conf). zacos9-wm (`compositor/src/fkeys.c`) takes only assigned keys: a running application comes forward, else `gio launch` / `xdg-open`; modifiers leave keys to programs (Caps/Num Lock ignored); `fkeys-command=true` ("Use F1 through F12 as function keys") makes it ⌘-F-key. Tests: `function-keys`, `function-keys-session` |
| `adb58c6` | **Proxy icons** (Missing Manual p. 15; gap plan 2.4): a Sniffer folder window's small icon before its title (drawn by zacos9-wm; platinum-shell-v1 v12 `set_proxy_icon` / `proxy_drag`). Dragging it moves the folder (Option copies) like dragging its icon: zacos9-wm hands the press to the window just above its content (a program needs a press of its own to start a drag) and holds the pointer there until the Sniffer's drag begins. Home, disks, the Trash and virtual folders show the icon without dragging; a window whose folder moved away closes. A drag resting on a collapsed window's title bar springs it open (0.6 s) and it collapses again after. Tests: `decor` (layout), `proxy-icon` (real Sniffer) |
| `b60aee6` | **Dog Ears** (Stickies) and **Scribble** (SimpleText) (gap plan 2.12). Dog Ears (`zacos9-dogears`, Zac menu): coloured note windows saved as you type with colour, size and place (`dog-ears.json`); File ▸ New Note / Close Note (offers to save its text) / Import Text… / Export Text…; Color menu of six. Scribble (`zacos9-scribble`): a window per plain text file, UTF-8 or Latin-1, CR / CR LF / LF kept; New, Open…, Save, Save As…, Find…, Find Again, Font and Size menus; closing with changes asks. Their menus show in the menu bar; zacos9-wm lets both size their own windows. Tests: `scribble-files`, `dog-ears-notes`; clicked through headless |
| `e092dac` | **Internet control panel** (gap plan 2.10; `zacos9-controlpanel internet`): default web browser (http, https, text/html) and e-mail program (mailto) through GIO into `mimeapps.list`, and the downloads folder (`XDG_DOWNLOAD_DIR` in `user-dirs.dirs`, other lines kept). **Calculator** desk accessory (gap plan 2.12; `zacos9-calculator`): Mac OS keypad (tall +, wide 0), left-to-right arithmetic, = repeats, C/E, typed or clicked, ⌘C/⌘V; in the Zac menu's default items. Tests: `internet-defaults`, `calculator`; looked at headless |
| `998495f` | **Toy Box** (Mac OS 9's Launcher, Missing Manual pp. 43–46; gap plan 2.6; `shell/finder/toybox.cpp`). One-click bevel buttons for the aliases in System Folder ▸ Toy Box Items (`~/.config/zacos9/Toy Box Items`, registry version 12); bulleted folders ("•Games") are pages with topic buttons (eight at most), loose items the Applications page. Drop icons on it to add aliases (on a topic button: that page); Option-drag a button out to remove it, or Control-click it for Open / Remove From Toy Box; Control-click (⌘-click) a blank spot for Small, Medium or Large buttons. Remembers place, size, page and button size. Opened from Control Panels ▸ Toy Box (Zac menu, System Folder); General Controls ▸ Show Toy Box at system startup. Test: `finder-toy-box`; clicked through headless |
| `4aff226` | **List columns** (Missing Manual pp. 16–23, 415; gap plan 2.17): drag a header to move its column (Name stays first), drag a divider to resize; the pyramid button above the vertical scroll bar reverses the sort; order, widths and direction kept per folder (`column-order`, `column-widths`, `sort-reverse` in folders.ini); View ▸ Reset Column Positions. Dates shorten as the column narrows (long → medium → "10/1/26, 3:42 PM" → "10/1/26"; relative to "Today"), sizes drop decimals. ⌘-dragging an icon snaps it to the grid (windows and desktop). Tests: `finder-view-options`, `erase-disk-menu`; clicked through headless |
| `6ca2d02` | **Sniffer View Options and Preferences** (Missing Manual pp. 24–25, 413). View ▸ View Options… for the front window's view (or the desktop's arrangement): icon view standard/small icons (names beside), None / Always snap to grid / Keep arranged by …; button view standard/small, kept in order; list view columns Date Modified, Date Created, Size, Kind, Label, Comments (Get Info's `user.xdg.comment`), Version (an application's package version), relative dates (Today/Yesterday/Tomorrow), Calculate folder sizes (another thread), icons standard/small/tiny (12 px generic shapes); Set to Standard Views. Saved per folder (`folders.ini`); a new folder inherits its parent's view, else the standard views (`standard-views.ini`). Edit ▸ Preferences…: General (spring-loaded folders on/off and delay; grid spacing Tight/Wide for windows, buttons and the desktop), Views (standard views), Labels (seven names and colours from the 16-colour palette, `label-names`/`label-colors` in desktop.conf, read by `pl_label_name`/`pl_label_color` so the menu bar follows). View ▸ Sort List (shown columns) and View Options… in the View and contextual menus. Not done: Puppy Mode (Simple Finder), Reset Column Positions. Tests: `settings`, `finder-view-options`, `erase-disk-menu`; clicked through headless |
| `0bc632f` | **Keyboard layouts** (Mac OS 9's Keyboard panel and keyboard menu, Missing Manual ch. 12). The Keyboard panel lists every xkb layout and variant (`rules/evdev.xml`), the ones in use first; tick up to four (xkb's group limit; one at least). zacos9-wm builds one keymap from them as they change (`keyboard-layouts`, `compositor/src/kblayout.c`), else from Debian's `/etc/default/keyboard` (ignored until now), whose model and options always apply. ⌘-Space steps through them, ⇧ backwards, or ⌘-Option-Space (`keyboard-switch=option`); one layout leaves the key to programs. The one in use is remembered (`$XDG_STATE_HOME/zacos9/keyboard-layout`). The menu bar's keyboard menu (`shell/menubar/kbmenu.c`) sits between the clock and the Application menu with two layouts or more: an abbreviation (US, US-DV) rather than a flag, each layout an item. platinum-shell-v1 v11: `keyboard_layouts` event, `set_keyboard_layout` request. Tests: `keyboard-layouts`, `keyboard-layout-list`, `menu-keyboard`, `keyboard-layout-switch`; clicked through headless; checked in the VM with QEMU's USB keyboard (US q, ⌘-Space, Dvorak ', back to q; Debian's file alone set to German gives z for Y) |
| `45372c6` | **General Controls** control panel (Control Panels; `zacos9-controlpanel general`; Mac OS 9's, Missing Manual ch. 12). *Show Desktop when in background*, off: while another program is in front zacos9-wm hides the Sniffer's windows and swallows clicks on the desktop, and the Sniffer leaves out its icons (pattern stays); ⌘-Tab, the Application menu, or nothing else being left open brings it back. *Menu Blinking* Off/1/2/3 (`menu-blink`; the menu bar). *Insertion Point Blinking* slow/medium/fast (`caret-blink`; Qt programs through the ZacOS 9 style, the Sniffer live, GTK through GSettings `cursor-blink-time`). *Documents*: Open/Save start from the list of disks (folder set by the application, the default), in the folder last used in that program (`$XDG_STATE_HOME/zacos9/open-save-folders`), or in Documents (made if missing) — the latter two straight in the folder, with Other Locations…; Qt's own dialogs also start in Documents. Tests: `settings`, `filechooser-locations`, `filechooser-requests`, `window-tricks`, `session-ui`; clicked through headless |
| `d635c84` | **Recent Applications, Documents and Servers** in the Zac menu (`shell/menubar/recent.c`): programs whose windows appeared, newest first (`$XDG_STATE_HOME/zacos9/recent-applications`; only those with a desktop file, never the Sniffer); documents from the freedesktop `recently-used.xbel`, newest first, missing files left out, the Sniffer opening the one chosen — and the Sniffer now adds documents it opens there; servers Neighbourhood connected to (`recent-servers`), reconnected by `zacos9-netbrowser --connect afp|smb ADDRESS`. A **Zac Menu Options** control panel (Control Panels; `zacos9-controlpanel zacmenu`) sets how many of each (None, 5 … 40; 10 by default; None leaves the submenu out). Tests: `menu-recent`; clicked through by hand headless |
| `9a75372` | **Favorites**: File ▸ Add To Favorites (and on icons' and real disks' contextual menus) makes an alias, without " alias" in its name, in `~/.config/zacos9/Favorites` (shown as System Folder ▸ Favorites, registry version 11); the Zac menu's Favorites submenu lists them and the Sniffer opens the one chosen (`open-path`); favourite folders are offered as locations in Open and Save dialogs, after the desktop's. The menu bar now rebuilds its menus each time one opens, and its line to the Sniffer takes long paths. Tests: `erase-disk-menu`, `finder-vfs`, `filechooser-locations`; clicked through by hand headless |
| `5e98fdb` | **Folder-path menu** (Missing Manual p. 11): ⌘-click on a Sniffer window's title text lists the folder and every folder enclosing it, out to its disk (Inner, Stuff, Desktop, Home, Zacintosh HD); choose one to open it, and with Option-⌘ the window clicked closes. ⌘-↑ / Option-⌘-↑ do the same from the keyboard. zacos9-wm sends `platinum_shell_v1.title_menu` to programs that asked (`enable_title_menu`, protocol v10); the Sniffer works out the chain (`shell/finder/folderpath.cpp`: up through the Macintosh view, real folders up to the one a node stands for, `vfsPathBacking`) and the menu bar shows it at the pointer, button still down. ⌘-drag on the title's stripes still moves a window behind. Tests: `finder-vfs` (chains), `erase-disk-menu` (the menu); clicked through by hand in a headless session. No folder icons in the menu yet |
| `bd86742` | **Window tricks** (Mac OS 9, Missing Manual ch. 1): Option-click on the close box closes all of the program's windows; on the collapse box collapses only the program's (it used to collapse every window); ⌘-drag (or ⌘-double-click) a window behind by its title or edge, and it stays behind. **Zoom fits the contents**: the Sniffer's windows tell zacos9-wm the size just big enough for their icons or list (`platinum_shell_v1.set_standard_size`, protocol v9), and the zoom box goes there, kept on screen; Option-click fills the screen; a client's maximize fills it. Tests: `window-tricks` (headless, two programs); zoom checked by hand in a headless session |
| `d9af3bd` | **Contextual menus** in the Sniffer: Control-click (or right-click; ⌘-click too, as ⌘ reaches apps as Control) on an icon, a disk, the Trash, a window's background or the desktop selects as a click does, then the Sniffer sends `context KIND` after its state, and the menu bar opens the menu at the pointer with its own painter, submenus and blinking (press-drag-release, or a click leaves it open). Only what can be done now is listed (Mac OS 9's rule). zacos9-wm now refocuses the pointer when a keyboard-exclusive layer maps, so the menu knows where the pointer is at once. Tests: `erase-disk-menu` (each kind's items), `session-ui` (renamed from `app-switch`: a desktop contextual menu's New Folder) |
| `ad17c6a` | Special ▸ **Eject** (⌘E): every selected disk, local or network (replaces Disconnect Network Drive and Unmount Disk); Special ▸ **Log Out…** (⇧⌘Q in the Sniffer): the Sniffer asks, then the menu bar (`shell/menubar/logout.c`) asks every program to close its windows and runs `loginctl terminate-session` once none is left; a program still open after 30 s cancels it and the Sniffer says so. `$ZACOS9_LOGOUT_COMMAND` overrides the command (tests). Tests: `erase-disk-menu` (menu gating), `app-switch` (Log Out end to end). Known flaky, unrelated: `finder-selection`'s "same-disk moves also show a status dialog" polls for a dialog that can come and go between polls |
| `0b701df` | ⌘-Tab / ⇧⌘-Tab switch programs, alphabetically by name as Mac OS 9 did, the Sniffer among them (with no windows, as a desktop click: the desktop gets the keys); hidden programs come back. zacos9-wm catches the key and asks the menu bar (it knows the names) through `platinum_shell_v1.switch_app`; the menu bar answers with `activate_app` (protocol v8). Option-click on the desktop hides the program you leave. Tests: `menu-app-switch` (the choice) and `app-switch` (real menu bar, Sniffer and windows, headless; `vptr hold/release` added) |
| `c417474` | ZacOS names for features shipped under Apple's: Extensions Manager → Tricks Manager, File Exchange → File Translator, Network Browser → Neighbourhood, Classic → Old Dog, Energy Saver → Nap Time, Platinum → Silver Coat (theme preset, session and GTK theme descriptions). Shown names only — binaries, settings keys, the `~/Classic` folder and the Sniffer registry ids are unchanged; Control Panels lists re-sorted. See `docs/mac-os-9-gap-plan.md` (glossary) |
| `f988700` | Force Quit in the Apple menu: a submenu of the open applications (front-most first, not the Finder); choosing one has the Finder ask "Force “App” to quit? Any unsaved changes in it will be lost." and then send `platinum_shell_v1.force_quit` (protocol v7), on which `zacos9-wm` SIGKILLs every process behind that app id's windows (never itself). Checked in a headless session (foot killed, another foot outside it untouched) and in the VM (the Finder's command from the menu item: the alert, Return, the Installer gone, the Finder still running); the submenu itself not clicked through (the VM's pointer didn't reach the session) |
| `84dd640` | Platinum login window: `session/zacos9-greeter` runs `zacos9-login` (`shell/greeter/`: pick a name, password, Log In; Restart, Shut Down) in a bare `zacos9-wm` (`ZACOS9_LOGIN_WINDOW`: no menu bar, Finder, collar, start-up screen, installer or Setup Assistant); it speaks greetd's IPC (`greeter/greetd.cpp`, PAM stays in greetd), remembers the last user, and falls back to tuigreet if the window can't run. `greetd-client` test passes; drawn in a headless zacos9-wm. In the VM (`scripts/dev-boot.sh`), under the real greetd after logging the live session out: a wrong password shows "Sorry, that password isn't right.", the right one starts the desktop as that user and the greeter's processes are gone. Restart and Shut Down not tried (the VM's pointer didn't reach the session) |
| `c417474` | Apple-menu icon is now Happy Zac's head drawn on its own (`assets/menubar/happy-zac-mini.png`), whole and on transparency instead of a square crop of the start-up picture, 20×15 so it fills the bar's height with a 1 px margin (`MBAR_ICON_W/H` in `lib/menudraw.h`; alpha-blended now): `tools/menubar/make-menu-logo.py` regenerates `logo16_data` in `lib/logo.c` |
| `c417474` | Busy pointer is now a rainbow dog bone (`assets/cursors/dog-bone.svg`) spinning clockwise instead of the tail-wagging/backflipping dog: `tools/cursors/build_cursors.py` draws the SVG antialiased at 16 and 32 px (24 frames, 60 ms, hot spot at the centre); `cursor-theme` and `busy-cursor-protocols` tests pass |
| `c417474` | "Ask for my password to wake" (Screen Saver panel, off by default): `zacos9-lock` (ext-session-lock-v1, PAM) locks once every screen is idle, before any system sleep (logind delay inhibitor) and on `loginctl lock-session` / ScreenSaver.Lock; the screen saver plays above the lock — see `docs/energy-saver.md` |
| `39d1eaa` | Energy Saver: display sleep and system sleep in `zacos9-wm` (per screen: a full-screen window or a video's idle inhibitor keeps its screen awake while the others sleep; the system sleeps only when none is kept awake), the Energy Saver control panel; the screen saver (`zacos9-screensaver`: Desktop Patterns, Starfield, Bouncing Logo, Blank Screen, per screen, ahead of display sleep) and its Screen Saver panel with a live preview; `zacos9-inhibit` answers `org.freedesktop.ScreenSaver` and the portal's Inhibit, keeping the asking app's screens awake — see `docs/energy-saver.md` |
| `82b77c3` | Regression fixes after 0.1.50 (released in 0.1.51): VS Code settings are read as JSONC before adding the native title bar (no insertion inside a comment, no truncation past 1 MiB, unreadable files never overwritten); AFP volumes mount at `mounts/SERVER/ID/NAME` and an existing mount is reused only if it is the volume asked for — see `docs/app-integration.md`, `docs/network.md` |
| `c417474` | Silent start-up: the platinum sparkle logo is replaced by Happy Zac, and the Welcome box is now "Welcome to Zacintosh." with the ZacOS logo in a box like the 1984 Macintosh's (no progress bar) — see below |
| `cd4f6ed` | Silent start-up: white screen, platinum sparkle logo, Welcome screen; `scripts/dev-boot.sh` |
| `d1f5765` | File Sharing control panel (AFP + SMB toggle, shared folders list) |
| `02cd5a4` | Get Info > Sharing view, pixel-matched to Mac OS 9's original |
| `2349968` | `network/afp/zacos9-afp`: AFP 2.x FUSE client skeleton |
| `3dbae73` | AFP client fully working: single-`sendmsg` fix, busy-delete retry, truncate fallback; `tests/sharing/afp-client.sh` passes on Mac OS 9 and Netatalk |
| `c417474` | Virtual Macintosh filesystem for the Finder, with real application icons, package origin, and hide-not-uninstall — see below |
| `c417474` | Fetch Software (app catalog over apt) and New Tricks (software updates) in the Apple menu — see below |
| `c417474` | Network Browser: connect to AFP and SMB servers, mounted volumes on the desktop — see below |
| `4e3ebe6` | ZacOS 9 rename (from Platinum 2026) |
| `996b311` | Applications: empty on fresh install, flat launchers |
| `634add8` | Finder: local disk volumes on the desktop (GVolumeMonitor) |
| `8771336` | Windows app compatibility: `.exe`/`.msi` open via Wine |
| `c417474` | Mac OS 9-style installer: Welcome → Select Disk → Installing → Done, runs `zacos9-install` via polkit; partition-level install (preserve FAT/HFS+ sibling partitions) |
| `c417474` | New ZacOS 9 SVG logo at 16×16 (menu bar) and 64×64 HQ (startup, installer) with real alpha blending |
| `c417474` | Installer fixed end to end: GRUB packages in the image, UEFI/BIOS detection, BIOS boot partition, fallback `EFI/BOOT/BOOTX64.EFI` for Macs, live user in `sudo` (polkit), failing step shown in the window. VM-tested (`build/test-install.py`): whole disk under BIOS and UEFI, and a partition beside a kept FAT partition under UEFI — each installed disk boots to the desktop. Boot splash now shows the logo and a progress bar. |

## Current work

**RDP remote desktop** (0.1.44 published): Remmina 1.4.39 and its
RDP plugin are installed and the client opens; FreeRDP 3.15.0 is available.
The xrdp/WayVNC bridge is implemented; installed native development package
`0.1.43+rdptest2` replaces the initial local test add-on and persists the compositor fix.
WayVNC uses a private mode-0700 Unix socket; xrdp's VNC backend is isolated
in a network namespace with an RFB 3.3-to-3.8 adapter running as the desktop
user. The external proxy and xrdp run unprivileged, require TLS and PAM, and
authorize only the sharing user's account. No VNC TCP port is exposed.
Authentication logs live in a root-owned runtime directory, separate from
the xrdp-writable transport socket and application log directory.
Default xrdp services remain masked. Sharing is on demand, with no boot
enablement. Testing used 127.0.0.1:3389; the host is now stopped, with no LAN
or localhost RDP/VNC listener left open.
Certificate-pinned RDP delivered real 3840x2160 desktop video and keyboard
text to a synthetic GTK window. The disposable test account was removed.
Mouse testing exposed ignored virtual-pointer output suggestions in the
compositor. The root fix and C regression pass; a real two-output, scale-2
headless capture/input probe verified correctly positioned clicks and typing.
The live desktop needs a restart to activate the compositor change before
the full live self-test or a second-computer session can be certified.
Eleven RDP configuration/transport regressions pass. The native amd64 package
passed 75 tests with four skipped and none failing. Clipboard, audio, drive redirection
and automatic resizing are deliberately disabled. See `docs/rdp.md`.
The 0.1.44 release package also passed 75 tests with four skipped and none
failing. GitHub's latest release is v0.1.44; downloading its amd64 Debian
asset produced an exact byte match with the tested package (SHA-256
`2cd74c89cf60e9b27507a77b295ce37542dd0da4199796733c6698d78561b435`).
The release is not automatically installed; restart the desktop after updating.
Sunshine is stopped and its user-service autostart disabled; credentials and
the separate RustDesk installation are retained.

**Sunshine and Moonlight two-way sharing** (local setup, not released):
Sunshine's checksum-verified Debian Trixie package and Moonlight 6.2.0 from
the existing user Flathub remote are installed. Before the RDP pivot, Sunshine
ran through its user service, initially with localhost-only access, UPnP off, a Desktop-only app list,
KMS capture and software H.264 encoding. Its monitor/encoder probes succeed
and local setup page returns HTTP 200; Moonlight opens. The uinput module was
loaded through administrator authorization and the packaged active-seat rule
grants the local user access without world-writable permissions.
Credentials and client pairing are left to the user; a two-computer video/input
session is not yet verified. Nouveau warns about cursor capture without atomic
modesetting. RustDesk is retained but its incoming session showed a black
screen. See `docs/sunshine-moonlight.md` for two-way setup and limitations.
Follow-up: LAN hosting now listens on IPv4 interfaces, with the streaming
endpoint verified at 192.168.2.110. Administration remains local-only and UPnP
off. Credentials were subsequently created, but the setup page returned HTTP
401 in the integrated browser. The user rejected this workflow; the Sunshine
host is now stopped and its user-service autostart disabled.

**RustDesk screen sharing** (initial integration, published in 0.1.43): integrate the official
native application rather than writing a remote-desktop engine. ScreenCast
routes to the wlroots portal while FileChooser stays on Finder. PipeWire,
WirePlumber and the portal backend are required runtime dependencies.
Attended sharing and native service input are documented; no permanent
password, relay server or relaxed device permissions are configured.
RustDesk 1.5.0 is installed and open on the live desktop; its service runs.
A user portal preference enables monitor capture now without replacing Finder.
Five focused suites pass, including a private GLES2 ScreenCast session that
exports a stream and an authorized PipeWire socket. Frame-consumption testing
did not succeed; no second-computer session has verified video or remote input.
Pixman screencopy format negotiation failed in the isolated probe. Hosting is
not yet certified; see `docs/screen-sharing.md` for setup and limitations.
The amd64 Debian package passed 73 tests with four skipped and none failing;
the isolated session test passed separately in the live development environment.
GitHub's latest release is v0.1.43 and the uploaded package digest matches the
tested artifact. RustDesk remains a separate install, not bundled by this update.

**Alias replacement warning fix** (0.1.42; published, not installed): unchanged Desktop aliases
were reporting replaced originals because Linux device numbers changed while
their target inodes stayed the same. Alias records now include the filesystem
ID, so identity checks and bounded reconnection survive device renumbering.
Legacy records migrate on use when the inode and recorded canonical path still
match. Genuine replacements still require confirmation. Finder and the file
portal build; the alias and three file-chooser suites pass. Copies of all 11
live Desktop alias records resolve twice without warnings or errors (six had
stale device numbers on ext4); the live aliases were not changed. The 0.1.42
amd64 Debian package builds with 72 tests passing, three skipped and none
failing. Published on GitHub and verified as the latest release, with the
uploaded package's SHA-256 matching the tested artifact. Software Update
will offer it; restart after installation to use the new Finder.

**The Collar correction** (uncommitted, after 0.1.40): the frame is mirrored
to match the user's open and closed reference screenshots. The close box and
recessed scroll arrows are on the left, 30-pixel module cells start at x=29,
and the 18-pixel grip is on the right. The open width is `60 + 31 × modules`.
Closed, only the 18-pixel grip remains.
The strip sits flush in the bottom-left corner. Command-drag moves it between
the screen bottom and the menu bar; Escape cancels the move.
New modules:
- A Sound Set switcher. Collar sounds follow the current set.
- Per-display Resolution and Brightness modules. Built-in panels use the
  backlight through logind; external monitors use DDC/CI through `ddcutil`,
  which is now recommended and in the live image.
Every menu is headed by its device label, for example "Display 2: NAME".
Over-long sound-set and resolution lists are trimmed around the checked entry,
with a "More..." item that opens the matching control panel.
Pixel diffs against the references match apart from the reference Mac's
rounded screen corner.
New `collar-displays` suite. The Collar, sound and theme suites pass.
DDC/CI is not yet verified on real monitors. Not released or installed.

**KDE Connect in The Collar and the Collar control panel** (uncommitted):
- When KDE Connect is installed, each paired, reachable device gets a Phone
  module. It offers battery and signal, browse, clipboard, ring, send files,
  SMS, ping, remote commands, pairing requests and settings, all over the
  daemon's D-Bus API.
- New `zacos9-collarpanel`, laid out from the Control Strip panel:
  - Show/Hide: show, hide, or a hot key, with a key recorder;
  - Font Settings for the module menus.
- `zacos9-wm` matches the hot key (default ⌘F8) and runs
  `zacos9-collar --toggle`.
- The Collar's artwork moved to `collarart.cpp`, so the panel can draw its
  preview.
- New suites: `collar-kdeconnect`, `collar-panel` and `collar-hotkey`.
  `collar-native` now presses ⌘F8 in a real compositor.
- Not tested against a real phone. Not released or installed.

**Keychain module in The Collar** (uncommitted): ChatGPT, an Electron app,
wasn't opening because it waits on gnome-keyring's "Unlock Keyring" prompt.
With automatic login, `pam_gnome_keyring` never receives the password, so the
keychains start locked; that design is deliberate. The new Keychain module,
modelled on Mac OS 9's Keychain Strip, talks to the Secret Service over D-Bus:
- lists the keychains and checks the default (choosing one sets the default);
- Unlock... shows the keyring's own password prompt;
- Lock and Lock All Keychains;
- Keychain Access... (Seahorse), when installed.
The padlock icon opens when the default keychain is unlocked. New
`collar-keychain` suite with a fake Secret Service; checked against the live
gnome-keyring (lists Login and Default Keyring). Not released or installed.

**Picture Viewer** (`zacos9-picture`, uncommitted): the default application
for image files (PNG, JPEG, GIF, BMP, WebP, TIFF, SVG, ICO, ICNS, TGA,
PNM, XBM, XPM) through `zacos9-mimeapps.list`, replacing ImageMagick's
`display`. A plain Qt window in the Platinum style with File and View menus in
the global menu bar. Pictures larger than the screen open fitted to it; smaller
ones open at actual size, and the window wraps the picture. The bottom bar has
zoom − / + buttons, the zoom percentage and the pixel dimensions. ⌘+ / ⌘−,
⌘0 (Actual Size), ⌘9 (Fit to Screen), ⌘-scroll zooms at the pointer, and
dragging pans. zacos9-wm lets it size its own window, as it does the Finder.
New `picture-viewer` offscreen suite; checked in a headless zacos9-wm. Needs
`qt6-image-formats-plugins` (added to Depends). Not released or installed.

**The Collar: screenshot-matched desktop control strip** (0.1.40; published, not installed
on the live desktop): original pixel artwork and Platinum menus, five modern
modules (volume/output, network/Wi-Fi, Bluetooth, backlight brightness, and
battery/sleep). The 24-pixel-high strip folds to a 17-pixel angled grip, resizes and
scrolls its modules, supports Alt-drag along the left edge and keyboard
navigation, and remembers its folded state, size and position. The compositor
starts it alongside Finder and the menu bar; `ZACOS9_COLLAR=''` disables it.
Services are asynchronous, missing hardware is disabled explicitly, and
failed user commands show alerts rather than success-shaped fallbacks.
Native testing exposed a compositor crash on parentless xdg popups awaiting
layer-shell attachment; those now use the shared popup configuration and
constraint lifecycle after attaching. Normal window popup behavior is
preserved. Focused UI/backend/audio tests and native 1x/2x committed-pixel
checks pass, including real pointer/keyboard popup opening and dismissal.
The native scenario also passes with an address-sanitized compositor.
Final focused regression run: 11 suites passed, including startup/launch
feedback, ordinary fullscreen/window behavior, cursor behavior, GTK menus,
snapshots and sound outputs alongside The Collar's tests.
Full pre-release run: 65 passed, 3 skipped, and one native-snapshot suite
failed on frame-readiness checks; two isolated reruns of that suite passed.
Debian package tests now run serially while compilation stays parallel.
Release 0.1.40 combines The Collar with the automatic-login restoration from
0.1.39, without a separate 0.1.39 publication.
The clean release package build passed all 66 runnable suites, with 3 skipped,
no failures and no timeouts (69 total), including native-snapshot and all
three Collar suites. Published
[ZacOS 9 0.1.40](https://github.com/chromasystemprofessional/Zac-OS-9/releases/tag/v0.1.40)
with the amd64 Debian package; its uploaded SHA-256 matches the local package,
which contains `zacos9-collar` and depends on UPower. No live installation,
desktop restart or ISO rebuild was performed.
The user's 2026-10-09 screenshot now guides the frame: a 17-pixel angled
grip, 14-pixel hollow-arrow scroll buttons, 32-pixel beveled module cells
with distinct menu triangles, a square 14-pixel end tab and a neutral
`#C0C0C0` face. Five modern modules yield a 219-by-24-pixel expanded strip.
Open control-panel commands lead the menus; open modules reverse their
bevels until dismissal. Original colored ZacOS icons replace the reference's
legacy artwork. The enlarged/compressed image supports those proportions,
not a byte-identical claim for its colors, fonts or original icons. See
[The Collar](docs/collar.md).

**Explicit disk disconnect/unmount menus and password login** (0.1.38):
Finder's Special menu now has Disconnect Network Drive
(AFP/SMB) and Unmount Disk (local disks, including USB), enabled only for the
appropriate single selected mounted disk. Startup disk and ordinary folders
are excluded. Existing Put Away is preserved through the same eject helpers.
Successful network disconnect removes the icon immediately without an
unexpected-disconnection warning; failures are reported, including GIO local
unmount errors. AFP/SMB command waits service Qt events instead of blocking
with `waitForFinished`.
All five focused suites pass (`erase-disk-menu`, `finder-desktop`,
`afp-disconnect`, `filechooser-locations`, `filechooser-requests`), covering
menu gating, USB helper routing, successful SMB disconnect without warning,
and a refused SMB unmount whose exit code is zero. Installed tested Finder,
menu bar, Network Browser and chooser with administrator authorization and
restarted Finder/menu bar with user approval on 2026-10-08. Both services are
active; the live Finder sends the new `network`/`unmount` fields, and the
chooser responds on D-Bus. Included in the 0.1.38 release. Full pre-release
suite: 63 passed, 3 skipped, no failures (66 tests).

**Automatic login retained** (0.1.39): the earlier desktop-startup password
prompt was undone at the user's request. The live machine's
`/etc/greetd/config.toml` was restored byte-for-byte from its backup, so
`[initial_session]` logs the user straight in again; the guidance in
[packaging](docs/packaging.md) now says applications request credentials
through their own normal sign-in flow at launch instead. No keyring files or
stored secrets were read or changed, and no other installation's login
settings are affected. Full pre-release suite: 63 passed, 3 skipped, no
failures (66 tests). Note that this suite must be run serially
(`meson test -C build --num-processes 1`) on a machine with a live desktop
session: in parallel, `filechooser-lifecycle`, `native-snapshot`,
`finder-selection` and `filechooser-locations` fail or time out on contention
for gvfs, the compositor and dpkg, not on any code defect.

**AFP disconnected disks** (0.1.38): confirmed the user
remounted `adam's home` successfully. AFP transport/session failure now ends
the failed FUSE mount rather than leaving an I/O-error disk behind. Idle
sockets are monitored, with TCP keepalive/user timeouts for silent outages.
After unmounting, the AFP daemon notifies Finder through its existing socket;
the desktop icon is removed immediately and a Platinum Server Disconnected
dialog names the disk and warns about unsaved changes. Normal eject and
ordinary file errors remain silent. All five focused suites pass; eight real
FUSE lifecycle scenarios unmounted within 0.00-1.02 seconds in a measured run,
including background daemon teardown. Finder socket tests verify the icon is
removed before its dialog appears and unrelated disks remain.
Installed the tested AFP client, Finder, Network Browser and chooser with
administrator authorization on 2026-10-08. Restarted Finder with user approval
and reloaded the chooser; both live endpoints respond, and installed binaries
match the builds. Kept the working AFP mount untouched: it retains the old
daemon until the next eject/remount. Included in the 0.1.38 release.
See [network](docs/network.md).

**Desktop folder aliases in the application chooser** (0.1.38; installed live): the
location screen lists Desktop folder aliases alongside disks, with their own
names, italic labels and Platinum folder icons. Targets may be any directory,
including mounted AFP or Windows/SMB folders outside Home. Open, folder
selection, SaveFile and SaveFiles return real target paths. Aliases can also
be followed from Home/Desktop in the Macintosh view. Missing or replaced
originals report errors instead of falling back to Home. Installed the tested
backend at `/usr/bin/zacos9-file-portal` with administrator authorization on
2026-10-08 and restarted only the chooser backend. Its new live process owns
the portal D-Bus service and responds to introspection; the installed binary's
SHA-256 matches the tested build. The user confirmed the AFP disk works after
remounting. Included in the 0.1.38 release. Focused tests pass:
`filechooser-locations`, `filechooser-requests`, `filechooser-lifecycle`,
`finder-desktop` and `alias`. Regressions use external-folder fixtures for
AFP/SMB paths and cover chained aliases, renamed originals, unavailable and
replaced targets, outside-location rejection and all four chooser operations.
Follow-up: the live AFP disk `adam's home` was absent because its mount
returned an Input/output error and Qt omitted it as not ready. The chooser
now discovers AFP and kernel SMB disks from active mount records and GVFS
Windows shares from their local paths, so an unavailable disk remains listed
and reports an access error. The real Desktop directory also contains a
`Home alias` pointing to `/home/adam`; it is not the AFP disk. After explicit
user approval, moved that alias and its identity record to Trash, preserving
the real Home folder. New chooser requests no longer list that alias.
Installed the correction with administrator authorization and restarted only
the chooser backend. Visually verified the actual live chooser lists
`adam's home` as a disk alongside the local disks (before trashing `Home alias`). All five
focused suites still pass. The user subsequently remounted the AFP disk and
confirmed it works; the disconnected-mount lifecycle fix is described above.

**Startup Items by drag and drop** (unreleased): dropping an application
into System Folder ▸ Startup Items (on the folder or into its window) makes
it open at login. Dragging a startup item to the Trash, or Move To Trash,
stops it. User-added entries go into the Trash and can be put away or
dragged back; system entries are masked with a `Hidden=true` override.
Items show their application's icon. Covered by `autostart` (add, re-enable,
refuse missing programs and non-applications, trash, restore, system mask)
and `finder-desktop`: an Applications drag into Startup Items, a drag to the
Trash, the drag back, and a refused document. Full suite: 62 ok, 3 display
skips. Not yet tried by hand in the live session.

**File associations** (unreleased): file names are shown without their
extension, and rename keeps the hidden one. Documents show the icon of the
application that opens them on a Platinum page. The new **File Exchange**
control panel picks the application for each type through GIO and the user's
`mimeapps.list`, the same default Open uses. `finder-fileassoc` covers this
(41 checks), and the full suite passes (62 ok, 3 display skips). Seen in a
nested session with a private HOME: hidden extensions, PDF and CSV documents
bearing their apps' icons, a photo preview, and the panel showing types and
their default apps. Change… and the Open With dialog have not been clicked
through, and nothing has been installed or seen in the live session yet.

**Picture previews** (unreleased): picture files show a framed preview of
their picture instead of the document icon on the Desktop, in icon and list
views, Find results and Get Info. Previews are decoded off the UI thread and
stored in the freedesktop thumbnail cache (validated against GdkPixbuf: URI,
modification time, file name hash, chunk CRCs). `finder-thumbnails` covers the
cache, edits, orientation, framing, failures, aliases, repaints and painting;
the Finder, chooser and VFS suites still pass. Not yet installed or seen live.

**VS Code chooser failure** (unreleased fix for 0.1.36): the installed-session
log confirmed `Backend call failed: Malformed FileChooser request`. The backend
now decodes the actual D-Bus `a{sv}` dictionary and reads nested arrays rather
than attempting to write them. Requests are dispatched with delayed replies,
expose `Request.Close`, reject overlapping requests explicitly, and no longer
terminate the backend when its last dialog closes. Multi-file save now asks
for its destination instead of accepting the suggested directory without
confirmation. Offscreen wire-level tests cover files, folders, SaveFile,
SaveFiles, both cancellation screens, and repeated use of the actual backend
process. Live VS Code selection has not yet been verified.

The corrected backend was installed with administrator approval; the user
confirmed VS Code's chooser opens and selects again. Follow-up refinement
always begins at Desktop drive icons, ignores application-suggested Home/Unix
paths, and restricts the file browser to the selected drive, including typed
paths and escaping symlinks. The user confirmed the initial restriction was too
narrow: Zacintosh HD now exposes Home files without Unix ancestors. The drive
list includes GIO data mounts, AFP, and active Mac/APFS mount records with
Desktop names; read-only disks are not excluded. Inaccessible disks report an
error rather than falling back. Offscreen regressions exercise drive-first
selection and confinement. Installed with administrator approval and restarted
only the chooser backend; the user confirmed in live VS Code that Home files
and both ChromaSystem Professional drives are available.

Chooser drive icons now use the Finder Desktop's Platinum disk icon, including
its USB badge, instead of Qt's generic drive icon; a pixel-exact regression
compares them with Desktop `paintIcon` output. Rejecting an outside item (such
as typed `/`) now clears the stale selection, which previously blocked a later
valid choice. Installed with administrator approval; the user confirmed the
icons match the Desktop.

Zacintosh HD in the chooser now shows the Finder's Macintosh view (System
Folder, Applications, Home, user folders) instead of raw Home: a per-request
read-only tree of real folders and links built from the Finder VFS, with each
application linked to its `.desktop` launcher and generated listings omitted.
Only items inside the linked real folders can be chosen; applications get real
paths. The Finder's ~5-second first application scan runs as a backend
warm-up, which the session triggers at login; requests wait for it, and a
request closed meanwhile is cancelled at once (previously it stayed pending
until the scan finished). Regressions cover the tree, browse-versus-choose
rules, link escapes, real-path results, save refusal in the view's own folders,
cancellation during warm-up and of a displayed chooser, and isolation from the
live session's chooser tree. Not yet installed or confirmed live.

**About This Computer, startup-disk Get Info, and application file chooser**
(0.1.36): About This Computer now presents installed version, processor,
graphics, and memory in human-readable form. Zacintosh HD Get Info reports the
home filesystem's capacity and free space without Source or Access rows. A
ZacOS FileChooser portal backend presents the private Zacintosh HD workspace
and mounted volumes with no places sidebar, and the session selects it for
standard GTK/Qt dialogs. The backend builds; its location/dialog tests,
Finder desktop/VFS tests, and D-Bus interface introspection pass. GTK/Qt portal
activation and visual behavior have not been exercised in a live desktop.
Details: [application integration](docs/app-integration.md) and
[Finder VFS](docs/vfs.md).

**Third-party application dialogs** (0.1.36): GTK 3 message prompts now
have explicit Platinum dialog/action-area styling; Qt 6 prompts inherit the
ZacOS palette and controls and have a palette regression assertion. Added a
GTK 4 stylesheet for applications that honor toolkit themes; GTK 4 and
libadwaita behavior is not verified in this build environment. GTK 3 theme
parsing is checked by `gtk-theme-css`.

**Classic monochrome graphics coverage** (unreleased, gate not passed):
expanded native capture to preserve monochrome pen/text/color-value state and
explicit application clipping, with `DrawText`, frames/lines, masked/scaled
`CopyBits`, backing-store self-copy and `ScrollRect` hooks. Replay and original
compound-call guards prevent nested double capture; helper scratch regions stay
in the helper heap, and the caller's port/zone are restored. The hook-free
fixture's opt-in graphics mode exercises styles, XOR/OR modes, spacing, pen
patterns, clipping, images and alternating scroll directions. Version-2 exports
require independent guest references and full meaningful-pixel comparison.
Actual graphics trials match both hidden/visible buffers at frames 36, 52 and
67, including 356x220 resize; a frame-82 original-pattern regression also passes.
Diagnostics confirm every added hook ran. Color-window testing explicitly
refuses capture without overwriting old evidence; color/PixMap/GWorld capture
is the next prerequisite, not implemented. Capture restarts after rejection,
closed-window invalidation and all-eight-vector restoration tested. Fourteen
host tests and strict builds of all three guest apps pass. Copied guest shut
down through Finder; original disk checksum is unchanged.
See [graphics experiment](emulation/seamless/README.md#monochrome-drawing-state-text-images-and-scrolling).

**Classic native execution-context capture** (unreleased, gate not passed):
the helper now resolves PowerPC InterfaceLib transition vectors through CFM
and reversibly hooks `PaintRect`, `EraseRect` and `DisposeWindow`, chaining
saved native entry/TOC pairs. This reaches the separate, hook-free fixture's
process context without modifying its binary, window procedures or clipping
regions. Native observation counted 75,873 fixture calls; its process-context
Window Manager list is accessible there. Helper-owned independent buffers
match both references exactly at frames 54 and 69 with either window fully
hidden, after resizing A to 356x220, and after advancing to frame 84 with B in
front. Screen crops still fail the hidden-window controls. Window close
invalidates captures; exports refuse a closed pair. App exit/relaunch produces
fresh exact frame-13 captures. Callback lifetime remains tied to the running
helper; it refuses unsafe unloading if another patch changes the vector chain.
No startup extension or emulator patch was necessary for this bounded result.
Ten host verifier tests pass, including independent frame/dimension/reference
checks. General text/color/blit capture and production recovery remain unproven.
The final guarded helper passes a fresh frame-29 trial. All six native trials
pass independent-reference CLI checks; screen negative controls fail as expected.
Native vectors restored, guest apps exited normally and copied guest shut down
through Finder; original disk checksum remains unchanged.
See [native capture experiment](emulation/seamless/README.md#native-powerpc-execution-context-and-capture).

**Classic separate-app/helper trial** (earlier result, Toolbox-only path blocked):
split the original drawing fixture into a separate `ZcDF` application with no
capture hooks and added a `ZcSH` diagnostic helper. Shared surface/export code
preserves the first in-process probe. Strict PowerPC builds pass for all three
apps. In the copied guest, Process Manager enumeration sees both apps, but the
helper's Window Manager list contains zero fixture windows. A reversible
rectangle-trap observer sees helper calls but zero fixture rectangles while
the separate fixture advances from frame 180 to 200 and changes front window.
Frame-200 references remain exact; the hidden screen crop fails all 61,440
pixels. Removed unexercised external replay code rather than claiming capture
success. The subsequent native CFM-vector experiment above establishes target
execution context without a startup extension. Precise Toolbox trap scoping
remains unresolved; no production integration.
Verified observer teardown, normal app exits and Finder shutdown of the copied
guest; original disk checksum is unchanged. Nine host verifier tests pass.
See [separate-app experiment](emulation/seamless/README.md#separate-applicationhelper-experiment).

**Seamless Classic rendering spike** (unreleased, gate not passed): inspected
the pinned macemu revision in a separate temporary checkout. SDL presents one
guest framebuffer; Native QuickDraw acceleration hooks and native thunks are
candidate integration points, not independent per-window backing stores.
Added an original PowerPC two-window QuickDraw probe with overlapping animated
patterns, cooperative offscreen reference buffers, rectangle-hook candidate
buffers, screen-crop exports,
resize and cancelled-close controls. Cross-compilation with the open-source
Retro68/Multiversal toolchain passes strict warnings and generates resources,
MacBinary and an application-only HFS disk. Added an exact-pixel host verifier
and nine focused tests for stale/corrupt/cropped frames and limits, wired into
Meson as `classic-window-probe`. Actual trials in a disposable copy of the
user's Classic setup passed exact-pixel reference and rectangle-hook capture
checks with either window fully obscured, after advancing the frame and after
resizing to 356x220. Hidden screen crops fail completely (61,440 wrong pixels
at 320x192); partial-overlap cropping also fails. Cancelling Quit preserves
both windows, and closing one preserves the other's exact pixels. The copied
guest was shut down through Finder; the original disk's SHA-256 is unchanged.
This proves only controlled black/white rectangle interception
before visibility clipping, not general rendering. No production emulator or
launcher changes, original guest disk writes or Apple SDK redistribution.
Unmodified app hook installation, cross-process discovery, other drawing
operations and the feasibility gate remain unverified. Exact guest OS version
is not yet confirmed. See [spike instructions](emulation/seamless/README.md).

**Seamless Classic windows plan** (milestone 0 started): agreed target is independent
SheepShaver application windows with a permanent full-desktop fallback, not
screen cropping. [Implementation plan](docs/classic-seamless.md) starts with
a rendering feasibility gate for obscured windows, then covers a guest helper,
versioned bridge, direct launching, host windows, dialogs/recovery, menus,
clipboard and compatibility packaging. No seamless implementation or schedule
is claimed; general guest rendering still needs proof.

**Alias icon inheritance** (0.1.30): Finder resolves symbolic links before
choosing their icon, so aliases retain the original's file-type icon and
application aliases (including chains) use its custom 32- and 16-pixel icons.
Shared-folder aliases inherit the shared-folder icon. Alias names, paths and
italic labels remain independent; broken or looping links use a generic icon.
Focused desktop and selection tests pass, including custom application icons,
file-type icons, alias chains, shared folders and unavailable targets; the
production Finder target builds successfully.

**System Folder resource providers and read-only system views** (0.1.28):
Finder's virtual registry now includes resource-provider nodes under System
Folder (Sounds, Startup Items, Application Support, Device Drivers, Network,
System Logs, Software Components and Extensions) plus trusted launchers for
System Information and Extensions Manager. Providers are allowlisted by id and
lazy-loaded; no provider accepts arbitrary registry paths or commands. Alias
providers canonical-check source paths against allowed roots and suppress
broken, looping and out-of-root links. Resource entries remain virtual: they
cannot be renamed, labeled, dropped into, trashed or treated as uninstallable
applications. Protected logs stay informational only. Get Info now supports
virtual resource details (kind, location, source and access) for curated items
without a single filesystem counterpart. System Information reports installed
ZacOS version, distro, kernel, CPU, memory, storage, network, audio, loaded
drivers and uptime. Extensions Manager is intentionally read-only in this
phase, showing status/startup metadata without load/unload toggles.
Focused Finder VFS, desktop and selection suites pass in an isolated build;
one transient `finder-selection` timing failure was resolved by immediate
re-run in the same build directory.

**User applications separated from system utilities** (0.1.27): Finder
now excludes automatically installed Debian dependencies as well as the
installation-image baseline from Applications. Settings, system tools,
file managers and shipped utilities are shown in System Folder > Utilities,
including qps, PCManFM, desktop/screen-saver settings, Print Settings and
balenaEtcher, even when explicitly installed. Ordinary user utilities,
manually installed Debian applications, user-local launchers, Wine apps and
Flatpak exports remain eligible for Applications. Apt mark changes refresh
the view; desktop-file ownership supplies package identity, with cached
queries invalidated when dpkg's database changes. Registry version 9 moves
the shipped Utilities folder and system-launcher rename/label overrides
without removing user nodes. System tools and automatic dependencies cannot
be queued or removed through Finder's application Trash path, including an
already queued app subsequently reclassified as a system tool. No packages
are removed. The production Finder builds, and focused Finder routing,
desktop and selection suites pass;
installed systems receive the change through Software Update. Apt marks on
this machine confirm all four named utility packages are automatic, but
installation metadata cannot reconstruct user intent if another installer
marked a package manual.

**Trash queues application uninstall** (0.1.26): moving a supported
Debian or user Flatpak application to Trash via the menu or a drag creates
a persisted uninstall record and hides its launcher, without uninstalling
yet. Put Away or dragging back to Applications cancels removal. Empty Trash
confirms app names/sources and shows progress, then uses the existing
software helper or user Flatpak uninstall, retaining personal data. Failures
keep pending entries for retry/restoration and do not delete ordinary Trash
contents. Unsupported or system-wide Flatpak apps report an explicit error.
Application aliases remain ordinary links and never uninstall their targets.
Finder builds and focused Finder/Store tests pass with mocked uninstall
commands; tests cover separate-process queue persistence, confirmation
cancellation, failed and no-op uninstalls, personal-data preservation and
ordinary Trash retention on errors. No real application was uninstalled.

**First-install Flatpak launcher discovery** (0.1.26): Finder now watches
the existing ancestors of missing application directories and reacts to GIO
application-list changes. A first Flatpak installation (such as Chaski) can
create its export directory after login and appear in Applications without a
manual refresh or reboot. Reloading also picks up newly created icon-theme
directories. Tests cover symlinked exports, automatic folder refresh,
Flatpak identity, icons, removal and export-directory replacement.
The automatic discovery regression and Store browser suite pass; a normal
refresh was requested in the live Finder for the already installed Chaski.

**Persistent local-administrator Mac mount authorization** (0.1.25):
the existing polkit mount rule now includes the constrained read-only Mac
mount/eject helper. Active local members of `sudo` no longer receive an
administrator-password prompt each time a permanently connected Mac drive
is mounted after reboot. No password is stored; non-admin, remote and
inactive sessions still use the policy's authentication defaults. Disk erase,
encrypted unlock and other-user mount actions are not granted by this rule.
The JavaScript authorization regression passes all 64 action/session/group
combinations; all three focused authorization and disk-helper suites pass.
The rule is installed through the existing package path; reboot behavior on
the connected physical drive still needs confirmation after installing it.

**Happy Zac and "Welcome to Zacintosh."** (uncommitted): the boot logo in
GRUB, the splash and the compositor is now Happy Zac
(`assets/boot/happy-zac.png`). The Welcome box copies the 1984 Macintosh's
welcome box (measured from a screenshot kept out of the repo): white box,
1 px black line, 2 px solid shadow, the ZacOS logo (`assets/boot/zacos-logo.png`)
at its left and "Welcome to Zacintosh." centred. The picture and progress
bar inside the box are gone. Everything uses one whole-number scale of the
1984 512x342 screen (`pl_welcome_scale`, 1–6; 3x at 1080p), so GRUB, the
splash and the desktop line up. Per-scale art: `assets/boot/*-Nx.png`
(from `tools/boot/make-boot-art.py`) and the splash's `welcome-Nx.png` (from
`boot/make-splash.c`). `boot/logo-64.rgb` and `boot/plymouth/logo.png` are
removed. Tests updated: `lib/tests/test_welcome.c`, `compositor/tests/test_startup.c`,
`boot/tests/test_splash.py`, `tests/vm/boot-frames.py`. Still to do: a full
meson build (needs the -dev packages) and a look on real hardware.

**VS Code's native title bar set automatically** (uncommitted): other ZacOS
computers running VS Code didn't get its global menu: the setting
`"window.titleBarStyle": "native"` had only been set by hand on this one.
The Electron launch path (`pl_electron_command`) now adds it to VS Code's
(and VSCodium's, Code - OSS's, Cursor's) settings.json when no title bar
style has been chosen there; a chosen one is kept (`lib/electron.c`).
`electron` test extended; checked against this computer's VS Code (already
set: untouched) and ChatGPT (not VS Code: ignored).

**Network Browser: servers announced over IPv6 alone, UTF-8 names**
(uncommitted): two ZacOS computers dropped out of the list after a first
connection: their avahi announced AFP over IPv6 only for a while, and
`discovery.cpp` kept IPv4 records only. Such servers are now listed by their
`.local` host name (IPv4 still preferred, no duplicates;
`parseAvahiBrowse`). Names with escaped UTF-8 ("Adam’s Computer") were
decoded a byte at a time ("Adamâs"); now as UTF-8. `discovery` test
extended; checked against this network's live avahi output.

**Network disk names: the shared folder's, never numbered** (uncommitted):
reconnecting an AFP server piled up "adam's home (2)", "(3)", "(4)" -
leftover empty mount folders (a logout skips the unmount cleanup) made each
new mount take the next number. AFP volumes now mount at
`mounts/<server>/<name>`, always the same place; leftovers are removed and a
dead mount lazily unmounted first; an already-mounted volume isn't mounted
again (`netvolumes.cpp`, `netbrowser.cpp`). The name is the folder's, as on
Mac OS 9 (duplicates side by side): Netatalk's "NAME's home" shows as NAME
when logged in as NAME, and our File Sharing sets `home name = $u`.
`netvolumes` test added; `afp-disconnect` and `discovery` pass. Not yet
tried live against a server.

**Apple menu names and icon** (uncommitted): the catalog (Software window) is now
**New Tricks** and Software Update is **Fetch Updates** (`menubar/launch.c`);
the windows keep their names. The Apple-menu icon is now a 16x16 close-up of Happy Zac's head
(`logo16_data` in `lib/logo.c`, from `tools/menubar/make-menu-logo.py`); the
64x64 ZacOS logo used by Get Info, setup and the installer is unchanged.
Seen in a nested session.

**GIMP 3's menus in the menu bar** (uncommitted): GIMP's image window is a
`GtkApplicationWindow` with its own menu bar widget, which the GTK module
skipped (it left all application windows to GTK, which exports nothing
without a menu model). The module now exports such windows, giving its
properties right after GTK's realize handler (GDK sends them once, at first
show) and opening the bus synchronously if needed (`shell/gtkmenu/module.c`).
`gtk-menu-protocol` has a GIMP-shaped fixture (`--app-window`); nested check
with GIMP 3.0.4: menus shown, own bar hidden, File > New... works. Installed
live with `scripts/install-live.sh`; the user confirmed GIMP's menus work.

**Global menus for Electron apps, sharp X11 on HiDPI** (uncommitted): VS Code
and other Electron apps put their menus in the menu bar. Electron exports
menus only under X11 and needs `libdbusmenu-glib4` (now a package
dependency), so the Finder, Login Items and the Apple menu start Electron
apps with `--ozone-platform=x11 --force-device-scale-factor=N`
(`lib/electron.c`). zacos9-wm shows such X11 windows at full resolution: a
per-window X11 scale in `xwayland.c`, and Xwayland gets outputs in pixels
from its own xdg-output manager (`xwayland_output.c`); other X11 windows are
unchanged. VS Code also needs `"window.titleBarStyle": "native"`. Checked:
live VS Code under X11 exported its menus to the running menu bar; nested 2x
session: VS Code sharp at normal size, clicks and pointer correct, xmessage
unchanged; `electron`, `autostart`, launch-feedback, `gtk-appmenu`,
`appstyle` and `startup-displays` tests pass. Installed live with
`scripts/install-live.sh`: VS Code's menu titles appeared but the menus were
empty - Electron fills a submenu only on dbusmenu `AboutToShow`, which the
menu bar now sends once per empty menu and submenu (`appmenu.c`), then
refetches. Nested check: File lists VS Code's items and New Text File opens
Untitled-1. Installed live; VS Code exports its full menus there. The
ChatGPT desktop app can't be covered (its runtime has no global-menu
exporter; see docs/app-integration.md). Seen once, not fixed: a new
X11 window took focus but stayed behind the window in front.
See docs/app-integration.md.

**GRUB picture follows upgrades** (uncommitted): installed systems kept
the old ZacOS logo in GRUB (the first white screen) after upgrading to
Happy Zac, because `zacos9-install` draws `/boot/grub/zacos9-logo.png` only
once. `debian/zacos9.postinst` now runs `zacos9-bootlogo --redraw` on it
(same size, same file name, so no `update-grub`). Checked: an 0.1.44-era
800x600 picture redraws identical to a fresh one; a non-PNG is refused.
Still to check: an upgrade on a real installed machine.

**Real extension parade at startup, with Welcome on the boot splash**
(0.1.32, 2026-10-07): the icons along the bottom of the startup screen
used to be six fixed pictures shown on a timer after logging in. Now they
are the curated extensions (`lib/extensions.c`, shared with Extensions
Manager) appearing as their kernel modules actually load.
- The boot splash shows "Welcome to ZacOS 9" half a second in, drawn from
  the compositor's own code at build time (`boot/make-splash.c`). It
  matches pixel for pixel; bar fills 4–218 were checked against
  `lib/welcome.c`.
- `zacos9-parade` sends each icon to the splash as its module loads.
- The splash hands over to the compositor without the white gap
  (`zacos9-boot-handoff`: deactivate, then `quit --retain-splash` once the
  wm has the display). There are fallbacks so the text login is never hidden.
- See `docs/boot.md`. Tests added: `boot-parade`, `boot-handoff`,
  `boot-splash-welcome`, and a handoff phase in `startup-displays`.
- Still to confirm on real hardware after a reboot: that the handover
  shows no white flash on i915. greetd may switch the console to text
  mode as it starts, and only a real boot shows that.

**Sniffer/System Folder refinements** (unreleased): Startup Items now follow the
FreeDesktop autostart spec (`autostart.cpp`); aliases keep an inode record and
reconnect when targets are renamed/moved, with Fix Alias… and loop detection
(`alias.cpp`); Extensions Manager gains Login Items and Background Services
views with confirmed, user-level-only Turn On/Off and Start/Stop
(`userservices.cpp`); Get Info explains virtual vs. physical items and shows an
alias's original; virtual folders refresh when autostart/user-unit folders
change. Tests: `autostart`, `alias`, `userservices`, `finder-vfs`. Not done:
watching alias targets, live service state events, UI-level tests of the new
Extensions views, cross-disk alias reconnection.

**Extensions Manager: friendly names, icons and a curated list**
(0.1.32, 2026-10-07): instead of 145 raw kernel module names, Extensions
Manager and System Folder > Extensions now show about 45 curated entries
(only the ones loaded on this computer), for example "HDMI Sound", "Bluetooth",
"Keyboard & Mouse", "Linux Disks (ext4)". Each entry has a new Platinum icon
(11 new icons: puzzle piece, display, Ethernet, Wi-Fi, USB, keyboard, camera,
power, sensor, padlock, chip), a description and the modules it covers.
Helper libraries are hidden; **Show All** lists every module with an icon.
The window stays read-only. `extension-catalog` test added; full suite passes.

**Software no longer stuck loading Flathub** (0.1.31, 2026-10-07): opening
Software sat on the Featured page with the busy sweep running on every
machine. The Flathub AppStream download takes about 80 seconds here, but the
catalog helper killed it at 60, so it never finished and every visit started
over. The refresh now runs detached and always completes; a cached catalog
is read at once (about 9 s for 3,376 apps) and refreshed in the background;
a first download that is still running reports "still downloading". Featured,
which only shows curated Debian apps, no longer loads Flathub at all.
Catalog (31) and store-browser tests pass.

**Flathub metadata and combined category browsing** (0.1.25): Software
reads the enabled user remote's AppStream metadata for application names,
summaries, cached icons and categories, instead of relying on the sparse
remote listing. Flathub applications join the main categories and All
Applications through a remembered "Include Flathub applications" checkbox,
checked by default; hiding them does not disable the remote or remove apps.
The dedicated Flathub view remains available. Refresh replaces categorized
results without duplicates and preserves the prior catalog on errors.
Runtime dependencies now explicitly include Flatpak and AppStream bindings.
Software builds and all three focused Store/catalog suites pass, including
26 Python catalog tests and offscreen checkbox persistence, automatic loading,
category/search filtering, pre-install icons and refresh/error regressions.
The real official remote yields 3,371 applications across all six main
categories, with 3,356 summaries and 3,296 cached icons; missing published
metadata uses the existing fallbacks.

**Flathub remote validation correction** (0.1.24): Flatpak 1.16 omits
the trailing options column when empty. The Store incorrectly treated the
official two-column remote row as an unofficial source, and the catalog
helper rejected it as malformed. Both parsers now accept an omitted empty
options column while retaining exact official URL checks and disabled-remote
handling. Regression tests cover both output forms and reject malformed
rows and unofficial URLs.
Real Flathub catalog rows also omit empty descriptions; those now load with
an empty blurb rather than aborting the entire catalog.
Software builds and all three focused Store/catalog suites pass. The actual
configured official user remote loads 3,371 application entries successfully.

**Relevant APFS volumes and real disk names** (0.1.23): enumerate stored
APFS names through libfsapfs, exclude standard helper names (Preboot, Recovery,
VM, Update, xART, iSCPreboot, Hardware and Diagnostics), and mount remaining
data/system volumes with their original numeric indexes. Finder shows each
normal volume by its stored name, such as Chromasystem SSD, and opens its
contents directly; eject still unmounts the complete container. Filtering is
exact-name based because the Debian reader lacks a public role API; renamed
helper volumes cannot be identified automatically. Finder builds; 30 Mac-disk
tests pass, including filtering, name enumeration and rollback regressions.
All four focused Meson suites pass; the hotplug suite also checks the exact
desktop names, browse paths and eject identity for a system/data volume pair.

**APFS busy-directory correction** (0.1.22): the reported volume 4 is
present in the kernel mount table but `os.path.ismount()` returns false.
The 0.1.21 helper treated it as unmounted and tried removing its directory,
masking the mount failure with EBUSY. Mount detection and image-cache checks
now use `/proc/self/mountinfo`. Rollback checks every attempted volume,
including the failed reader's mount, and preserves the original error if
cleanup fails. Eject discovers orphan child mounts even without a record;
leftover mounts receive recovery records so Sniffer can offer eject.
Verified the detection fix against the actual stranded volume 4 without
modifying it. All four focused suites pass, including 27 Mac-disk tests;
mounting and browsing the affected volume still need verification after install.

**APFS boot mount correction** (0.1.21): the 0.1.20 helper passed `-f all`
to fsapfsmount, whose numeric-only parser rejects it despite advertising the
option. Multi-volume APFS containers then failed with an invalid volume index.
The helper now enumerates containers through libfsapfs and mounts each volume
by its one-based number. Desktop discovery recognizes the child mounts, eject
unmounts all children, image cleanup tracks those actual mountpoints, and failed
mounts roll back prior volumes. Regression tests cover numeric selection,
multi-volume mount/eject and rollback, including the installed reader's parser.
Finder builds and all four focused suites pass (22 Mac-disk tests). Actual
APFS mounting on the reported drive still requires verification after installation.

**Read-only Mac disk mounting** (0.1.20): physical HFS/HFS+/APFS volumes
and Mac disk images mount through a constrained block-device helper, with
polkit authentication, read-only source descriptors and ro/nodev/nosuid/noexec
mounts. Sniffer image double-click mounts instead of launching Classic;
the Classic menu still opens selected raw images. Raw APM/GPT, Disk Copy 4.2,
UDIF DMG, sparseimage and sparsebundle containers are supported. Desktop
icons include APFS FUSE mounts; eject removes image loops and converted
cache files after their final partition is unmounted. Errors remain visible,
and APFS is excluded from the erase/initialization helper. See
[Mac disks](docs/mac-disks.md) for backend limitations. Privileged mounting
on real hardware remains to be verified; no physical disks were modified
during automated tests. Finder builds; six focused Meson suites pass, including
17 Mac-mount helper tests. A synthetic sparse image was also read with Debian's
actual libmodi backend, verifying the converted data and untouched source.

**Classic sound set import** (uncommitted): `wmov` drag and release sounds
now come from the `snd#` slots classic sets actually use (slot 1 and slot 3),
and are not rejected when they share IDs with other drag codes. That is why
window dragging was silent. μ-law, IMA4, `twos` and uncompressed CmpSoundHeader
samples decode; leading null commands and low (1 kHz+) rates are accepted;
empty resources are silent instead of errors. Limits are raised to 256 sets,
30 s and 8 MiB per sample. All 75 local sets import with no errors (49 with a
drag sound, 18 with a release sound). Only MACE is still unsupported.

**GTK 3 global menus** (0.1.15): Added GTK's version-one Wayland menu
metadata protocol and per-focused-surface menu paths over platinum-shell v4,
preserving earlier clients and distinguishing windows of the same process.
The menu bar imports GTK GMenuModel/GActionGroup exports, live enabled and
checked/radio states, sections/submenus and action targets; asynchronous owner
PID verification rejects spoofed/stale exports and owner loss restores fallback.
Native GTK exports work directly. A realization-safe ZacOS GTK module reuses
Debian's appmenu parser for legacy GtkWindow menu bars: the stock module's
25.04 legacy Wayland export fails before realization and sends the wrong
action path. Session/dependency wiring installs and enables the corrected
module; legacy local menus reappear when the menu service disappears.
Isolated headless tests verify real Galculator File/Edit/Help global titles,
legacy/native GTK action clicks, same-process window focus/close, local-menu
fallback and real Qt registration/action compatibility. Backend tests cover
updates, toggles/radios, invalid paths, owner loss and PID mismatch. This is
released together in 0.1.15; the compositor/session need a coordinated
update and logout/login. GTK forced to X11 and custom/non-exported menus remain
outside this implementation.
The compositor, menu bar, Finder and GTK module build successfully. All
eight focused GTK, Qt-action, launch-feedback, Finder and native-snapshot
regression suites pass with no skips or failures.

**Sniffer copy/move status dialogs** (0.1.15): File transfers and
Duplicate now show a Platinum movable-modal dialog with current filename,
destination, progress bar, percentage and Stop. A worker thread counts and
copies folder contents in bounded chunks while the GUI remains responsive.
Stop/Escape/Command-period keep completed items, remove the current incomplete
copy and preserve originals; same-disk rename moves stop between items.
Aliases (including relative and dangling links) and resource-fork companions
are preserved. Explicit errors cover conflicts, read/write failures and
failed cleanup. Finder-originated drops release the drag grab before showing
the modal controls, and stopped/failed external drops are not reported as
completed moves. Tests cover copy interruption during actual file I/O,
completed-item retention, move status, keyboard Stop, recursive alias copies,
read-error rollback including read-only copied directories, and existing
Desktop/resource-fork behavior. Movable-modal windows now occupy a compositor
layer above ordinary app windows, keeping their Platinum frames visible.
Content-inset correction (0.1.17): xdg scene trees already compensate for
window-geometry offsets; the compositor no longer subtracts them a second time.
App content now starts below the classic title bar at the existing frame margins,
including movable-modal dialogs and transitions out of fullscreen.
The compositor builds; content-inset and fullscreen-protocol regression tests
pass. Live verification in the installed session is still pending.
Default window size: new main windows from other programs (xdg-shell and
XWayland) open nearly full-screen, inset 96 px from each side and 32 px from
the menu bar and screen bottom so desktop icons stay visible. Finder windows,
dialogs, fixed-size windows and fullscreen requests are unchanged. The
`default-window-size` test covers this, and the full Meson suite passes;
live-session verification is pending.
Installed-session drag/dialog interaction
still needs interactive verification after updating the Finder. Sniffer
builds successfully; all three focused selection, Desktop and VFS suites
pass, including pixel checks of the Platinum dialog and progress fill.

**Classic startup on installed hardware**: The user's Power Mac 7300–9600
Old World ROM, Mac OS 9.0 CD image and 2 GB hard-disk image were recognized,
but installed SheepShaver exited before opening a window with
`Cannot map Low Memory Globals: Operation not permitted`. With explicit
administrator authorization, `cap_sys_rawio=ep` was applied only to the
root-owned installed SheepShaver binary. Global `vm.mmap_min_addr` remains
65536. Relaunch created an 800×600 emulator window, and the user confirmed
the Mac OS desktop or installer appears. This is a local capability change,
not an automatic package permission grant; replacement of the binary during
an emulator upgrade may remove it. The permission's risks and removal are
documented in `emulation/README.md`.
Packaged (next release): `zacos9`'s postinst grants the same capability to
`/usr/libexec/zacos9/SheepShaver` on install and upgrade (Software Update
carries it). A dpkg file trigger reapplies it when `zacos9-emulators`
replaces the binary, and the emulator package's own postinst grants it too.
Both packages depend on `libcap2-bin`. A setcap failure only warns.

**Desktop-first save locations** (0.1.15): Finder continues to show
the real XDG Desktop directory while retaining startup/mounted disks and
Trash; locally mounted USB volumes use the USB icon overlaid on the drive
icon. Login registers Desktop and a private, per-user Zacintosh HD document
workspace without replacing existing paths or bookmarks. The workspace's
System Folder and Applications are ordinary private document directories,
not the real system/application paths. Standard GTK and Qt save dialogs
prefer Desktop instead of their generic Home/current-directory default; Qt
adds mounted filesystems to its sidebar, and GTK/portal places include the
private workspace and GIO mounts. Virtual application and backed-folder drags make
private workspace and GIO mounts. Virtual application and backed-folder drags
make real Desktop aliases without moving originals; Command-Option-drag makes
aliases of ordinary files/folders. Alias copies preserve links, unavailable
originals give an alert, and transfers resolve destination aliases before
checking for recursive folder copies. Offscreen tests cover external saves/
watcher updates, New Folder, actual application and backed-folder drag
payloads, alias opening/copying/trashing, and saving through a Desktop folder
alias in a real Qt file chooser. Registration tests cover custom paths,
existing bookmarks, repeated login, invalid paths and symlinked configuration
files. Finder and the Qt style build; all focused Desktop, GTK, selection and
VFS suites pass. A real GTK chooser verifies Desktop and Zacintosh HD places
and private workspace saves. Installed-session
portal routing still requires verification after updating the package and
logging in again.

**Native Screen Snapshot replacement** (0.1.15): Following a repeatable
report of YouTube playback failing after `grim` captures, Snapshot now uses a
native private Wayland interface to read the last committed display buffer.
Unlike screencopy, it does not change output-render or software-cursor locks.
The compositor retains one current displayed buffer per output, releases it
on replacement/reconfiguration/removal, and copies requested images into sealed
memory files. The native client validates dimensions, imposes a 32-megapixel
limit, combines monitor images at the selected scale, crops and atomically
saves PNGs. Rectangle selection, Desktop naming and clipboard behavior remain.
There is no automatic grim or unverified GNOME fallback. Old compositors give a
restart/update alert. Tests verify unchanged output locks and buffer lifetime,
repeated multi-display readback while animations continue on pixman and GLES,
image colors/crops, malformed geometry, save/clipboard failures and cancellation.
HiDPI captures retain pixel resolution, and all eight output rotation/mirror
transforms are covered by exact pixel assertions. The compositor and native
client build successfully; all seven focused snapshot, startup, launch, window
sound and Finder selection suites pass.
The affected Firefox/YouTube reproduction still needs installed-system testing;
the replacement does not establish the root cause of the earlier HTTP 403s.

**Original window-sound live check**: ZacOS Original is available in installed
0.1.14 and already maps drag, release, collapse and expansion events. Enabling
the per-user sound set exposed stalled playback on the developer machine's
HDMI sink: bounded pw-play/paplay previews do not finish, while an original
chirp plays successfully on analog without changing the HDMI default. Resetting
the HDMI sink and restarting user audio services did not recover it; the
existing Haswell position_fix=1 is active. Window-sound, playback and settings
regression suites pass. On installed 0.1.15, YouTube resumed after moving its
stream to analog while HDMI hardware pointers remained frozen. A local HDMI-only
48 kHz/1024-frame period/8192-frame headroom/no-suspend rule still froze when a
second stream joined. Adding interrupt scheduling and a fixed 1024-frame quantum
passed a sustained-stream plus twelve-short-sounds test, but failed the subsequent
live window-sound test: TV audio was silent, hardware progress stopped, and a
woodblock player remained stuck. The unsuccessful local rule was removed and
user audio services restarted; interface sounds remain temporarily disabled at
the user's request, with volume 5 preserved. No HDMI fix is established or
shipped; recovery, audible concurrent playback, and a bounded sound-player
lifetime remain unresolved.
Later checks in that boot, with interface sounds off, saw PipeWire HDMI
streams freeze within seconds of starting, and unbinding and rebinding the
HDMI controller did not recover it; a reboot did. The kernel log's only
audio event was "IRQ timing workaround is activated for card #0" during the
interrupt-scheduled testing, with no i915 FIFO underrun, flip timeout or
codec timeout. Two test tools from that session were unsound and their
results are void: a raw ALSA client wrote 16-bit samples, and a probe
misparsed the padded keys in `/proc/asound/.../status`. After the reboot,
16-bit streams opened directly on the HDMI device stall every time while
32-bit streams play; PipeWire always drives the HDMI sink in S32LE, so this
quirk is not reached in normal use. On healthy HDMI, five minutes of the
drag loop's pattern (clips respawned, SIGKILLed on release, end sounds)
beside a sustained stream, and 57 drag bursts waking an idle sink, nine from
a runtime-suspended controller, all played without a stall. On the analog
sink a clip joining or being killed left the device's trigger time and
hw_params unchanged. The sound pattern alone does not reproduce the hang;
a live test with YouTube, real window drags and interface sounds on, under
a read-only hardware-position monitor, is next.

**Recurring HDMI stall after updating to 0.1.26** (2026-10-06): the
installed Haswell position fix was active and the ACPI storming GPE was
masked, but HDMI's ALSA hardware and application pointers stopped advancing
while Brave and a short imported button-click player remained active.
Terminating that stuck player did not restore hardware progress. Apt history
shows the update installed only ZacOS 9, not an audio-server, browser or kernel
update; this does not establish what triggered the stall. After an authorized
reboot, the user confirmed YouTube video and HDMI/TV sound worked again.
Three bounded original chirps and twelve bounded copies of the previously
stuck button-click sample completed successfully, the latter alongside Brave
playback with advancing hardware pointers. Reopening Software Update, checking
for updates and moving its window also left playback working, confirmed by
the user. HDMI selection and interface-sound settings were unchanged. No
preventative fix was established in that boot; the recurring stall and
unbounded detached sound-player lifetime remained unresolved.

**HDMI freeze recovery without a restart** (0.1.29, 2026-10-07): a
once-a-second monitor caught two HDMI freezes on the iMac14,1 during
Firefox/YouTube, 90 seconds apart. Each time, PCM state and sink stayed
RUNNING with `hw_ptr` frozen, while the TV's ELD, the connector and the
controller's runtime power stayed good. HDA power saving had been turned
off at run time (`power_save=0`, `power/control=on`), so runtime suspend is
ruled out; the power-saving modprobe change tried earlier that day was
dropped unshipped. Suspending and resuming the HDMI sink (`pactl
suspend-sink`) restored TV sound both times, confirmed by the user, with
no reboot. Switching outputs failed because a stuck interface-sound
`pw-play` kept the frozen device open. New `zacos9-hdmi-watchdog` user
service: on Haswell HDMI controllers only, it restarts the controller's
PipeWire sinks after 3 s without hardware progress; it caught and recovered
the second freeze live. `hardware-quirks` covers it (17 tests). Ruled out
since: the IOMMU. Kernel logs (exported by the user) had no audio or
graphics errors at the freezes. With the HDMI controller's IOMMU group
switched live from `DMA-FQ` to `identity` at 15:09 (`position_fix=1` kept),
it ran 20 minutes clean and then froze again at 15:28:56; the watchdog
recovered it. An `identity` change briefly added to `zacos9-hda-load` was
reverted unshipped. The root cause of the freeze is still unknown, and so
is the unbounded detached sound-player lifetime.

**USB erase safety-lock fix and disk names** (0.1.14): The helper's own
exclusive disk claim prevented sfdisk from taking another exclusive claim,
producing an erroneous "disk is currently in use" failure on unmounted disks.
sfdisk now leaves the preflight claim and kernel refresh to the helper: the
helper retains its exclusive descriptor, verifies the written table, fsyncs
and performs BLKRRPART through that same descriptor. No force option is used;
refresh failure stops before mkfs and reports that the partition table changed.
Both Erase Disk and unreadable-disk Initialize request a FAT32 label before
confirmation, defaulting to Untitled. Cancel causes no unmount or erase. Names
are validated by both GUI and privileged helper and passed to mkfs as one
argument. Raw blkid verification preserves spaces instead of comparing escaped
export labels. Physical disk/privileged-loop verification remains pending.
Finder and menu bar build, and all five focused suites pass. The helper suite
has 71 tests, including actual mkfs/blkid round-trips on disposable regular-file
images (no physical storage), exclusive-descriptor refresh and busy-refresh
failure tests. Offscreen dialog tests cover editing, invalid names and Cancel;
controller tests cover label propagation and cancellation in both entry points.

**Special > Erase Disk** (0.1.13): Select one safe mounted USB disk icon
to enable Erase Disk. A default-Cancel confirmation identifies the physical disk
and warns that every partition will be erased. Administrator authorization,
identity revalidation and non-forced unmounting precede the existing verified
FAT32/MBR formatter. Finder inhibits automount during the operation, then
remounts Untitled. Internal/system, encrypted, read-only and unsafe mount
topologies remain excluded; automatic unreadable-disk initialization still
rejects mounted disks. Hardware-free menu, controller and hot-plug regression
tests cover gating, cancellation, replacement and automount inhibition. No
physical disk was erased; interactive hardware verification is pending.
Finder and menu bar build successfully. All five focused suites pass, including
66 mock-only helper tests and invalid-success/authorization-error controller
checks. The menu-state parser remains compatible with older Finder messages.

**USB hot-plug discovery fixes** (0.1.12): Finder now services GLib/GIO
events with a bounded Qt timer, handles volume-changed when an inserted drive
becomes mountable after volume-added, and registers listeners before startup
enumeration. Mounted storage under /run/media is visible rather than filtered
as runtime storage. Disk-initialization failure reporting starts before desktop
automount, and completed mount requests refresh disk icons. Hardware-free
hot-plug tests pass with Qt GLib integration disabled, covering late readiness,
reinsertion, failure reporting and path filtering. Finder builds and disk-dialog
and selection regression tests pass. Physical hot-plug verification remains
pending on the installed update.

**Unreadable USB disk initialization** (0.1.11): Sniffer checks inserted
USB storage and mount failures and offers a Platinum Eject/Ignore/Initialize
alert. Initialization requires a second explicit whole-disk erase confirmation
with Cancel as default, followed by administrator authorization. A constrained
helper creates one FAT32 partition labeled Untitled, rejecting unsafe storage
and verifying device identity again before destructive commands. Mounted,
system, encrypted and read-only devices are not initialization candidates.
Physical USB and formatting validation are pending; no real disk is erased
during automated tests.
Finder builds successfully. The 40 mocked helper tests, offscreen dialog
controller tests and Finder selection regression suite pass. Helper tests cover
mounted/system-device topology, swap, protected signatures, identity changes,
exclusive opens and verified FAT32 command sequencing. Read-only discovery was
checked; physical USB formatting verification remains pending.

**HDMI audio on Haswell Macs** (0.1.8): HDMI/DisplayPort sound stuttered
on the iMac14,1 while analog played cleanly. The Haswell HDMI controller's
position buffer runs ahead of the sound sent: its pointer leapt 500-2,400
frames in 1-2 ms and overtook PipeWire about 14 times a second ("Unstable
LPIB" in the kernel log). PipeWire buffering and interrupt scheduling still
underran. A modprobe hook (`hardware/zacos9-hda-load`) loads `snd_hda_intel`
with `position_fix=1` (LPIB) for Haswell HDMI controllers only, as upstream
already does for Broadwell. `zacos9-gpe-guard.timer` masks ACPI GPEs firing
over 1,000/s on Macs: GPE 0x06, the graphics SCI, fired ~24,000/s from boot
and kept `irq/9-acpi` on most of a CPU. `hardware-quirks` covers both against
a fake /sys. Confirmed on the iMac: HDMI playback is clean after a restart.

**Appearance presets and sound themes** (uncommitted): Appearance adds Themes
and Sound Sets tabs with saved preset combinations, None/original/custom sound
sets, preview and independent interface volume. Registry version 8 adds Themes
and Sound Themes under Appearance. Interface events are wired into ZacOS
buttons/checkboxes, menus, Sniffer windows and successful Trash operations.
Classic PCM sound resources and WAV manifests import without modifying sources;
full classic window/control skins are intentionally unsupported for this phase.
The local compatibility archive is excluded from commits/package builds.
Full build and nine focused suites pass. The local sample archive validates
496 entries and 374 resource forks; all 75 sound-set metadata maps were checked,
with 399 supported PCM events decoded in private verification. Unsupported
compressed samples and command sequences are reported. UI rendering and
playback volume/overlap are covered by offscreen tests. Not installed/published.
StuffIt extraction creates visible companions for resource-only files; Sniffer
preserves their AppleDouble forks during individual moves and copies. The sound
catalog also accepts a copied Sounds folder with one level of classic sets.

**Window interaction sounds** (0.1.10): sound sets now accept
window-collapse, window-expand, window-drag and optional window-drag-end.
The compositor emits collapse/expand on actual state changes and starts
repeating drag playback only after movement. Release stops playback; Escape,
window unmap, fullscreen transitions and session shutdown cancel it. Classic
PCM events wcol, wexp and wmov are imported; drag repeats the complete sample,
not classic interactive variants or embedded loop regions. No Apple audio is
bundled. The release also requires the StuffIt decoder at runtime.
Compositor and Appearance builds pass; focused state-machine, mock playback,
classic importer, offscreen theme-selection and settings suites pass. Physical
audio and interactive desktop verification are still pending.

**Expanded optional Software catalogs** (uncommitted): Featured stays the
default; searchable All Applications uses Debian AppStream desktop metadata,
and optional Flathub is enabled explicitly for the current user through
Additional Sources. Flatpak app installs/removals are user-scoped, and disabling
the source retains apps/data. Metadata discovery is asynchronous with timeout
and explicit errors; arbitrary APT sources are excluded. Session data paths
include Flatpak exports for Sniffer/menu launchers. Mocked catalog and offscreen
browser tests exercise discovery, filtering, confirmation and command routing;
no sources or applications have been added to the running system.
The full build and five focused suites pass, including the pending wallpaper
folder migration. Not installed or published yet.

**Wallpaper folder placement** (uncommitted): Wallpaper now lives inside
System Folder > Appearance, backed by `zacos9/appearance/Wallpaper`.
Registry version 7 reparents the old node and preserves overrides; existing
wallpaper storage is moved without changing image IDs. Failed moves are logged
and retain the old storage rather than losing or overwriting user files.
Sniffer and Appearance build successfully; the catalog, VFS and selection
test suites pass, including legacy file moves and registry override preservation.
Not yet installed or published.

**Sniffer multiple selection** (0.1.7): desktop, icon and list views
support Mac OS-style dotted selection rectangles, Shift-click and Shift-drag
toggling, and Edit > Select All / Command-A. List rectangles start in blank
columns, select rows by icon/name and autoscroll at window edges. Existing
multi-item drags move or Option-copy the whole selection. `finder-selection`
uses real offscreen mouse/key events to cover icon and list rectangles,
toggling, Select All, autoscroll and a three-file drop into another folder.
Desktop rectangle code shares the tested hit-area/painting helpers; physical
desktop validation is pending.

**Desktop wallpaper mode** (0.1.6): a separate System Folder > Appearance > Wallpaper
folder and Appearance Wallpaper tab allow user photos and classic PICT pictures.
Choosing a pattern or photo switches background mode while retaining both
selections. Fit, Fill, Stretch and Center render independently on each monitor.
An asynchronous photo catalog shares the existing importer, with photo-sized
limits and cached per-monitor renderings invalidated on file replacement.
Focused tests cover placement extents, central cropping versus stretching,
landscape/portrait monitor geometry, selection persistence, mode switching,
replacement cache invalidation and classic PICT imports. Installation on the
physical desktop is pending.

**User-created desktop patterns** (0.1.6): System Folder > Appearance >
Desktop Patterns maps to the user's XDG data directory. The Appearance Desktop
tab, main desktop and secondary monitors share an asynchronous pattern catalog
that discovers additions, replacements and removals every two seconds.
Normal image tiles and classic Mac ppat/PAT/PAT#/ppt#/PICT resources are read
from raw forks, MacBinary, AppleDouble and checksum-validated BinHex; standalone
PICT uses the packaged ImageMagick converter. Original files are unchanged,
selection IDs are stable, built-in patterns remain available, and errors are
shown in the panel tooltip and logged. Unsupported pattern packing/layouts
are explicitly rejected. Synthetic tests contain only original artwork.
Focused tests pass for format decoding, palette/direct pixels, BinHex checksums,
file discovery/replacement/removal, stable selection persistence and an exact
112x112 custom-tile preview. The writable System Folder path and existing
zip/7z/tar extraction tests pass. Physical desktop installation is pending.

**Animated dog busy pointer** (0.1.6): original monochrome cursor artwork
wags its tail and backflips for standard wait/progress requests, including
Wayland cursor-shape requests and themed Wayland/Xwayland cursors. Sniffer's
desktop entries, built-in launchers, document opening and application menu
send launch feedback through platinum-shell-v1 version 3. Matching app IDs,
launch PIDs or their descendants end feedback when a window maps/activates;
failure, launcher disconnection and a 15-second timeout prevent stuck feedback.
The normal client cursor is preserved during launch feedback, and pointer input
continues normally. Document launches use the registered GIO handler so its
identity is tracked instead of an already-exited xdg-open wrapper. Classic's
pre-launch check is asynchronous so it does not freeze pointer feedback.
Xwayland also publishes cursor X resources for Qt's XCB theme lookup.
Custom application-drawn busy cursors cannot be identified.
Focused cursor artwork, launch lifecycle and isolated Wayland/Xwayland pixel
checks pass, including complete animation cycles, wait/progress aliases,
normal/text cursor restoration and the exact 15-second timeout. The affected
desktop components build successfully; physical desktop validation is pending.

**Sniffer naming** (0.1.5): user-visible Finder branding is now Sniffer,
including the application menu and Hide command, Qt application name, folder
desktop entry, login-session description and Windows installer guidance.
Executable names, application IDs, D-Bus interfaces and source paths remain
unchanged for compatibility.

**Application fullscreen** (0.1.5): the compositor now honors native
Wayland, Xwayland and foreign-toplevel fullscreen requests instead of ignoring
them. Fullscreen removes Platinum decorations, fills one output including the
menu-bar area, and restores the previous frame position/size and zoom state on
exit. Active fullscreen windows sit above the menu bar; focusing a regular
window returns fullscreen content to the normal view layer. Output changes
refresh fullscreen geometry and disconnected outputs fall back to a remaining
display. `fullscreen-protocols` exercises fullscreen enter/exit with actual Qt
Wayland and Xwayland clients in an isolated dual-output headless compositor.
Captured native Wayland frames confirm content fills every pixel of the selected
1280x720 output and restores its exact previous rendered position and size.
Firefox/YouTube on the physical desktop has not yet been retested after restart.

**Volume sound feedback** (0.1.5): successful system-volume adjustments
preview the selected alert sound via `pl_beep()`, once on drag release or after
a keyboard change. Playback waits for the audio-server command to succeed;
failed commands do not produce success feedback. Existing mute, alert-volume
and None-sound behavior is retained.

**Multi-monitor boot/startup artwork** (0.1.4): Plymouth renders a separate
centered logo, progress bar and password prompt in each display viewport.
The compositor no longer stretches one startup buffer across the combined
desktop: each output has its own centered logo/Welcome box, visible only during
that output's frame commit, including overlapping mirrored layouts. Hot-plug,
different resolutions, negative layout positions and HiDPI are covered by
`startup-displays`; `boot-splash-displays` executes the theme in Plymouth's real
interpreter with display mocks. Actual isolated dual-headless compositor frames
were captured and checked for centered logos. Packages activate Debian's
`update-initramfs` trigger so theme upgrades reach the next boot. GRUB's earlier
display behavior remains firmware-dependent. No live dual-monitor reboot tested.

**Sound output selection** (0.1.3): the Sound control panel has a Platinum
output menu for built-in speakers, available HDMI/DisplayPort monitor outputs,
headphones and other audio-server outputs, including Bluetooth. Names include
the monitor name when provided. Selection sets the default sink/port and moves
existing playback streams; volume and mute track the active output. Asynchronous
`pactl` calls have timeouts and visible errors; the output list refreshes every two
seconds for hot-plug and server fallback. Focused `sound-output` tests cover
discovery, routing, ports, volume/mute, disconnect/reconnect and error paths.
Read-only live discovery confirmed built-in speakers and the connected SONY TV.

Resuming the work on other applications' look (themes, styles, global menus): see `docs/app-integration.md`.

**balenaEtcher preinstalled, in Applications > Utilities; a polkit password prompt** (package built and inspected; Utilities
covered by `test-finder-vfs`; not yet installed or run - needs root and a new ISO).

- `packaging/etcher/build-deb.sh` -> `balena-etcher_2.1.7_amd64.deb` from balena's Linux zip (SHA-256 pinned to GitHub's digest;
  the user's ~/Downloads copy matched), in /usr/lib/balena-etcher (Debian's AppArmor profile path), setuid chrome-sandbox, icon
  from balena's repo, desktop entry `Categories=Utility;X-ZacOS9-Utility;`. ISO installs it; installer Update carries it;
  `zacos9` Recommends it.
- Applications > Utilities (`applications/utilities`, registry v4; v3 registries gain it and Applications' exclusion on upgrade)
  shows X-ZacOS9-Utility entries, even ones the system came with (`appdb.cpp` exempts them from base-applications).
- Etcher writes disks as root through `pkexec /bin/bash -c ...`: no safe polkit rule can allow that without a password, so
  `zacos9` now depends on `lxqt-policykit` and the session starts `lxqt-policykit-agent` (a Qt 6 password dialog, so Platinum).
- Not done: Software Update only fetches the `zacos9` .deb, so existing systems get Etcher from a new ISO's Update or by hand.

**Double-clicking an archive expands it in place** (built; checked in a nested session; 13 checks in `meson test expand`).

- StuffIt Expander's way: a double-clicked .zip / .7z / .sit / .tar / .tar.gz / .tgz / .tar.bz2 / .tar.xz is expanded beside itself by
  `zacos9-expand` (`shell/finder/`, Python's zipfile/tarfile and python3-libarchive-c for 7z). One top-level item lands as itself, several go in
  a folder named after the archive; taken names get " 2"; the archive is kept; the result is selected. A shared status dialog appears while
  any supported archive is expanding. Entries escaping the folder
  are refused, __MACOSX/._ files skipped, zip Unix modes kept, password-protected zips refused with a message.
- Seen: Photos.zip on the desktop -> "Photos" selected beside it; Project.zip in a Downloads window -> "Project" selected.
- StuffIt runtime packaging fix (0.1.10): `unar` is now a mandatory
  runtime dependency, not just a build dependency, so installed systems receive
  both `unar` and `lsar` for `.sit` double-click extraction. Resource forks are
  preserved as AppleDouble companions; existing extraction tests use generated
  StuffIt files to cover in-place output, uppercase extensions and collisions.
- 7z support (0.1.6) follows the same naming, collision avoidance,
  original-archive retention and partial-extraction cleanup. Links, special
  entries and unsafe paths are refused; encrypted or damaged archives show an
  error. Debian installs the libarchive Python reader as a runtime dependency.
  Focused extraction tests cover real 7z contents, executable permissions,
  single/multiple roots, repeated extraction, uppercase extensions, metadata
  filtering, traversal, links, empty/corrupt archives and encryption, alongside
  the existing zip/tar behavior.
- Not done: a progress window for big archives (it works in the background; the result appears when done); .rar.

**Other programs' "Show in Folder" opens the Finder** (built; checked in a nested session).

- Reported on real hardware: Firefox's folder button after a download did nothing. Nothing on the session bus was
  `org.freedesktop.FileManager1`, and nothing was the default for `inode/directory`, so its fallback failed too.
- The Finder now owns `org.freedesktop.FileManager1` (`shell/finder/filemanager1.cpp`): ShowFolders opens folder windows,
  ShowItems opens the item's folder with it selected, ShowItemProperties adds Get Info. A hidden `zacos9-finder.desktop`
  (`zacos9-finder --open URI...`, forwarded to the running Finder) is the default for folders through
  `share/xdg/zacos9-mimeapps.list` (read via XDG_CONFIG_DIRS and XDG_CURRENT_DESKTOP=ZacOS9).
- Seen: gdbus ShowItems on Downloads/report.pdf -> Downloads window, report.pdf selected; `gio open ~/Documents` -> its window.

**Icon labels: cut in the middle, whole when selected** (built; seen in a nested session).

- As Mac OS 8/9: in icon views and on the desktop a long name is cut in the middle to 72 px ("Mousep…rences", "Quarter… 3.txt")
  so it fits its 80-px grid cell; a selected icon shows its whole name (up to 400 px), drawn after everything else so it lies over
  its neighbours, and moved sideways to stay inside the screen or window. Button view shows the whole name when selected too.
  `Item::labelText()` / `fullLabelText()` / `shownLabel()` in `shell/finder/items.cpp`; `paintIconLabel` for the second pass.

**Every drive on the desktop at startup** (built; seen working on the real system with the build's Finder).

- Reported on real hardware: other internal drives (NTFS "Archive and Projects", "GAMING SSD", a Windows partition) didn't appear on
  the desktop: the Finder showed mounted volumes only, and nothing mounts internal drives (udisks: should_automount=0).
- `localVolumesMountAll()` (`shell/finder/localvolumes.cpp`): at Finder start, every GVolume with a block device that can mount and
  isn't mounted gets `g_volume_mount`; later ones too (volume-added). Each is tried once; failures are logged. GIO already hides
  EFI/recovery partitions. `share/polkit/50-zacos9-mount.rules` grants udisks2 filesystem-mount(-system) to local active sudo users
  (internal drives otherwise need a password, and nothing here can ask for one).
- Seen for real once the rule was installed (Finder run from the build): all three mounted under /media/adam and on the desktop.
- Also fixed: `Desktop::paintEvent` never drew `m_localVolumes` (since local volumes were added): they had places and could be
  clicked, but were invisible.

**Installer: Update an installed ZacOS 9 instead of erasing** (built; window flow checked in a nested session with a fake helper;
`--detect`/`--update` not yet run for real - needs root, a new ISO and a disk with ZacOS 9, e.g. the iMac).

- `zacos9-install --detect` (read-only mounts) finds installed systems; the installer lists "— ZacOS 9 0.1.0" beside such a disk and
  offers Update (default; Reinstall at the same version, none if the disk's is newer) beside Erase & Install.
- `zacos9-install --update PARTITION`: apt installs the medium's `zacos9/*.deb` into the system (new dependencies from Debian, so
  online if it has any), then Debian's updates, then update-grub; accounts, files, settings, fstab and boot loader kept.
  `scripts/build-iso.sh` puts the packages on the medium. See `docs/updates.md`.
- Fixed on the way: the script's cleanup did `rm -rf` on the mount point after `umount -R`, which would have deleted a disk's files
  if an unmount ever failed; it is `rmdir` now (start, cleanup and end).

**Bluetooth control panel; headphones and speakers as the sound output** (built; window seen in a nested session; not tried with
real devices yet - this machine has an adapter, but bluez wasn't installed).

- `zacos9-bluetooth` (`shell/panels/bluetooth.cpp`), Apple menu > Control Panels > Bluetooth: Bluetooth On, the device list, Search,
  Connect/Disconnect (pairs and trusts first if needed), Forget, Play Sound Here. BlueZ over the system D-Bus (QtDBus; a
  NoInputNoOutput pairing agent while open); an audio device's PipeWire sink becomes the default output through pactl.
  `debian/control` depends on bluez. See `docs/bluetooth.md`.

**Screen Snapshot in the Apple menu** (built; checked in a nested session; not released yet).

- `shell/snapshot/zacos9-snapshot`: drag a rectangle (slurp, Platinum-styled: black 1-px outline, screen lightly dimmed; Escape
  cancels); grim saves it on the Desktop as "Picture N.png" (Mac OS 9's naming) and wl-copy puts it on the clipboard.
- zacos9-wm now offers `wlr-data-control` so wl-copy sets the clipboard without a window of its own. `debian/control` depends on
  grim, slurp, wl-clipboard.
- Seen: Apple menu > Screen Snapshot, a 300x200 drag -> "Picture 1.png" (300x200, no overlay in it) and `image/png` on the clipboard.

**Nested zacos9-wm overwrote the real session's screens file** (fixed; verified).

- `prefs_write_outputs` named `$XDG_RUNTIME_DIR/zacos9-outputs-*` after `$WAYLAND_DISPLAY`, but runs as the outputs appear, before
  zacos9-wm sets that to its own socket: a nested session wrote the outer session's file. The real Finder then read a nested screen
  ("WL-2") as the main display, put its pattern-only `desktop-extra` layer over both real screens, and hid every desktop icon.
  Now named after `server->socket`. Checked: a nested run leaves the real file byte-identical and writes `...-wayland-1`.

**Clicking the desktop puts the Finder in front** (built; checked in a nested session).

- As on the Mac: a click on the desktop (`zacos9-desktop`, or `desktop-extra` on other displays) brings the Finder's windows forward
  above every other program's, in their own order (and back from Hide Finder), and leaves no window active
  (`view_clear_focus` in `compositor/src/view.c`), so the menu bar shows the Finder's menus (File, Edit, View, Special; "Finder" at
  the right). Clicking a window makes it active again. Seen in a nested session: foot in front of a Finder window -> desktop click ->
  the Finder window comes above foot, Finder's menus, foot's frame inactive; foot alone -> desktop click -> Finder's menus.

**New Tricks in the Apple menu** (built; window seen in a nested session; installing not tried yet - needs a published release).

- `zacos9-update` (`shell/update/`): checks the newest GitHub release's `zacos9_*.deb` against the installed version and apt's pending
  Debian updates, lists both, and with Update downloads, checks (digest, package name, version) and installs the .deb, then Debian's
  updates, through `zacos9-appstore-helper` (new `refresh` and `upgrade` commands). Offers Restart after a ZacOS update.
- `scripts/release.sh VERSION "notes"`: changelog entry, tag, build, push, `gh release create` with the .deb. See `docs/updates.md`.
- Seen: "Updates are available" (a test release from a file) and "up to date" (the real repository, no releases yet).
  19 checks in `meson test update-check`.

**Global menus for Qt 6 programs** (built; tested in a nested session; not on the installed system yet).

- A Qt 6 program's own menus show in the menu bar while its window is in front, and its in-window menu bar goes away.
  `shell/menubar/appmenu.c` (registrar + dbusmenu over GDBus); zacos9-wm reports the front window's pid
  (`platinum_shell_v1` version 2). Details, what's shown and what isn't: `docs/app-integration.md`.

**Terminal: ⌘C/⌘V copy and paste** (built, installed as a .deb and confirmed working on the real system 2026-10-04).

- Reported on real hardware: copy/paste doesn't work in Terminal. zacos9-wm turned ⌘ into Ctrl for every client, so in foot ⌘C was an
  interrupt and ⌘V an unbound Ctrl+V (foot's own keys are Ctrl+Shift+C/V).
- zacos9-wm now leaves ⌘ as Super (Mod4) when foot has the keyboard (`keeps_command_key` in `compositor/src/input.c`); other apps unchanged.
  The menu bar's Edit commands now send ⌘ rather than Ctrl, so zacos9-wm makes the same choice for them.
- `share/xdg/foot/foot.ini` (installed to /usr/share/zacos9/xdg, put first in XDG_CONFIG_DIRS by zacos9-session) binds Mod4+c/v to copy
  and paste. Checked: foot 1.21 reads it from there (`foot --check-config`); ~/.config/foot/foot.ini still overrides it.
- Confirmed by the user after logging in to the installed build: ⌘C/⌘V copy and paste in Terminal.
- Seen in the log, not done: zacos9-wm has no primary selection (middle-click paste), which foot warns about.

**Software window: look-and-feel presets (GIMP › Photoshop Layout)** (built and tested; not yet committed; not in an ISO yet).

- A catalog item can carry a `"style"`; the detail pane then shows a second button (Photoshop Layout /
  Standard Layout) once the app is installed, with a line saying what it does and "Photoshop Layout is
  on." in the status. `appstore/zacos9-appstyle` (Python, runs as the user) does the work — see
  `docs/appstore.md`. `debian/control` now Depends on `python3` and `procps`.
- Checked first: Debian 13 has GIMP 3.0.4 (PhotoGIMP 3.1 needs 3.0+); PhotoGIMP is GPL-3.0; the
  splash/icons/launcher artwork has no stated license, so they're not used.
- Tested for real: in the Software window with GIMP installed — apply, GIMP launched with the layout
  (single slim tool column, tool options and layers on the right), then Standard Layout restored an
  existing settings file exactly. Found and fixed: the preset's `sessionrc` holds its author's
  2560-wide, two-monitor window sizes and positions (GIMP opened off-screen); those are cut out.
- 19 checks in `tests/store/test_appstyle.py` (meson test `appstyle`). Krita/Inkscape presets not done:
  their built-in shortcut schemes still to be confirmed in the Debian versions (Krita 5.2.9, Inkscape 1.4).

**iMac freezes (joining Wi-Fi, dropping an app on Applications): GPU lockups - fix built, not committed.**

- The user's `journalctl -b -1 -k` from a frozen boot (iMac7,1, Radeon HD 2600, `radeon` driver): CP stalled status registers, "GPU reset
  succeeded, trying to resume", then a ttm_bo_release warning - the GPU's 3D engine hung under zacos9-wm's GLES drawing and the screen never
  came back. Not our panels; joining from Terminal only worked because nothing new was drawn.
- `zacos9-session` now uses the pixman renderer when the GPU's driver is `radeon` (pre-GCN Radeons); amdgpu cards keep GL.
  The xdg-activation guards added while hunting this (mapped-only focus, no double insert) stay.
- Seen in the same log: Wi-Fi is Broadcom's proprietary `wl` module (taints the kernel, works).

**Control panel changes ignored in a fresh account - the real cause of the Monitors revert** (fixed; reproduced and verified; not committed).

- zacos9-wm watches ~/.config/zacos9 for the panels' changes, but made it with a one-level mkdir(): in a new account (every live session, and
  the first login after Setup) ~/.config doesn't exist yet when zacos9-wm starts, so the watch failed silently and no panel change applied
  for that whole session (the Finder makes the folder a moment later, so the panels' writes did land in the file). Monitors then re-read the
  unchanged layout and the display jumped back. Mouse, keyboard and resolution changes were equally ignored.
- Now mkdir -p in prefs_init and in lib/settings (pl_setting_set), and a failed watch is logged. Reproduced with HOME=/tmp/fh (empty) and two
  nested screens: before, desktop.conf held the new place but WL-2 stayed at 1280,0; after, WL-2 moved to -30,720.

**Monitors Arrangement: grab offset bug** (found by reproducing; fixed; not committed).

- Real-hardware report: arranging a display reverted and nothing was saved. Cause: on pressing a display that was already selected, the grab
  offset was measured against a stale position (`m_dragPos`, left by the previous drag or never set), so the drop snapped somewhere far off,
  most often back to the start. The first display is selected from the start, so dragging it was always wrong unless it sat at the origin.
- Test (two nested screens, display 1 at x=1280, panel on the main one): dragging display 1 below display 2 now saves and applies (x -30, y 720).

**Monitors Arrangement: drag preview** (built; seen in a nested session; the reported spring-back was not reproduced; not committed).

- Reported on real hardware: dragged displays spring back to where they were. In nested sessions saved layouts always stuck (side, below, raised,
  different sizes - checked by writing desktop.conf directly). Likely cause: the dragged display did not move on screen and snapped to the *nearest*
  touching place on release, so a short drag landed where it started. Now the display follows the pointer and an outline shows where it will land.
- The compositor logs "display layout: the saved places overlap..." (session.log) if it ever discards a saved layout.

**TCP/IP panel: hardening after a crash choosing a Wi-Fi network on the iMac** (built; panel starts; the Wi-Fi path itself is untested - no NetworkManager/Wi-Fi in the test setup; not committed).

- Cause not found. Fixed what could plausibly do it: `nm::run` used `QProcess::waitForFinished()` (banned in CLAUDE.md - it deadlocks in this
  Qt/Wayland process) on the GUI thread for every refresh, join and scan; it now serves events while waiting. The 4-second refresh can no longer
  run inside itself, and join/choose guard a device or list that changed while a dialog was open.
- Other `waitForFinished` calls remain elsewhere (controlpanel, datetime, filesharing, netvolumes, smbclient, discovery, appdb, finder) - not touched.

**Installer lists whole drives only, by make and model** (built; seen on screen; not committed; not in an ISO yet).

- `shell/installer`: the destination list shows only whole disks, each by its make and model (from `lsblk`) - no size, no device name,
  no partitions; drives that report no model are "Hard Disk"; two of one make are numbered (1), (2). The warning names the disk.
  `zacos9-install` itself still accepts a partition on the command line; the window no longer offers one.
- The drive ZacOS 9 is running from (the installer USB or disc, found through findmnt on /run/live/medium) is left out of the list.

**Several displays; Monitors panel Arrangement** (built; tested with two nested screens, not on real monitors; not committed; not in an ISO yet).

- Compositor (`prefs.c`, `output.c`, `layers.c`): per-display settings in desktop.conf - `main-display=NAME`, `display.NAME.resolution=WxH`,
  `display.NAME.x|y=N` (layout position, logical pixels), `mirror=1`. Layout is applied from them (side by side if none or overlapping);
  ZacOS 9's own layer surfaces (namespace `zacos9-*`: menu bar, its menus, desktop icons) are pinned to the main display and move with it
  live, also when a screen is unplugged. The desktop colour rect now reaches negative coordinates. The screens file gains `x y main`, `mirror`.
- Monitors panel: with more than one display, an Arrangement box (drag a display; it snaps to touch another; drag the menu bar strip to
  another display to make it main), Mirror Displays, and the Resolution list for the selected display. One display looks as before.
  Pixel size (scale) is still one setting for all displays.
- Finder: `SecondaryDesktop` shows the desktop pattern on every non-main display.
- Seen with WLR_WL_OUTPUTS=2: dragging display 2 below 1 saved x/y; dragging the strip made display 2 main (menu bar and icons moved, windows stayed);
  mirror put both at 0,0; display.WL-2.resolution=1024x768 applied. Not tested: real monitors/hotplug, dragging windows between displays.
- Not done: per-display pixel size, a different pattern per display, mirror with different resolutions (the smaller shows part).

**Qt style plugin** (built; seen on screen with a Qt 6 test program; not committed; not in an ISO yet).

- `shell/qtstyle/`: a `QProxyStyle` over Fusion, installed to `<libdir>/qt6/plugins/styles/zacos9style.so`, selected for every Qt
  program in the session by `QT_STYLE_OVERRIDE=zacos9`. Platinum palette; push buttons (incl. default ring), check boxes, radio
  buttons, text field frames and scroll bars are drawn by `lib/widgets.c`, so they match the native programs pixel for pixel.
  Scroll bar geometry (arrows at both ends, fixed thumb) is defined in `subControlRect` to match `lib/widgets.c`.
- Seen: buttons, disabled button, check boxes, radios, line edit, a scrolling text view with the Platinum scroll bar. Thumb dragging and
  the arrows were driven with the pointer and scroll the text; page clicks weren't tried. Menus, tabs, sliders, combo boxes, spin boxes are Fusion in the Platinum palette.
- Qt 6 only (Debian 13's Qt 5 programs are not covered). Accent colour is read at start-up from the Appearance panel's setting.

**GTK 3 Platinum theme; toolkit session defaults** (built; looked at on screen with Mousepad; not committed; not in an ISO yet).

- `share/themes/ZacOS9/gtk-3.0/gtk.css` (installed to `/usr/share/themes`): Adwaita as the base, restyled - grey 3-D outlined
  buttons, square corners, sunk white fields, flat grey menu bar and menus, lavender selection, Platinum-style scroll bars and tabs,
  no shadows or animations. `zacos9-session` sets `GTK_THEME=ZacOS9`, `GTK_CSD=0`, `QT_WAYLAND_DISABLE_WINDOWDECORATION=1`.
- Seen on screen in Mousepad (GTK 3): grey menu bar, an opened File menu (grey, lavender bar highlight), the Preferences dialog
  (outlined buttons, tabs, square check boxes with a black mark, combo box). Not seen: scroll bars, GIMP/Inkscape
  (GIMP keeps its own dark theme until Preferences > Theme is set to System). Lavender is fixed - the Appearance accent isn't followed yet.
- Not done: Qt style, GTK 4/libadwaita (ignores themes), global menus (next).
- Build tree: the theme is only installed, not copied into build/share; point XDG_DATA_DIRS at share/ to try it from a build.

**Windows come forward when re-opened; double-click opens one item** (built; checked in a nested session; not committed).

- Compositor: `xdg_activation_v1` (`compositor/src/xdg.c`) - a client asking for its window to be activated brings it to the
  front with keyboard focus. Qt's `activateWindow()` (used by `FolderWindow::open` for an already-open folder) uses it, so
  opening the startup disk while its window is buried raises it. If none is open, one opens as before.
- Double-click opens only the item under the pointer (selecting just it), on the desktop and in folder windows, so two
  icons selected by accident (shift-click, marquee) aren't both opened. File > Open still opens every selected item.

**Documents replaced by Home; the user can add folders to the startup disk** (built; VFS test passes; not committed). Registry version 3 drops the untouched default Documents node (Home holds ~/Documents). File > New Folder in the disk's window makes a folder of the user's own (`vfsNewFolder`, registry node with `"user": true`, real directory in `~/.local/share/zacos9/finder/folders/`); renamable, takes files, Move To Trash deletes it only when empty.

**Home folder on the startup disk; Wine programs without a menu entry** (built; VFS test passes; not tested with a real Wine install; not committed; not in an ISO yet).

- The startup disk gains **Home** (-> `~`, so Downloads from Firefox and everything else is reachable). Registry
  version 2: existing registries pick it up by migration.
- Reported: a program installed through the Windows Installer (notepad .exe) didn't appear in Applications.
  Cause not confirmed (installer probably filed no Start-menu entry). The installer now notes the program
  folders in the prefix (`Program Files*`, `AppData/Local/Programs`) and, if it finds no new Wine entry but a
  new folder, writes its own entry in `~/.local/share/applications/wine/Programs/` for the folder's main .exe.

**Software window: Inkscape › Illustrator Layout** (built and tested; not yet committed; not in an ISO yet).

- Same button as GIMP's. Inkscape 1.4 already ships `adobe-illustrator-cs2.xml`; the preset (new
  `"kind": "prefs"` in `zacos9-appstyle`, no download) sets `/options/kbshortcuts/shortcutfile` in
  `preferences.xml` and reset restores just that value. Verified with real Inkscape 1.4 (strace shows
  it loading the Illustrator set after apply). 7 more checks in `tests/store/test_appstyle.py`.
- Krita Photoshop preset still not done (Krita 5.2.9; scheme unconfirmed).

**Boot screens: logo from the first moment, a new Welcome screen** (built; checked in QEMU under BIOS and UEFI, live and installed; not yet committed; not in an ISO yet).

- **GRUB shows the logo** (centred, on white) from the moment it starts, where there was a blank
  white or black screen — live medium (`iso/config/bootloaders/grub-pc`: 800x600 picture
  `zacos9-boot.png`; `isolinux/splash.png` + hidden countdown for BIOS) and installed
  (`boot/09_zacos9`; `zacos9-install` runs `boot/zacos9-bootlogo` — stdlib-only Python writing a
  white PNG the size of the current screen (fb0) with the logo in the middle — and sets
  `GRUB_GFXMODE=WxH,auto`). GRUB can only draw backgrounds at the top left, hence a picture the
  screen's size. GRUB's own text is hidden by `color_normal=white/black`. Past 4K it's skipped.
  Debian's `00_header` hardcodes `background_image -m stretch`, which is fine for an exact-size picture.
- **Welcome screen** (`compositor/src/startup.c`): "Welcome to ZacOS 9", a vector picture of a modern
  computer (`lib/welcome.c`, cairo; display on a stand showing a ZacOS desktop, keyboard, mouse) in a
  white well, status, progress bar; the extension icons now march in along the bottom of the screen
  as the bar fills (they used to run during the logo phase). Logo-only phase 1500 → 500 ms.
  `lib/tests/test_welcome.c`; `tests/vm/boot-frames.py` recognises the new box.
- Tools: `tools/boot/make-boot-art.py` regenerates the boot art in `assets/boot/` and the two live pictures.
  `build/bootlab/` (not committed): loop-mount the test disks, patch an ISO's boot files with xorriso
  (no rebuild), record a boot frame by frame (`rec.sh`, `rec-iso.sh`, `analyze.py`).
- **Not fixed — gaps of ~1 s of plain white** between GRUB and the splash, and before zacos9-wm
  draws (the kernel's text console clears the screen to its all-white palette when it starts; and
  plymouth quits before the wm's first frame). Tried `fbcon=map:9` (keeps the logo up through the
  first gap) but it leaves a *black* gap after plymouth and unbinds the text consoles (the greeter
  needs them) — would need a seamless hand-off (plymouth deactivate / quit --retain-splash around
  wm start, fbcon bound after) that can't be validated on real hardware from here.
  *Update (0.1.32):* the second gap now has that hand-off (`docs/boot.md`), pending a real boot.

**Disk name, aliases of applications, About logo** (built and tested; not yet committed; not in an ISO yet).

- The startup disk: "Macintosh HD" (Apple's) replaced. Name = the user's rename, else the name
  given at install (`/etc/zacos9/disk-name`), else "Zacintosh HD"; a registry still holding the
  old default is treated as unnamed. The installer's Select screen has a "Disk name" field
  (default Zacintosh HD, ≤ 27 chars, no `:` `/`), passed as `zacos9-install DEVICE NAME`, also
  used as the ext4 label (16 bytes). The desktop's disk can now be renamed (it was "fixed").
  VM-tested: installed as "Adam's Disk", label and file right, shown on the desktop after setup.
- Make Alias on a Macintosh-view item (an application, or a folder standing for a real one) makes
  the alias on the desktop (it silently failed before). An application's alias is a link to its
  desktop file: shown with the app's icon, opening it launches the app (`appLaunchFile`).
- About This Computer: the 64x64 logo (was the 16x16 scaled 3x).
- Installer disk list leaves out RAM disks (`lsblk -e 1,7,11`).

**Installing apps by dropping them onto Applications** (built and tested in a nested session; not yet committed; not in an ISO yet).

- Files dropped on the Applications icon or into its window (`vfsIsApplications`; Applications
  now accepts drops) are installed, not moved: `.exe`/`.msi` to the Windows Installer, the rest
  to the new App Installer (`shell/appinstall/`), a Mac OS 9 copy-style window ("Items remaining
  to be installed", progress bar, Stop).
  - `.deb`: an alert names package, version and publisher (setup runs as root), then
    `zacos9-appstore-helper install-deb FILE` (new: validates, copies the file, `apt-get install`
    with `APT::Status-Fd` for real progress; dependencies fetched). Not stoppable once started.
  - `.AppImage`: copied (chunked, with progress) into `~/.local/share/zacos9/apps`, made runnable;
    name/icon from its own root files via `--appimage-extract`, following their symlinks into
    `usr/share` (extracting a link alone left it dangling). `libfuse2t64` added to the ISO.
  - `.tar.gz/.xz/.bz2/.zst`: unpacked there with progress; a desktop entry inside, else exactly
    one program (or one named like the archive), else refused and removed.
  - Anything else: "isn't an application to install" (shown by the App Installer).
- The Finder now loads `Icon=` file paths (GFileIcon), so these apps show their own icons.
- **Compositor focus bug fixed** (`view_focus`, view.c): after a click or drag on the desktop
  (a layer surface with on-demand keyboard), newly opened windows were drawn active but typing
  went to the desktop until clicked. Only an *exclusive* layer (a menu being tracked) keeps it now.
- Tested: .deb (with a real dependency fetched), a real AppImage (appimagetool), an archive with
  one program, one with two (refused), notes.txt (refused); drops on the icon and into the window;
  the installed archive app launched from Applications.
- Not done: removing such apps (Move To Trash still only hides; `X-ZacOS9-Installed` in their
  entries records what to delete when that's added). Flatpak `.flatpakref` not handled.

**Finder: moving icons in the Macintosh view, Clean Up, Arrange** (built and checked on screen; not yet committed).

- Icons of virtual items (Macintosh HD's three folders, System Folder, Applications) used to refuse
  to drag at all. They now drag within their own window and stay put (remembered per folder, as
  for real folders), carrying `ICON_MOVE_MIME` rather than file URLs, so nothing else accepts them.
  Dropped anywhere else, an application gets an alert: it stays in Applications; Move To Trash
  removes one. Other virtual items: "part of this computer and can't be moved".
- View > Clean Up (nearest free grid spot, topmost-leftmost first) and View > Arrange > by Name /
  Date Modified / Date Created / Size / Kind / Label (`items.cpp` `arrangeIcons`/`sortIcons`), for
  an icon-view window, or the desktop with no window in front (the Trash keeps its corner).
- Checked in a nested session: Documents moved and still there in a new session; Clean Up snapped
  it to the grid; Arrange by Name ordered the disk's folders; dragging an app out of Applications
  showed the alert; desktop Clean Up / Arrange.

**Windows Installer** (built and tested in a nested session, not yet committed; not in an ISO yet).

- `shell/wininstall/`: drop a .exe/.msi on its window (from the Finder or a browser), or
  double-click one in the Finder (`Finder::launchWindows` now opens it here — and no longer uses
  `waitForFinished`). Install (first time: `wineboot --init` with a progress sweep), or Open
  Without Installing (.exe only). Afterwards it names the programs added — the new desktop
  entries Wine files under `~/.local/share/applications/wine/`, which Applications shows.
  Apple menu > Windows Installer. Closing is refused while Wine is running the installer.
- Wine in the ISO: `iso/config/hooks/normal/8000-zacos9-wine.hook.chroot` adds i386 and installs
  `wine wine64 wine32:i386 fonts-wine` (Debian 13's wine64 has no WoW64 — `i386-windows` is
  empty — so 32-bit installers need wine32). **Untested until the next ISO build.**
- Tested with a real 32-bit NSIS installer (`build/wintest/`, made with `makensis`): dragged from
  the Finder desktop onto the window, Install, Wine's first-run setup, the NSIS pages clicked
  through; window then said "Installed: ZacTest Notes." (its uninstaller left out); the program
  appeared in Applications with its icon and opened (Notepad with the installed file).
- Adobe Creative Cloud: deliberately skipped (its installer and app don't run under Wine).
- `shell/wine/zacos9-wine` is no longer used by the Finder (still installed).

**Applications shows only what's been installed; list scrolling** (built and tested, not yet committed; not in an ISO yet).

- Applications used to require `OnlyShowIn=ZacOS9`, which hid real programs (Firefox from
  Software) and showed ZacOS 9's own control panels. Now: every launchable entry *except*
  ZacOS 9's own (`zacos9-*`), the ones the image was built with (the ISO hook writes
  `/usr/share/zacos9/base-applications`), and Wine's "Uninstall …" shortcuts. Windows programs
  installed through Wine appear because Wine files their Start-menu shortcuts as desktop entries
  in `~/.local/share/applications/wine/`. Without the list (an install from an older ISO, a dev
  tree) everything not `zacos9-*` shows. `test-finder-vfs`: 5 new checks, 109 pass.
- List boxes (`PanelList`): the scroll-bar thumb can be dragged, and the scroll wheel scrolls
  (3 rows a notch). Wired into Setup, Installer, Date & Time, Software, Appearance, Network
  Browser and the volume picker. Thumb drag checked on screen in Set Time Zone; the wheel isn't
  (vptr has no axis events).

**Setup Assistant** (first-run account setup, built and VM-tested, not yet committed; not in an ISO yet).
After Mac OS 9's Mac OS Setup Assistant: one window, ◀ ▶ arrows, "Go Ahead" on the last page.
Pages: Introduction, Name (+ short name), Password (+ "Log in automatically", on by default),
Computer Name, Time Zone, Conclusion.

- `shell/setup/` (the window), `setup/zacos9-setup-helper` (root through pkexec; polkit allows
  only the `zacos9-setup` account, and the helper refuses once `/var/lib/zacos9/setup-pending`
  is gone). Input on stdin, so the password is never in a process list.
- `zacos9-install` now removes the live user, `/etc/sudoers.d/live` and live-config's
  `sudo_on_live.rules` from the installed copy, makes the locked `zacos9-setup` account, logs it in
  once via greetd's `initial_session`, and leaves the flag; `zacos9-wm` opens the assistant when the
  flag is there.
- Go Ahead: account (sudo + the live user's groups), password, hostname + pretty name, time zone,
  then greetd restarts into the new account (`/run/greetd.run` removed first — greetd otherwise runs
  `initial_session` only once per boot). Without automatic login, that one-time login is removed
  again so the next start-up asks for the password.
- `build/test-firstrun.py`: install in a VM, drive the assistant by keys, check the disk. Passed:
  account/groups/hostname/greetd all as chosen, setup account and live user gone, and the session
  hands over to the new account's desktop.
- **Text login fixed:** it drew white on white — the boot's all-white console palette was only
  reset by `zacos9-console-colors.service` 20 s after greetd started. greetd now runs
  `session/zacos9-greeter`, which runs `setvtrgb vga` before `tuigreet` (`build/test-greeter.py`:
  readable at once at boot with automatic login off; logging in by keys reaches the desktop).
  A Platinum login window in place of tuigreet is still on the roadmap.
- Seen once, not reproduced: `plymouth-quit.service` taking ~19 s (once timing out at 20 s),
  which holds greetd back that long. Worth watching on real hardware.

**Network Browser** (built, tested end to end against a real AFP server, not yet committed).

Connecting to other computers from the Apple menu, right after Software:
discover servers (Bonjour, and our own WS-Discovery probe for Windows),
Connect to Server, log in, pick an AFP volume, mount it — it then shows
up on the desktop as a disk icon, and ⌘Y/Put Away ejects it. See
[docs/network.md](docs/network.md), which also covers five real bugs
found only by actually connecting end to end (two in `avahi-browse`
invocation, a `QProcess::waitForFinished()` deadlock, a rejected `--`
argument, and `gio mount`'s meaningless exit code) and exactly what
could and couldn't be verified live in this dev container.

- `shell/network/` — `netbrowser.{h,cpp}` (the window), `discovery.{h,cpp}`
  (Bonjour via `avahi-browse`, Windows via a hand-written WS-Discovery
  probe — Debian ships no query tool, only `wsdd2`'s responder),
  `afpclient.{h,cpp}` (wraps the already-built `zacos9-afp
  info`/`volumes`/`mount`), `smbclient.{h,cpp}` (wraps `gio mount`),
  `netvolumes.{h,cpp}` (what's mounted, read from `findmnt`/gvfs's own
  runtime directory — no registry to drift from reality), three dialogs
  (`connectdialog`, `logindialog`, `volumedialog`).
- AFP mounts fully detached (double-fork + a pipe for the password,
  `afpMount()`) at `~/.local/share/zacos9/mounts/<volume name>`, so
  they outlive the GUI the way a real Mac's mounted volumes did.
- `shell/finder/desktop.cpp` polls what's mounted every 3 s and shows
  each as a disk icon (`Item::isNetworkVolume`); `Finder::putAway()`
  ejects instead of trying to move a file. Dragging one is not
  supported yet — inherited from a pre-existing gap (every "fixed"
  desktop item, including the startup disk itself, already couldn't be
  dragged, with its own TODO predating this work).
- Netatalk needs nothing to be Bonjour-discoverable once `avahi-daemon`
  is running (confirmed live: it self-announces via its own
  Bonjour-compatibility library, under the configured server name).
  Samba's own avahi support couldn't be confirmed the same way (`smbd`
  crashes on start in this dev container — unrelated pre-existing
  issue), so `sharing/zacos9-sharing-helper` still writes an explicit
  `_smb._tcp` avahi service file when SMB sharing is on.
- `avahi-utils` and `gvfs-backends` added to `debian/control` Depends.
- Deliberately no SMB share-list dialog: anonymously browsing a
  server's share list needs its root mounted first, and that behaved
  inconsistently testing against this project's own (broken) SMB
  server; the login dialog asks for the share name directly instead,
  the way Windows' own "Map Network Drive" does.

Verified end to end against this project's own Netatalk test server,
through the real menu and real keyboard/mouse input, not just
`meson test`: opened from the Apple menu; it found "Platinum Test (AFP)"
by itself (no manual entry needed) once File Sharing's AFP was turned
on; logged in as `platest`; the volume list showed "Public" and
"platest's home"; mounted "platest's home" (confirmed with `df` and a
real write); watched it appear as a disk icon on the desktop within the
next poll; ⌘Y'd it away (confirmed gone from both the desktop and
`findmnt`). `tests/ui/network-connect.sh` scripts this exact sequence
and was itself run to completion to confirm its coordinates are right.

**Not verified live**: a real SMB mount (`smbd` crashes on start in
this dev container, and no other SMB server was reachable on the dev
network), and WS-Discovery finding an *independent* Windows computer
(only this container's own `wsdd2` responder answered — correctly, but
it proves the protocol code works, not that it finds someone else's
computer on a real network). Both follow their documented protocols/
conventions faithfully; both are the first things to check if they
don't work on a real network.

---

**Software window** (built, tested end to end against the real system, not yet committed).

A curated app catalog in the Apple menu, right after Classic:
category list, item list, details pane with Install/Remove — Mac OS 9
never had one of these, so it's styled after the Mac's own
list-and-details convention (and the Installer VISE packages its CDs
shipped) rather than any real reference. See
[docs/appstore.md](docs/appstore.md).

- `shell/store/` — `store.{h,cpp}` (the window, built entirely from
  existing `PanelList`/`PanelButton`/`pl_progress_paint` widgets),
  `storeclient.{h,cpp}` (catalog loading, helper invocation).
- `assets/store/catalog.json` — 20 curated apps across 6 categories plus
  Featured; shipped read-only, installed to `$datadir/zacos9/store/`.
- `appstore/` — `zacos9-appstore-helper` (root, through pkexec;
  validates every package name — Debian's own charset, never starting
  with `-` — *before* its root check, so the check runs regardless of
  caller) plus the matching polkit `.policy`/`.rules`, mirroring
  `sharing/`'s passwordless-for-sudo-group pattern (no polkit agent runs
  on this desktop to prompt for a password).
- An installed item's icon is the real one: matched by package against
  `appList()`'s `AppEntry::origin` (this session's earlier
  `dpkg:<package>` origin lookup), reusing the Finder's own icon
  resolution rather than duplicating it.
- Deliberately indeterminate progress (a sweep, not a percentage):
  parsing apt's own output for real progress isn't stable across
  versions; showing a possibly-wrong number seemed worse than an honest
  "working."
- `"Software"` added to `share/applications/` (so it is also discoverable
  from the Finder's own Applications folder, Categories=System) and to
  the Apple menu (`shell/menubar/launch.c`).

Verified end to end against the real system, not just `meson test`:
opened from the Apple menu; browsed every category; selected Calculator,
clicked Install, watched the real `apt-get install galculator` run
through the helper, confirmed installed both on screen ("Installed.",
the real calculator icon) and via `dpkg-query` outside the app; clicked
Remove, confirmed the alert names the package and offers Remove/Cancel,
confirmed removal both on screen and via `dpkg-query` — nothing left
installed afterward.

---

**Virtual Macintosh filesystem** (built, tested, not yet committed).

The Finder shows a startup disk holding System Folder, Applications and
Documents instead of Debian's Unix hierarchy. Nothing on disk is moved,
renamed or hidden; no FUSE mount. See [docs/vfs.md](docs/vfs.md).

The Startup Items, persistent alias, Extensions Manager and refresh refinement
pass is implemented and focused-tested. Login startup launches effective XDG
autostart entries through the compositor's post-display hook; alias resolution
and service queries run asynchronously; service mutations revalidate the
current user-unit policy; source watchers are debounced and bounded. Verification
passed with `meson compile -C build` and the `autostart`, `alias`, `userservices`,
`finder-vfs`, `finder-desktop` and `finder-selection` suites. These automated
checks do not constitute a live login or interactive desktop verification.

The Apple-menu **About This Computer** view is separate from the technical
System Information window. It uses the installed ZacOS version, readable
processor/graphics labels and gigabyte memory summaries in a dedicated
classic-styled layout.

Get Info on the virtual Zacintosh HD now reports total capacity and available
space on the filesystem backing Home; its Source and Access rows are omitted.

- `shell/finder/vfs.{h,cpp}` — the node model and its JSON registry at
  `~/.local/share/zacos9/finder/vfs.json` (stable ids, display names,
  icons, backing paths, visibility, kinds, migration by `version`).
- `shell/finder/appdb.{h,cpp}` — installed applications through GIO:
  XDG precedence, `Hidden`/`NoDisplay`/`OnlyShowIn`/`NotShowIn`/`TryExec`
  (and GIO's own rule that a plain `Exec` naming a program that can't be
  found is dropped too), Desktop Actions, launching with full
  desktop-entry semantics (`Exec` is never handed to a shell), real
  application icons via `QIcon::fromTheme`, and package/version origin
  via `dpkg` (never guessed for Flatpak, read from `X-Flatpak` instead).
- `lib/draw.{h,c}`: `pl_image_blend`, real alpha compositing for a
  resolved icon (the compiled icon set stays plain on/off alpha).
- Hide-not-uninstall: Move To Trash on an application's folder asks for
  confirmation, then hides it via the same override mechanism a rename
  uses — never touches dpkg. **Special > Show All Applications** (new
  menu item, `shell/menubar/menus.c`) reverses it.
- Integrated at the existing chokepoints only: `listFolder()`,
  `openSelection()`, `displayName()`, the desktop's disk item,
  `FolderWindow`'s path handling, `LabelEditor::commit()`,
  `moveSelectionToTrash()`, `fileops`, `paintIconItem()`.
- Two new icons (System Folder with the sparkle, Control Panels with
  sliders) in `assets/icons/platinum-icons.picon`.
- `libglib2.0-dev` added to Build-Depends; `qt6-svg-plugins` added to
  Depends (SVG-only icons need the plugin at runtime) in `debian/control`.
- Deliberately not done: a Version/Package row in Get Info — that
  window is pixel-measured from a real Mac OS 9 screenshot, which has no
  such row; the data is there (`AppEntry::version`/`origin`) for when a
  reference screenshot justifies adding one.

Verified on screen, through real menu/keyboard interaction (not just
`meson test`): the disk's exactly three folders; Applications listing
one folder per installed application with its real resolved icon (a
Desktop Action keeps the generic document icon); Mousepad launching from
its folder; the System Folder's five folders; `showUnixVolume`'s second
"Unix" disk; and the full hide/confirm/restore round trip — Move To
Trash on an app folder, the confirmation alert naming it, the folder
disappearing from the open Applications window live, then Special > Show
All Applications bringing it back.

## Open issues / known gaps

- **`smbd` crashes on start in this dev container** (`core-dump`,
  signal ABRT, immediately after "ready to serve connections") — a
  pre-existing environment issue, not investigated (not this session's
  AFP/discovery work, and not something `zacos9-sharing-helper`'s
  config generation can cause). Blocks live verification of real SMB
  serving *and*, as a side effect, real SMB mounting (there was nothing
  reachable to mount from). Worth a real investigation before relying
  on SMB in this project at all.
- No SMB share-list dialog (see `docs/network.md`); no automatic
  retry/backoff in the Network Browser if `avahi-daemon` itself isn't
  running yet when it scans.
- AFP connects measured at roughly ten seconds end to end through the
  GUI in this dev container, versus well under one second for the same
  command run directly from a shell — almost certainly a WSL networking
  quirk (see `docs/network.md`), not a logic bug (the fix for the
  actual bug alongside this, a `QProcess::waitForFinished()` deadlock,
  is already in). Expect this to be faster on a real installed system;
  not confirmed either way yet.
- Network volumes can't be dragged (inherited, not introduced here: the
  startup disk itself already couldn't be, with its own pre-existing
  TODO) — ⌘Y or File > Put Away is the way to eject one.
- The Software window has no search and no batch install (checkboxes +
  one combined apt transaction, closer to Mac OS 9's own CD installer);
  both left out deliberately, see `docs/appstore.md`'s "deliberately
  left out" section. No "Check for Updates" either — `dpkg-query` says
  installed or not, not whether a newer version exists.
- `debian/control` still needs `fuse3`, `libfuse3-dev`, `libgcrypt20-dev`
  for `zacos9-afp` (left alone: separate feature — it's the same
  binary the Network Browser now drives, so this is worth picking up
  alongside it next).
- ISO has not been rebuilt since the silent-boot commit; it is missing
  file sharing, Get Info Sharing, the AFP client and the virtual filesystem.
- ~~Long icon labels overlap in icon view~~ - fixed (2026-10-04): labels are cut in the middle to fit their grid cell, and shown
  whole, over their neighbours, while selected (Mac OS 8/9's way).
- Get Info on a *curated* folder (System Folder, Applications, an
  application's folder) opens nothing, because `InfoWindow` reads a file
  and those stand for none. Applications and `backed` folders do work —
  they show their desktop entry / real directory. A Get Info of their own
  is the follow-up, and the natural place to eventually show version/origin.
- Resolved icons are kept in memory only (no disk cache); cheap enough in
  practice (one theme lookup per newly-discovered application) that this
  wasn't worth the added complexity, but worth revisiting if a system
  with hundreds of installed GUI apps makes refresh noticeably slow.
- `scripts/snapshot.sh` **was** suspected broken (its `x11-namespace.sh`
  re-exec appeared to leave UI tests talking to the wrong
  `WAYLAND_DISPLAY`) — re-tested this session and it now works correctly
  end-to-end. The earlier failure looks like the stale-WSLg-bridge class
  of issue this project already knows about (blank/stuck windows after
  sleep or a monitor change — `wsl --shutdown` fixes it), not a real bug
  in the script. If it recurs, shut down WSL first before concluding the
  harness is broken.

## Next steps (priority order)

1. **Investigate the `smbd` crash** in this dev container — blocks real
   verification of both SMB serving and SMB mounting.
2. **`debian/control`** — add `zacos9-afp`'s own dependencies
   (`fuse3`, `libfuse3-dev`, `libgcrypt20-dev`).
3. **An SMB share-list dialog**, once a working SMB server is available
   to test anonymous root-browsing against reliably (see
   `docs/network.md`).
4. **ISO rebuild** — once the above are in; it is missing every feature
   built since the silent-boot commit (file sharing, Get Info Sharing,
   the AFP client, the virtual filesystem, Software, the Network
   Browser).

## Test servers (local)

| Server | Address | Credentials |
|---|---|---|
| Netatalk (this machine) | `127.0.0.1:548` | `platest` / `Sesame-42`, volume `platest's home` |
| Mac OS 9 (Classic) | `127.0.0.1:5548` | `^Mac User` / `macpass`, volume `Macintosh HD` |

## Architecture cheat-sheet

See `CLAUDE.md` for the full map. Key paths:
- Finder: `shell/finder/` (virtual filesystem: `vfs.cpp`, `appdb.cpp`)
- Control panels: `shell/panels/`
- Software window: `shell/store/`, helper in `appstore/`
- Network Browser: `shell/network/`
- AFP client: `network/afp/`
- File sharing helper: `sharing/zacos9-sharing-helper`
- Tests: `meson test` (unit), `tests/ui/*.sh` (screenshots),
  `tests/sharing/afp-client.sh`
