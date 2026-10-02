# Packages, ISO and virtual machine

Everything here runs inside Debian 13 (on the Windows dev box, the `Debian`
WSL distro), from the project folder.

## Debian packages

```sh
scripts/build-debs.sh               # build/packages/platinum-2026_*.deb
scripts/build-debs.sh --emulators   # and platinum-emulators_*.deb
```

- **platinum-2026** is the desktop:
  - `platinum-wm`, the menu bar, the Finder, the control panels and `platinum-classic`;
  - the fonts, icons, patterns, cursors and sounds;
  - a **Platinum 2026** session in `/usr/share/wayland-sessions`.

  Login managers (GDM, SDDM, LightDM, greetd) list the session. From a text
  console, run `platinum-session`. The session log is
  `~/.local/state/platinum/session.log`.
- **platinum-emulators** holds SheepShaver and Basilisk II, built from the pinned
  macemu commit in `emulation/build-emulators.sh`. They are installed in
  `/usr/libexec/platinum`, where `platinum-classic` looks for them.
  - The emulators are GPL-2. Their source is written next to the package as
    `platinum-emulators_*.source.tar.gz`, and must go wherever the package goes.
  - No ROMs or Mac OS system software are included.

The package is built from what `git add -A` would commit. That means the tracked
files plus any new ones, minus everything `.gitignore` excludes. Build output
and the Apple files in `macos/` and `uploads/` can never get into a package.

The package build runs the unit tests. Without the HIG figures
(`tools/measure/figs`), the pixel tests are skipped.

To install the packages on a Debian 13 machine:

```sh
sudo apt install ./platinum-2026_*.deb ./platinum-emulators_*.deb
```

## The ISO

```sh
scripts/build-iso.sh                # build/iso/platinum-2026-VERSION-amd64.iso
```

This builds the packages, then a Debian 13 live image with live-build:

- **Work folder.** live-build runs in `/var/tmp/platinum-iso`, because it needs a
  Linux file system for its chroot.
- **Requirements.** It needs sudo, a network connection and about 10 GB free.
- **Time.** The first build downloads everything and takes 20 to 40 minutes.
  Later builds reuse the package cache.

The configuration is in `iso/config/` and is copied over a fresh `lb config`.
The live system includes:

- **Login.** greetd with tuigreet (`iso/config/includes.chroot/etc/greetd`).
- **Session.** Xwayland, foot, PipeWire and NetworkManager (set up in the
  TCP/IP control panel).
- **Firmware.** Debian's `non-free-firmware`, so that real hardware works.

**Live.** The ISO starts straight into Platinum 2026 as the user `user`. There
is no password, and `sudo` works without one.

- How it works: `iso/config/includes.chroot/usr/lib/live/config/2000-platinum-greetd`
  adds greetd's one-time `initial_session` at boot.
- Logging out (or Ctrl+Alt+Backspace) shows the login screen.
- Add `noautologin` to the boot line to start at the login screen.

**Install.** The boot menu also offers Debian's installer:

1. It copies the system to a disk and creates your user.
2. The installed system starts at the login screen, which runs Platinum 2026.

Write the ISO to a USB stick with any image writer (it is a hybrid image), or
boot it in a virtual machine.

## Trying it on a Mac (2008 to 2015 Intel MacBooks and iMacs)

The live system runs from the USB stick and changes nothing on the Mac's disk.

1. **Make the USB stick on Windows.** You need a USB stick of 4 GB or more,
   which will be erased. Use either of these:
   - [balenaEtcher](https://etcher.balena.io/);
   - [Rufus](https://rufus.ie/). When it asks, choose **Write in DD Image mode**.
2. **Start the Mac from the stick.**
   - Shut the Mac down and plug in the stick.
   - Press the power button and hold **Option (⌥)** until the startup disks
     appear. Choose **EFI Boot**.
   - The boot menu starts the live system after 5 seconds.
3. **Use the desktop.** The desktop appears by itself within a minute or two.
   - A Retina screen shows it at 2× automatically, giving a 1280×800 or
     1440×900 desktop. Monitors ▸ Normal switches to 1×.
4. **Connect to Wi-Fi.**
   - The Broadcom `wl` driver is included.
   - Open **Control Panels ▸ TCP/IP** from the logo menu, choose **Wi-Fi** in
     Connect via, then your network in the **Network** pop-up.
   - A Thunderbolt Ethernet adapter, or USB tethering from a phone, also works.

**Keys.** ⌘ is the Command key. The F-keys need fn, for example
Ctrl+Alt+fn+F2 for a text console. Ctrl+Alt+Backspace ends the session and
shows the login screen.

**Don't choose "Start installer"** unless you mean to install. It can erase
the Mac's disk, including macOS.

**If something goes wrong:**
- Note your exact model (About This Mac ▸ Model Identifier, for example
  `MacBookPro11,1`).
- The session log is `~/.local/state/platinum/session.log`. Read it from
  Terminal with `cat`, or copy it to another USB stick.
- 15-inch models with NVIDIA graphics are the most likely to have display
  trouble.

## Virtual machine

```sh
scripts/vm.sh                # the newest ISO, live
scripts/vm.sh --install      # with a 20 GB disk, to try the installer
scripts/vm.sh --disk         # boot what you installed
scripts/vm.sh --uefi ...     # UEFI instead of BIOS
```

On Windows, double-click **`vm.cmd`**, or run `.\vm --install` from a terminal.

**How it runs.** QEMU runs inside WSL with KVM and shows its screen in a WSLg
window. Unlike `run.cmd`, this runs the whole system: booting, the login screen,
the DRM backend, consoles (Ctrl+Alt+F1 to F12) and sound.

**Files.** The disk and QEMU's monitor socket are in
`~/.local/share/platinum-vm`. Delete `disk.qcow2` to start over.

**Hyper-V.** Not tested yet. It should work in a Generation 2 VM with Secure
Boot off, or set to "Microsoft UEFI Certificate Authority".

## Tests

```sh
sh tests/vm/boot.sh [--uefi]   # boots the ISO headless; passes on the desktop
```

The test reads QEMU's screen through its monitor (`screendump`). It passes once
the Platinum menu bar is drawn (`tests/vm/desktop-up.py`). The result is
`/tmp/vm-desktop.png`.
