# ZacOS 9

A strict, pixel-faithful replica of the Mac OS 8/9 "Platinum" desktop, built as a
desktop environment for Debian Linux, with built-in support for running classic
Mac software through emulation.

No Apple code, ROMs, fonts, icons or sounds are included. Every asset is original
or freely licensed. To run classic Mac apps, you supply your own ROM and system
software.

## Layout

| Path | What |
|---|---|
| `compositor/` | `zacos9-wm`: Wayland compositor (C, wlroots 0.18). Draws window frames and handles input. |
| `shell/` | Menu bar, Sniffer, Control Panels (Qt6). *Phase 2+* |
| `style/` | Platinum QStyle plugin and GTK theme. *Phase 5* |
| `emulation/` | SheepShaver / Basilisk II integration. *Phase 4* |
| `assets/` | Original fonts, icons, desktop patterns, sounds. |
| `docs/` | Roadmap, fidelity references, design notes. |
| `scripts/` | Build and run helpers; package, ISO and VM scripts. |
| `session/` | The login session: `zacos9-session` and its `wayland-sessions` entry. |
| `hardware/` | Hardware quirks: Haswell HDMI audio, ACPI interrupt storms on Macs. |
| `debian/`, `packaging/`, `iso/` | Debian packaging, the emulator package, the live-build configuration. |

## Developing on Windows (WSL2 + WSLg)

The source tree lives on the Windows drive. Everything builds and runs inside the
`Debian` WSL distro, and WSLg shows the compositor as a normal window.

To run it, double-click **`run.cmd`** in the project folder, or type `.\run` in a
VS Code / PowerShell terminal opened in this folder.

This builds the code and opens `zacos9-wm` with a terminal (`foot`) running
inside it. **The window often opens behind other windows.** Look for
"wlroots - WL-1 (Debian)" on the taskbar. To quit, press **Ctrl+Alt+Backspace**
inside it or close the window.

Options:
- `-d` turns on debug logging.
- `-S 2` scales everything 2× (crisp on 4K screens).
- `-s CMD` runs CMD at startup.

For example: `.\run -S 2 -s "foot & xterm"`.

Both Wayland and X11 apps work. Under WSLg the run scripts give the compositor
a private `/tmp/.X11-unix` (see `scripts/x11-namespace.sh`), because WSLg's copy
is read-only.

**If the window shows on the taskbar but draws nothing** (you see your desktop
wallpaper through it), WSLg's display bridge has gone stale. This is common
after sleep or monitor changes on multi-monitor setups. Run `wsl --shutdown` in
PowerShell and launch again. `wsl --terminate Debian` is not enough, because it
doesn't restart WSLg.

Build only: `wsl -d Debian -- scripts/build.sh`

## Packages, ISO and VM

- `scripts/build-debs.sh` builds the `.deb` packages.
- `scripts/build-iso.sh` builds a live and install ISO that boots straight into
  ZacOS 9.
- **`vm.cmd`** runs that ISO in a QEMU virtual machine, with a real boot, login
  and screen.
- `scripts/dev-boot.sh` tries system and boot changes in a VM in a few minutes,
  without building the ISO.

See [docs/packaging.md](docs/packaging.md).

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
takes effect after a restart. See [Hardware quirks](docs/hardware.md).

## Fullscreen applications

Native Wayland and Xwayland applications can request true fullscreen, including
Firefox's fullscreen button, F11 and a video player's fullscreen shortcut.
The window's frame is hidden and its content fills the selected monitor,
covering the menu bar while the fullscreen window is active. Leaving fullscreen
restores its previous window size, position and zoom state. Other monitors keep
their desktop layout; resolution changes and monitor removal update fullscreen
placement.

## Busy pointer

An original animated dog wags its tail and does backflips while applications
launched from Sniffer or the application menu are starting. The normal pointer
returns when the matching application opens or activates a window. Launch
failures and launcher disconnection cancel feedback; applications that never
show a window time out after 15 seconds. The pointer remains usable throughout.

Applications requesting the standard wait/progress cursor also use the dog,
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
See [Software](docs/appstore.md) for details.

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

Double-click `.zip`, `.7z`, `.sit`, or supported tar archives to extract them beside the
archive. A single top-level item keeps its name; multiple items go into a folder
named after the archive. Existing items are never overwritten, and the archive
is kept. 7z support uses `python3-libarchive-c`; StuffIt (`.sit`) support uses
`unar` and `lsar` (installed with ZacOS 9) and preserves resource forks as hidden
`._` AppleDouble sidecars without recursively expanding nested archives.
Unsafe StuffIt paths, links, and special entries are rejected before extraction.
Password-protected archives are reported as unsupported rather than prompting.

### Unreadable USB disks

Sniffer automatically mounts newly inserted volumes once GIO reports them
ready and displays mounted disks, including those under `/run/media`. A disk
that Linux detects but cannot mount may need filesystem support or repair;
discovery alone does not make unsupported formats readable.

When an unformatted USB disk is detected, or an eligible USB disk cannot be
mounted, Sniffer offers **Eject**, **Ignore**, and **Initialize** in a
classic-style alert. Initialize asks again before erasing the **entire disk**,
including every partition, then creates an MBR partition table with one
**FAT32** volume named **Untitled**. Cancel is the default on the erase
confirmation; administrator authentication is required.

Never initialize a disk containing files you need to recover. This is not a
repair tool. Mounted, system, encrypted, read-only and other unsafe disks are
excluded. Hardware that Linux cannot detect cannot be initialized this way.
FAT32 limits individual files to less than 4 GiB; the formatter only accepts
disk sizes compatible with its FAT32/MBR layout.

## Custom desktop patterns

Appearance also supports saved appearance presets and interface sound sets.
Use its **Themes** and **Sound Sets** tabs, with custom files under
**System Folder > Appearance > Themes** or **Sound Themes**.
See [Themes and sound sets](docs/themes.md) for formats and compatibility limits.

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

## License

GPL-3.0-or-later.
