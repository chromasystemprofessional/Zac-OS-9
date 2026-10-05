# Packages, ISO and virtual machine

Everything here runs inside Debian 13 (on the Windows dev box, the `Debian`
WSL distro), from the project folder.

## Debian packages

```sh
scripts/build-debs.sh               # build/packages/zacos9_*.deb
scripts/build-debs.sh --emulators   # and zacos9-emulators_*.deb
```

- **zacos9** is the desktop:
  - `zacos9-wm`, the menu bar, the Finder, the control panels and `zacos9-classic`;
  - the fonts, icons, patterns, cursors and sounds;
  - a **ZacOS 9** session in `/usr/share/wayland-sessions`.

  Login managers (GDM, SDDM, LightDM, greetd) list the session. From a text
  console, run `zacos9-session`. The session log is
  `~/.local/state/zacos9/session.log`.
- **zacos9-emulators** holds SheepShaver and Basilisk II, built from the pinned
  macemu commit in `emulation/build-emulators.sh`. They are installed in
  `/usr/libexec/zacos9`, where `zacos9-classic` looks for them.
  - The emulators are GPL-2. Their source is written next to the package as
    `zacos9-emulators_*.source.tar.gz`, and must go wherever the package goes.
  - No ROMs or Mac OS system software are included.

The package is built from what `git add -A` would commit. That means the tracked
files plus any new ones, minus everything `.gitignore` excludes. Build output
and the Apple files in `macos/` and `uploads/` can never get into a package.

The package build runs the unit tests. Without the HIG figures
(`tools/measure/figs`), the pixel tests are skipped.

To install the packages on a Debian 13 machine:

```sh
sudo apt install ./zacos9_*.deb ./zacos9-emulators_*.deb
```

To publish a release that installed systems pick up through Software
Update, see `docs/updates.md` (`scripts/release.sh`).

- **balena-etcher** is balenaEtcher 2.1.7, repackaged from balena's own Linux
  build by `packaging/etcher/build-deb.sh`. It is preinstalled on the ISO and
  shows in Applications → Utilities.
  - The script downloads the official zip (or takes `ETCHER_ZIP=...`) and
    checks it against the SHA-256 that GitHub publishes for it.
  - It installs to `/usr/lib/balena-etcher`, the path Debian's AppArmor
    profile expects. The package has the upstream name, so balena's own
    `.deb` upgrades it cleanly.
  - The icon (`packaging/etcher/balena-etcher.png`) is balena's
    `assets/icon.png` at the v2.1.7 tag; the Linux zip has none.
  - The licence is Apache-2.0.
  - **Writing a disk needs an administrator's password.** Etcher asks
    through `pkexec`, which the session's polkit agent (`lxqt-policykit`)
    answers with a password dialog.

## The ISO

```sh
scripts/build-iso.sh                # build/iso/zacos9-VERSION-amd64.iso
```

This builds the packages, then a Debian 13 live image with live-build:

- **Work folder.** live-build runs in `/var/tmp/zacos9-iso`, because it needs a
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

**Live.** The ISO starts straight into ZacOS 9 as the user `user`. There
is no password, and `sudo` works without one.

- How it works: `iso/config/includes.chroot/usr/lib/live/config/2000-zacos9-greetd`
  adds greetd's one-time `initial_session` at boot.
- Logging out (or Ctrl+Alt+Backspace) shows the login screen.
- Add `noautologin` to the boot line to start at the login screen.

**Starting up.** No text shows, as on a classic Mac, and the logo (where a
classic Mac showed its start-up icon) is up as soon as the computer can draw it:

1. **GRUB: the logo on white,** from the moment GRUB starts. It waits 3
   seconds on the live medium (none once installed) without a menu.
   - Press **Esc** (or Shift) during that time for the boot menu, which has
     the installer.
   - GRUB can only draw a background image at the screen's top left, so the
     picture is the screen's own size with the logo placed in its middle
     (`boot/zacos9-bootlogo`). The live medium fixes GRUB at 800x600 and
     ships `iso/config/bootloaders/grub-pc/zacos9-boot.png` (and
     `isolinux/splash.png`, 640x480, for BIOS); `zacos9-install` makes one
     for the installed computer's own screen and sets GRUB to that mode
     (`boot/09_zacos9` shows it; past 4K it is left out).
   - GRUB's own messages and errors are white on the white screen, so none
     show; the menu keeps readable colours. Regenerate the live pictures
     with `tools/boot/make-boot-art.py`.
2. **The boot splash: the same logo in the same place,** and a progress bar
   under it (`boot/plymouth`, which keeps its disk password prompt). The
   kernel's console palette is all white while booting (`iso/kernel-params`),
   so nothing written to it shows and no black flashes up.
   - `zacos9-console-colors.service` and `session/zacos9-greeter` restore
     normal colours, so the login screen and Ctrl+Alt+F2 consoles read.
   - Each monitor gets its own centered logo, progress bar and password
     prompt, even at different resolutions. Viewports are kept separate so
     one monitor's artwork cannot overlap another's. Theme updates activate
     Debian's `update-initramfs` trigger to refresh the installed boot image.
3. **zacos9-wm: the logo on white** for half a second, then **the Welcome
   box** - "Welcome to ZacOS 9", a picture of a modern computer
   (`lib/welcome.c`) and a progress bar - over the desktop pattern, with the
   extensions' icons marching in along the bottom of the screen as the bar
   fills, then the desktop. Both screens come from `compositor/src/startup.c`.
   greetd runs on tty1, so going from the splash to the desktop is white to
   white.
   - With multiple monitors, each gets a complete logo and Welcome box,
     centered within that output's logical resolution. This applies to
     extended and mirrored layouts, including different pixel scales.
     It does not change the saved desktop arrangement.
4. **Known gaps:** a second or so of plain white between GRUB and the
   splash (the kernel's text console clears the screen when it starts) and
   again before zacos9-wm draws, where the logo is gone for a moment.
   The earlier GRUB screen still depends on the firmware's display support;
   it cannot independently draw a background for each connected monitor.

**Multi-monitor startup tests.** `meson test -C build startup-displays
boot-splash-displays` checks unequal resolutions, mirrored/offset layouts,
HiDPI, hot-plug and startup completion. The Plymouth test uses its installed
script interpreter with isolated display/sprite mocks and skips when that
plugin is not installed. It does not change the active desktop or boot image.

**Install.** The boot menu also offers Debian's installer:

1. It copies the system to a disk and creates your user.
2. The installed system starts at the login screen, which runs ZacOS 9.

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
   - The screen stays white, then the logo appears. Press Esc while it's
     white for the boot menu.
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
- The session log is `~/.local/state/zacos9/session.log`. Read it from
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
`~/.local/share/zacos9-vm`. Delete `disk.qcow2` to start over.

**Hyper-V.** Not tested yet. It should work in a Generation 2 VM with Secure
Boot off, or set to "Microsoft UEFI Certificate Authority".

## Trying changes without building the ISO

```sh
scripts/dev-boot.sh              # build packages, update, boot in a window
scripts/dev-boot.sh --no-debs    # without rebuilding the packages
scripts/dev-boot.sh --record     # no window: a timeline of the screen
scripts/dev-boot.sh --retina     # a 2560 x 1600 screen
scripts/dev-boot.sh --clean      # start over from the ISO build's system
```

**What it does.** It starts from the system the last ISO build made (its
chroot in `/var/tmp/zacos9-iso`) and puts this tree on top, in an overlay:

- the packages, `iso/config` and its hook;
- packages added to the package lists, installed with apt.

Then it packs the result and boots it in QEMU with the kernel and initramfs
given directly. This takes a few minutes, against about 35 for an ISO.

**What it leaves out.** The boot menu (GRUB or ISOLINUX) needs a real ISO
build.

**Recording.** `--record` takes a screenshot every 0.2 seconds for the first
90 seconds into `/tmp/devboot`, and prints a timeline:

```
  1.0 -   6.6 s  white
  9.8 -  24.0 s  white
 24.2 -  25.6 s  logo
 25.8 -  26.2 s  other        (the Welcome screen)
 26.4 -  89.9 s  desktop
```

The first frame of each run is saved as `run-NN-KIND.png`.

**VM-only screens.** Some of what you see in QEMU never appears on real
hardware:

- the virtual firmware's own text at the very start;
- QEMU's "Display output is not active" while its virtual graphics card
  starts.

**Logging in.** A login runs on the VM's serial port (`user` / `live`):

```sh
python3 tests/vm/serial.py ~/.local/share/zacos9-vm/serial.sock 'journalctl -b'
```

## Tests

```sh
sh tests/vm/boot.sh [--uefi]   # boots the ISO headless; passes on the desktop
```

The test reads QEMU's screen through its monitor (`screendump`). It passes once
the ZacOS 9 menu bar is drawn (`tests/vm/desktop-up.py`). The result is
`/tmp/vm-desktop.png`.
