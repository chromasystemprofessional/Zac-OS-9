# ZacOS 9 user guide

Details of everyday features: sound, fullscreen programs, the Sniffer,
disks, desktop patterns and wallpaper. ZacOS Help (Help menu) covers the
rest from inside the system.

## Sound output

Open **Apple menu > Control Panels > Sound**, then use **Sound Output** to
choose built-in speakers, a connected HDMI/DisplayPort monitor, headphones,
or another output exposed by PulseAudio/PipeWire. Monitor names are shown when
the audio server provides them. Disconnected ports are omitted.

The choice changes the system's default output and moves existing playback to
it. Volume and Mute control that output; Alert Volume remains a separate setting.
The panel refreshes every two seconds, including when a monitor is connected or
removed, and follows the audio server's fallback output after disconnection.
Audio-server errors are shown in the panel rather than silently ignored.
Adjusting Volume previews the selected alert sound once the change succeeds:
on release when dragging, or after a keyboard adjustment. The preview respects
Mute, Alert Volume and the "None" alert-sound choice.

On computers with Intel Haswell graphics, such as the iMac14,1, HDMI and
DisplayPort sound needs a driver setting that ZacOS 9 applies by itself; it
takes effect after a restart. See [Hardware quirks](hardware.md).

## Fullscreen applications

Native Wayland and Xwayland applications can request true fullscreen, including
Firefox's fullscreen button, F11 and a video player's fullscreen shortcut.
The window's frame is hidden and its content fills the selected monitor,
covering the menu bar while the fullscreen window is active. Leaving fullscreen
restores its previous window size, position and zoom state. Other monitors keep
their desktop layout; resolution changes and monitor removal update fullscreen
placement.

## Busy pointer

An original rainbow dog bone (`assets/cursors/dog-bone.svg`) spins clockwise
while applications launched from Sniffer or the application menu are starting. The normal pointer
returns when the matching application opens or activates a window. Launch
failures and launcher disconnection cancel feedback; applications that never
show a window time out after 15 seconds. The pointer remains usable throughout.

Applications requesting the standard wait/progress cursor also use the bone,
on Wayland and Xwayland, through the ZacOS9 cursor theme. Apps drawing their own
custom cursors or not reporting busy state cannot be detected automatically.
Other cursor shapes, including text selection and resizing, are unchanged.

## Software sources

Software opens with the small curated **Featured** selection. Choose
**All Applications** to browse Debian desktop apps and use **Search** to filter
the current category. Refresh Debian metadata through Software Update after
installing this feature; discovery uses repository AppStream data, not every
APT package.

For more apps, open **Additional Sources > Enable Flathub** and confirm.
The optional **Flathub** category then offers user-scoped Flatpak apps,
separate from Debian packages. Disabling Flathub keeps installed apps and data.
No additional repository is enabled automatically.
See [Software](appstore.md) for details.

## Sniffer

**Sniffer** is ZacOS 9's desktop and spatial file browser (formerly Finder).
The application menu, folder-opening entry and user-facing messages use Sniffer.
Internal names such as `zacos9-finder`, its application ID, D-Bus interfaces and
`shell/finder/` remain unchanged for compatibility.

Select multiple items as in Mac OS: drag a dotted selection rectangle from blank
space on the desktop or in an icon/list window, Shift-click to add or remove an
item, Shift-drag to toggle the items covered, or choose **Edit > Select All**
(Command-A). In list view, start a rectangle in a blank column; it selects rows
whose icon or name it crosses and scrolls when dragged past the window edge.
Drag any selected item to move the whole selection into another folder; hold
Option while dropping to copy instead.

An icon stays where it is put. Moving one leaves its neighbours alone, and
icons may sit close together or overlap as they did on a Mac; **Clean Up** and
**Arrange** are there when an orderly grid is wanted. An alias dragged in from
the Macintosh view (Applications, the System Folder, a control panel) lands
where it is dropped, on the Desktop or in another folder.

Applications, Utilities and the System Folder stay read-only, but aliases can
live there too: they are stored in a small user-owned overlay and shown with
the folder's own items. **File > Make Alias** in one of those windows puts the
alias there; dragging or pasting an alias in does the same. Deleting the alias
removes only the link, never the application or system item.

**Edit > Copy** puts the selected items on the Clipboard (their names as text
too), and **Edit > Paste** duplicates them into the front window or Desktop,
named "copy", "copy 2" and so on beside an original. Files, folders and
aliases are copied as they are; an application's desktop entry is copied as a
real launcher, never turned into an alias. In the read-only folders only an
alias can be pasted.

As in Mac OS 9, a disk's desktop icon is dimmed while its folder window is
open, and comes back to full strength when the window closes.

Archives — `.zip`, `.7z`, `.sit` and the tar and compressed formats — show
a zipped-page icon rather than a blank document, so they can be told apart
at a glance.

Double-click `.zip`, `.7z`, `.sit`, or supported tar archives to extract them beside the
archive. A single top-level item keeps its name; multiple items go into a folder
named after the archive. Existing items are never overwritten, and the archive
is kept. 7z support uses `python3-libarchive-c`; StuffIt (`.sit`) support uses
`unar` and `lsar` (installed with ZacOS 9) and preserves resource forks as hidden
`._` AppleDouble sidecars without recursively expanding nested archives.
Classic Mac folder and file icons carried in those forks (a folder's `Icon\r`)
appear in the Finder after extraction.
Unsafe StuffIt paths, links, and special entries are rejected before extraction.
Password-protected archives are reported as unsupported rather than prompting.

### USB disk initialization and erasing

Sniffer automatically mounts newly inserted volumes once GIO reports them
ready and displays mounted disks, including those under `/run/media`. A disk
that Linux detects but cannot mount may need filesystem support or repair;
discovery alone does not make unsupported formats readable.

When an unformatted USB disk is detected, or an eligible USB disk cannot be
mounted, Sniffer offers **Eject**, **Ignore**, and **Initialize** in a
classic-style alert. Initialize asks again before erasing the **entire disk**,
including every partition, then creates an MBR partition table with one
**FAT32** volume with the name you enter (default **Untitled**). Cancel is the
default on the erase confirmation; administrator authentication is required.

To reformat a readable USB disk, select its disk icon on the desktop and choose
**Special > Erase Disk…**. This erases the **whole physical USB disk**, including
other partitions, and creates a named **FAT32** volume. Back up
everything first and close files on every volume on that disk. Cancel is the
default; administrator authorization is required. The helper unmounts the disk's
volumes without force and stops if they are busy or the disk's identity changes.
Before confirmation, a name dialog accepts 1-11 ASCII letters, numbers, spaces,
hyphens or underscores. Names cannot start or end with spaces; `NO NAME` is
reserved by FAT. Cancelling this dialog does not unmount or erase anything.
Finder pauses automatic mounting for that disk during erasing and remounts it
afterward. Only safe USB disks mounted beneath `/media` or `/run/media` are
eligible; internal, system, encrypted and read-only disks are excluded.
The helper owns the disk exclusively while writing its partition table and
refreshes the kernel's partition view through that same descriptor. It does
not force through a busy disk. A refresh failure stops before formatting;
the error explicitly warns if the partition table has already been changed.

Never initialize a disk containing files you need to recover. This is not a
repair tool. Automatic initialization excludes mounted disks. Hardware that
Linux cannot detect cannot be initialized this way.
FAT32 limits individual files to less than 4 GiB; the formatter only accepts
disk sizes compatible with its FAT32/MBR layout.

### Screen Snapshot

**Apple menu > Screen Snapshot** lets you drag a rectangle, saves it as
**Picture 1.png**, **Picture 2.png**, etc. on the Desktop, and copies the PNG
to the clipboard. Escape or a right click cancels selection.

Capture uses ZacOS's native committed-frame reader instead of `grim` and the
wlroots screencopy path, following a report of YouTube streams becoming stuck
after screencopy captures. It reads the already-displayed image without changing
the compositor's output-render or software-cursor locks. Multiple displays and
HiDPI are supported; hardware cursors are excluded. Captures are limited to
32 megapixels, with explicit errors for unavailable frames or save failures.

Both the capture helper and compositor must be updated: **restart after
installation**. On an older compositor, capture reports that a restart/update
is needed; it does not silently fall back to the reported problematic path.
Native capture is tested with animated software and GPU-rendered output in
isolated sessions. Verification of YouTube playback on the affected system
remains necessary; this is a replacement capture path, not a proven diagnosis
of the server's earlier `403` responses.

## Mac disks and disk images

Sniffer mounts HFS/HFS+ and supported APFS volumes read-only, including Mac
disk images opened with a double-click. See [Mac disks](mac-disks.md)
for supported containers, eject behavior, authentication and limitations.

Other disk images mount read-only too: ISO 9660 and UDF (CD/DVD), FAT/exFAT
floppy and Zip images, ext, NTFS, MBR/GPT-partitioned images, and single-data-track
CUE/BIN images (converted to ISO with bchunk). Double-click one to set up a
read-only loop device and open its first filesystem; Put Away ejects it and
removes the loop device. Image contents are never modified. Audio CD and
multi-track CUE images cannot be mounted.

Floppy disks, USB floppy drives and Iomega Zip/Jaz disks are noticed when
they are inserted or removed. Inserting one mounts it and shows its disk
icon; Put Away unmounts it so it can be removed. See
[Removable media](removable-media.md) for supported formats and limits.

## Inserted discs and AutoPlay

An audio CD or video DVD starts playing in VLC when it is inserted, unless
**Sniffer > Preferences > General > Media** has "Play audio CDs and video DVDs
when inserted" turned off. Audio CDs, blank discs, data CDs and DVDs appear on
the desktop like any other disk. Opening an audio CD plays it, and Put Away
ejects the disc; a blank disc is reported as blank. Programs on a disc never
start by themselves.

A physical PlayStation 1 or 2 game disc can be played without copying it
first: drag its mounted desktop icon onto **Home ▸ Games**. Game Player
reads the disc into its cache image and starts mednafen or PCSX2. PlayStation
BIOS files and the separate Game Player extra are still required.

Commercial movie DVDs are usually CSS-encrypted. Debian main does not include
the `libdvdcss` decoder: enable the `contrib` section, install `libdvd-pkg`, and
run `sudo dpkg-reconfigure libdvd-pkg` if the decoder was not built during
installation. Unencrypted discs play without it. Blu-ray playback is not
supported.

## Custom desktop patterns

Appearance also supports saved appearance presets, user themes and interface
sound sets. Use its **Themes** and **Sound Sets** tabs, with custom files under
**System Folder > Appearance > Theme Files** or **Sound Themes**. **New
Theme…** and **Edit Theme…** open the Theme Editor, which can create a dark
theme and edit an imported Mac OS 9 theme without changing the import.
See [Themes and sound sets](themes.md) for formats and compatibility limits.

Place your own or freely licensed tiles in **System Folder > Appearance >
Desktop Patterns**, then choose them in **Control Panels > Appearance > Desktop**.
The real folder is `$XDG_DATA_HOME/zacos9/appearance/Desktop Patterns`
(normally `~/.local/share/zacos9/appearance/Desktop Patterns`). Files added,
replaced or removed there are discovered automatically, without restarting.
The preview and every monitor use the same tiled image.

Supported inputs include PNG, JPEG, BMP, GIF and TIFF; raw `.ppat`/`.pat`;
classic Mac resource-fork files containing `ppat`, `PAT `, `PAT#`, `ppt#` or
`PICT` resources; MacBinary `.bin`; BinHex `.hqx`; and AppleDouble `._` sidecars
(keep the sidecar beside its original file). Standalone PICT pictures are also
supported through ImageMagick. BinHex checksums and resource offsets are
validated; unsupported packed pattern layouts or PICT drawing operations
produce an import error rather than an incorrect pattern.

Original files are not modified. Import errors appear in the Desktop tab
(hover for full details) and session log. A removed selected tile falls back
to the built-in default, retaining its selection ID if the file returns.
Limits are 16 MiB per source, 1 megapixel per tile, and 512 tiles/16 megapixels
per catalog. No Apple artwork is included.

## Desktop wallpaper

Place photos or user-created PICT desktop pictures in **System Folder >
Appearance > Wallpaper** (`$XDG_DATA_HOME/zacos9/appearance/Wallpaper`, normally
`~/.local/share/zacos9/appearance/Wallpaper`). Existing wallpaper folders from
older versions are moved here automatically without changing selection IDs.
Open **Control Panels > Appearance >
Wallpaper**, select a picture, and choose its placement:

- **Fit**: show the whole photo, preserving proportions with dark margins.
- **Fill**: cover the monitor, preserving proportions and cropping centrally.
- **Stretch**: fill the monitor without preserving proportions.
- **Center**: retain the picture's original size, with margins or clipping.

Selecting a wallpaper activates photo mode; selecting a pattern on the Desktop
tab switches back to tiled-pattern mode. Both selections and placement are
remembered. Each monitor renders the picture independently at its own size,
rather than spanning a combined desktop. Added or replaced files are refreshed
automatically; removed selections fall back to the built-in default.

PNG, JPEG, BMP, GIF, TIFF, standalone PICT and PICT resources in classic Mac
containers are supported. Pattern resources belong in Desktop Patterns instead.
Wallpaper limits are 64 MiB per source, 32 megapixels per picture and 128
pictures/64 megapixels per catalog. Sources are not modified.
