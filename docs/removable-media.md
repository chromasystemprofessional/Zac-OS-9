# Removable media

The Sniffer shows CDs, DVDs, floppy disks, Iomega Zip/Jaz disks, USB storage
and disk images on the desktop and opens them like any other disk. Ordinary
media have no second mounter: UDisks2 owns the hardware, GIO reports volumes
and mounts, and the Finder draws them. Mac APFS images use the existing
read-only FUSE helper.

## What appears

- **Data CDs and DVDs** — mounted by UDisks2, including ISO 9660 and UDF
  (video DVD) filesystems.
- **Audio CDs** — a GIO `cdda://` volume with no native filesystem path, so
  the Finder gives it its own icon and opening it starts the disc player.
- **Blank CDs and DVDs** — shown on the desktop; opening one explains that
  it is blank, and Put Away ejects it. ZacOS has no burning program.
- **PC floppy drives** — a disk is mounted when inserted and the icon
  disappears when it is removed.
- **USB floppy and Iomega Zip/Jaz drives** — polled as SCSI media; their
  partitions are mounted and unmounted as one cartridge.
- **Disk images** — read-only loop devices, described below.
- **USB sticks, SD cards and extra drives** — the existing UDisks/GIO path,
  with a USB badge.

Media are recognized by udev/UDisks, not by mount-point names or file
extensions: `ID_CDROM`, `ID_DRIVE_FLOPPY`, `ID_DRIVE_FLOPPY_ZIP`,
`ID_DRIVE_FLOPPY_JAZ`, `ID_FS_TYPE`, the loop backing file, and so on. The
classification lives in `shell/files/mediakind.cpp`; the desktop icons come
from `MediaKind` in `shell/files/items.cpp`.

## Mounting and opening

`localVolumesMountAll()` mounts every drive volume at login and on hotplug.
An unmounted optical volume is still listed by `localVolumes()`, so it can be
opened later. Opening an unmounted data disc mounts its `GVolume` and opens
the first filesystem; opening an audio CD starts the player; a blank disc is
reported as blank.

Put Away, dragging a disk to the Trash, and the Sniffer's Unmount Disk all
use one decision table (`shell/files/mediaeject.cpp`):

| Medium | What Put Away does |
|---|---|
| Loop image | Unmount, then delete the loop device |
| Optical data/video | Unmount, then eject the drive |
| Audio CD / blank disc | Eject the drive |
| PC floppy | Unmount only; the disk is safe to remove |
| Zip/Jaz | Unmount, then eject the drive when it can |
| USB/ordinary disk | Unmount; eject the drive only when supported |

The desktop icon stays until the mount is actually gone. A busy volume
reports the error and keeps its icon.

A mounted PlayStation 1 or 2 game disc dragged onto **Home ▸ Games** is
handed to the Game Player extra, which reads it into a cache image and
launches mednafen or PCSX2; nothing is copied into Games. PlayStation 3 discs
are not read directly.

## AutoPlay

With **Sniffer ▸ Preferences ▸ General ▸ Media** on, an inserted audio CD or
video DVD plays in VLC. The content-type guess is asynchronous, and only
physical optical media are considered. VLC's NoDisplay entries are preferred;
the configured default, other handlers, and finally a prompt to install VLC
are fallbacks. Programs on a disc are never started.

`share/xdg/zacos9-mimeapps.list` sets the disc defaults. `vlc` is installed
on the ISO and recommended by the `zacos9` package. Commercial movie DVDs
are usually CSS-encrypted; Debian main does not include `libdvdcss`. Enable
`contrib`, install `libdvd-pkg`, and run `sudo dpkg-reconfigure libdvd-pkg`
if the decoder was not built during installation. Unencrypted discs play
without it. Blu-ray is not supported.

## Disk images

Double-clicking a recognized image sets up a read-only loop device through
UDisks and opens the first mounted filesystem. The source file is never
written to. Supported content:

- ISO 9660 (`.iso`, and ISO images with other suffixes)
- UDF (`BEA01`, `NSR02`, `NSR03`), including UDF-only DVD images
- FAT12/16/32 and exFAT floppy and Zip images
- ext and NTFS images
- MBR and GPT partition tables
- HFS, HFS+ and APFS, including `.dmg`, `.sparseimage`, and
  `.sparsebundle` (the existing Mac read-only helper)
- Single-data-track CUE/BIN images, converted to a temporary ISO with
  `bchunk`

Suffixes are hints; the content decides. Images are read-only by design and
are mounted with UDisks's `nodev` and `nosuid` protections; the Mac helper
also disables executable files. Converted CUE/BIN images live in
`$XDG_CACHE_HOME/zacos9/disc-images` and are removed with their loop device.

Audio CD images and multi-track CUE/BIN images are not mountable. `.cue`
files in `~/Games` remain PlayStation discs; an empty or unreadable CUE keeps
the old console-game behavior.

## Floppy and Zip media changes

UDisks deliberately assumes a PC floppy always has media so it never clicks
and spins an idle drive, which means GIO has no insertion event. The Sniffer
polls `/dev/fd*` with `FDPOLLDRVSTAT` and reads sector 0 only when the change
generation or flags change (`shell/files/floppypoll.cpp`). The `floppy`
driver is loaded by `hardware/zacos9-floppy.conf`.

USB floppy, ATA/USB Zip and Jaz drives are polled with SCSI `TEST UNIT READY`
(medium-not-present is sense key `0x02`, ASC `0x3A`), with `BLKGETSIZE64` as
a fallback when `SG_IO` is refused. This avoids reading data from an idle
cartridge.

## Limits

- Audio CD images and multi-track CUE/BIN images cannot be mounted.
- Parallel-port Zip drives are not covered.
- MFS 400K/800K Mac floppy images are not supported.
- APFS, HFS and other Mac images remain the Mac helper's read-only scope;
  encrypted images and FileVault are not unlocked.
- Blu-ray/AACS is out of scope.
- The PC/USB floppy and Zip polling and real optical AutoPlay still need
  hardware validation.

## Code and tests

| Part | Where |
|---|---|
| Volume list, mounting, opening, polling, eject | `shell/files/localvolumes.cpp` |
| Media classification | `shell/files/mediakind.cpp` |
| AutoPlay | `shell/files/autoplay.cpp` |
| Eject decision table | `shell/files/mediaeject.cpp` |
| CUE/BIN parsing | `shell/files/cueimage.cpp` |
| Floppy and SCSI media polling | `shell/files/floppypoll.cpp` |
| Desktop icons and opening | `shell/finder/desktop.cpp`, `shell/finder/finder.cpp` |
| Image recognition | `shell/files/items.cpp` |

Tests: `media-kind`, `media-eject`, `media-autoplay`, `local-volume-hotplug`,
`finder-vfs`, and `finder-desktop`.
