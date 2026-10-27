# The System Folder: the plan

Mac OS 7 to 9's System Folder was mostly a set of drop-in folders: put
something in one and the system used it, at startup or the next time a
menu was built; take it out and it was gone. This page goes through
every item Apple kept there, what ZacOS has for it today, where that
lives in the code, and how to make each work the Mac way. The Macintosh
view that shows the System Folder is described in [vfs.md](vfs.md); its
default layout is in `shell/files/vfs.cpp`.

**States:** *Working*: in the System Folder and doing what the Mac's did.
*Partial*: ZacOS has the feature, but not as a System Folder item.
*View only*: the folder shows what's there, but dragging changes nothing.
*Missing*: nothing in the System Folder yet. **Size** is a rough effort:
S a day or so, M a few days, L more. Mac OS versions are when an item
first appeared in Apple's System Folder, to the nearest release.

Of 31 items: 15 working, 6 partial, 1 view only, 9 missing.

## The System Folder's own files

| Mac OS item | What it did | ZacOS today | Its home | Idea | Size |
|---|---|---|---|---|---|
| System (7.0) | The OS itself; in System 7 you could drag alert sounds and keyboard layouts in and out of it. | Missing | `compositor/`, `lib/`, `shell/common/` | A *System* item whose Get Info shows the ZacOS version and build; opening it lists the alert sounds and keyboard layouts. | M |
| Finder (7.0) | The Finder application. | Missing | `shell/finder/` | A *Sniffer* item: Get Info shows its version; opening it brings the desktop forward. | S |
| Clipboard (7.0) | What was last copied, kept across restarts. | Partial: Edit ▸ Show Clipboard | `shell/files/clippings.cpp` | A *Clipboard* item that opens Show Clipboard. | S |
| Scrapbook File (7.0) | The Scrapbook's pages. | Partial: Stash | `shell/stash/` | Stash's data as *Stash File*, opening Stash. | S |
| Note Pad File (7.0) | The Note Pad's pages. | Partial: Notepad | `shell/notepad/` | Notepad's data as *Notepad File*, opening Notepad. | S |

## Folders that build menus and run things

| Mac OS item | What it did | ZacOS today | Its home | Idea | Size |
|---|---|---|---|---|---|
| Apple Menu Items (7.0) | Whatever was in it appeared in the Apple menu. | Working: Zac Menu Items, with a (Disabled) twin | `shell/menubar/launch.c` | – | – |
| Control Panels (7.0) | One file per panel. | Working: the panels' desktop entries | `lib/controlpanels`, `shell/panels/panelregistry.h` | – | – |
| Control Panels (Disabled) (7.5) | Extensions Manager moved panels here to turn them off. | Working: drag a panel in to turn it off, back to turn it on | `lib/controlpanels`, `shell/files/vfs.cpp`, `shell/finder/fileops.cpp` | – | – |
| Startup Items (7.0) | Opened everything in it at startup. | Working: XDG autostart | `shell/files/autostart.cpp`, `shell/autostart/` | – | – |
| Shutdown Items (7.5) | Opened everything in it when shutting down. | Working | `session/` (`zacos9-shutdown-items`) | – | – |
| Launcher Items (7.5) | The Launcher's buttons. | Working: Toy Box Items | `shell/finder/toybox.cpp` | – | – |
| Contextual Menu Items (8.0) | Commands added to Control-click menus. | Working | `shell/finder/contextitems.cpp` | – | – |
| Favorites (8.5) | Aliases in the Apple menu and Open dialogs. | Working | `shell/menubar/`, `shell/fileportal/` | – | – |
| Scripts, Folder Action Scripts (8.5) | Scripts, some run when items arrived in a folder. | Working: Folder Tricks | `shell/finder/foldertricks.cpp` | – | – |
| Control Strip Modules (7.5) | One file per Control Strip tile. | Partial: the Collar's modules are built in | `shell/collar/` | Each module registers itself, as the panels do, and *Collar Modules* (and its Disabled twin) appear in the System Folder. | M |

## Extensions

| Mac OS item | What it did | ZacOS today | Its home | Idea | Size |
|---|---|---|---|---|---|
| Extensions, Extensions (Disabled) (7.0, 7.5) | Loaded at startup; drag to (Disabled) to turn one off at the next restart. | Working: drag one in to turn it off, back to turn it on | `system/modules/`, `shell/files/extensioncatalog.cpp`, `shell/finder/modules.cpp`, `shell/finder/fileops.cpp` | – | – |
| Extensions Manager sets (7.5) | Named on/off combinations. | Working: Tricks Sets | `shell/files/tricksets.cpp` | List them in Preferences ▸ Extensions Manager. | S |

## Look, sound and language

| Mac OS item | What it did | ZacOS today | Its home | Idea | Size |
|---|---|---|---|---|---|
| Appearance (8.0) | Desktop Pictures, Sound Sets, Theme Files. | Working | `shell/panels/appearance.cpp`, `customthemes.cpp` | – | – |
| Fonts (7.1) | Drop a font in to install it. | Working | `shell/files/fonts.cpp` | – | – |
| Alert sounds (in System, 7.0) | Drag a sound in and the Sound panel offered it. | View only: Sounds | `lib/settings.c`, `shell/panels/sound.cpp` | Drop a WAV into Sounds and the Sound panel offers it as an alert sound. | S |
| Keyboard layouts (in System, 7.0) | Each layout a resource in the System file. | Missing | `shell/panels/xkblayouts.cpp` | A read-only *Keyboard Layouts* folder; opening one opens the Keyboard panel with it chosen. | S |
| Language & Region Support, Text Encodings (8.5–9) | The scripts and encodings the system could show and type. | Partial: Language Kits, the Numbers and Text panels | `shell/panels/locale.cpp`, `system/accounts/` | A folder of installed locales; adding one goes through the accounts helper. | M |
| Help (7.5, 8.5) | Help books; drop one in and Help listed it. | Missing | `shell/help/`, `share/zacos9/help/` | A *Help* folder listing the books; one dropped in appears in the Help Center. | S |
| Speakable Items, Voices (7.5) | PlainTalk commands and text-to-speech voices. | Missing | `shell/panels/speech.cpp` | A read-only *Voices* folder; opening one plays a sample. | S |

## Settings and shared data

| Mac OS item | What it did | ZacOS today | Its home | Idea | Size |
|---|---|---|---|---|---|
| Preferences (7.0) | Every program's settings file. | Working: opens `~/.config` | `shell/common/conf.h` | Show ZacOS's own settings files with readable names. | S |
| Application Support (8.0) | Programs' shared support files. | Working: a read-only view | `shell/files/vfs.cpp` | – | – |
| Location Manager sets (8.x) | Settings saved per place. | Partial: Walkies | `shell/walkies/location.cpp` | Walkies' places in Preferences ▸ Walkies; opening one switches to it. | S |
| Keychains (9) | Passwords, a file per keychain. | Missing | `shell/panels/keychainaccess.cpp` | A read-only *Keychains* folder; opening one opens it in Keychain Access. | S |
| Printer Descriptions, PrintMonitor Documents (7.x) | Printers' PPDs, and jobs waiting to print. | Missing | `shell/files/printers.cpp`, `desktopprinter.cpp` | Read-only folders of installed drivers and waiting jobs. | M |
| ColorSync Profiles (7.5) | Color profiles; drop one in to install it. | Missing | new provider in `shell/files/` | The system's profiles read-only, and the user's, which a dropped profile joins. | S |
| Internet Search Sites (8.5) | Sherlock's search plug-ins. | Missing | `shell/finder/findwindow.cpp` | Later: search sites as files, offered by Find. | L |

## The mechanisms to reuse

Most ideas above are one of four mechanisms, each with one home in the code:

- **Drop a file in a folder and the system picks it up** → a desktop
  entry with a category, read by one reader: `lib/controlpanels` lists
  Control Panels this way, and the same shape fits Collar Modules, Help
  books and Voices.
- **A "(Disabled)" twin folder** → the user's own copy of an entry, marked
  `NoDisplay` or `Hidden`. Already honored for panels, and used for Zac
  Menu Items.
- **A preferences file per program** → `KeyValueFile` in
  `shell/common/conf.h`, the one reader for ZacOS's settings files.
- **Extensions changed at the next restart** → a root helper in `system/`,
  run through `runHelper()` in `shell/common/process.h`.

## Order

1. Control Panels (Disabled).
2. Extensions you can drag on and off.
3. Sounds you can drop in.
4. Collar Modules as a folder.
5. Help, Voices, Keyboard Layouts and Keychains folders.
6. System and Sniffer items, and the Clipboard, Stash and Notepad files.
