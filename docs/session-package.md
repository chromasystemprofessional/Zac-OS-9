# The session package

**Status: implemented in 1.18.28.** ZacOS 9 was one binary package until
1.18.28. TTOS Linux wants to offer the desktop session inside its own
Debian-based installer. This page describes the split: what belongs to the
session, what belongs to the whole-OS package, and what is tested before the
session is installed on another distribution.

## Why

Tom's plan for TTOS is to install the ZacOS package unmodified and change
only the `/etc/skel` wallpaper. The current `zacos9` package is a whole-OS
package: installing it also installs ZacOS's boot splash, GRUB scripts,
installer, Setup Assistant, Software/Update and the update timer. Those are
correct on a ZacOS ISO and wrong on TTOS.

The package divides into:

| Package | Entries | What it is |
|---|---|---|
| `zacos9-session` | 225 | the desktop a foreign distribution can install safely |
| `zacos9-greeter` | 2 | the optional greetd login window |
| `zacos9` | 94 | boot, installer, Setup Assistant, Software/Update |
| `zacos9-emulators` | separate | optional Classic emulators, unchanged |

`tests/baseline/installed-files.txt` lists 315 entries. Six Plymouth
`happy-zac-*.png` files are installed to both `share/plymouth` and
`share/zacos9/boot`, so the packages contain 321 paths: 225 in the session,
2 in the greeter and 94 in the full package.

## What moves to `zacos9`

These are the files that touch the machine outside the session:

```text
usr/bin/zacos9-appinstall
usr/bin/zacos9-installer
usr/bin/zacos9-setup
usr/bin/zacos9-store
usr/bin/zacos9-update
usr/lib/systemd/system/plymouth-quit-wait.service.d/zacos9.conf
usr/lib/systemd/system/plymouth-quit.service.d/zacos9.conf
usr/lib/systemd/system/zacos9-boot-handoff.service
usr/lib/systemd/system/zacos9-parade.service
usr/lib/systemd/user/zacos9-update-check.service
usr/lib/systemd/user/zacos9-update-check.timer
usr/lib/zacos9/setos.efi
usr/libexec/zacos9/zacos9-appstore-helper
usr/libexec/zacos9/zacos9-appstyle
usr/libexec/zacos9/zacos9-boot-handoff
usr/libexec/zacos9/zacos9-bootlogo
usr/libexec/zacos9/zacos9-install
usr/libexec/zacos9/zacos9-parade
usr/libexec/zacos9/zacos9-release-disk
usr/libexec/zacos9/zacos9-rotate-boot
usr/libexec/zacos9/zacos9-setup-helper
usr/libexec/zacos9/zacos9-software-catalog
usr/libexec/zacos9/zacos9-startupdisk-helper
usr/share/applications/zacos9-appinstall.desktop
usr/share/applications/zacos9-installer.desktop
usr/share/applications/zacos9-setup.desktop
usr/share/applications/zacos9-startupdisk.desktop
usr/share/applications/zacos9-store.desktop
usr/share/applications/zacos9-update.desktop
usr/share/plymouth/themes/zacos9/happy-zac-1x.png
usr/share/plymouth/themes/zacos9/happy-zac-2x.png
usr/share/plymouth/themes/zacos9/happy-zac-3x.png
usr/share/plymouth/themes/zacos9/happy-zac-4x.png
usr/share/plymouth/themes/zacos9/happy-zac-5x.png
usr/share/plymouth/themes/zacos9/happy-zac-6x.png
usr/share/plymouth/themes/zacos9/parade.png
usr/share/plymouth/themes/zacos9/pattern.png
usr/share/plymouth/themes/zacos9/progress-box.png
usr/share/plymouth/themes/zacos9/progress-fill.png
usr/share/plymouth/themes/zacos9/welcome-1x.png
usr/share/plymouth/themes/zacos9/welcome-2x.png
usr/share/plymouth/themes/zacos9/welcome-3x.png
usr/share/plymouth/themes/zacos9/welcome-4x.png
usr/share/plymouth/themes/zacos9/welcome-5x.png
usr/share/plymouth/themes/zacos9/welcome-6x.png
usr/share/plymouth/themes/zacos9/zacos9.plymouth
usr/share/plymouth/themes/zacos9/zacos9.script
usr/share/polkit-1/actions/org.zacos9.appstore.policy
usr/share/polkit-1/actions/org.zacos9.installer.policy
usr/share/polkit-1/actions/org.zacos9.setup.policy
usr/share/polkit-1/actions/org.zacos9.startupdisk.policy
usr/share/polkit-1/rules.d/50-zacos9-appstore.rules
usr/share/polkit-1/rules.d/50-zacos9-installer.rules
usr/share/polkit-1/rules.d/50-zacos9-setup.rules
usr/share/polkit-1/rules.d/50-zacos9-startupdisk.rules
usr/share/zacos9/store/catalog.json
usr/share/zacos9/whatsnew/1.11.0.txt
usr/share/zacos9/whatsnew/1.12.0.txt
usr/share/zacos9/whatsnew/1.13.0.txt
usr/share/zacos9/whatsnew/1.14.0.txt
usr/share/zacos9/whatsnew/1.14.1.txt
usr/share/zacos9/whatsnew/1.14.2.txt
usr/share/zacos9/whatsnew/1.14.3.txt
usr/share/zacos9/whatsnew/1.18.0.txt
usr/share/zacos9/whatsnew/1.18.1.txt
usr/share/zacos9/whatsnew/1.18.2.txt
usr/share/zacos9/whatsnew/1.18.3.txt
usr/share/zacos9/whatsnew/1.18.4.txt
usr/share/zacos9/whatsnew/1.18.5.txt
usr/share/zacos9/whatsnew/1.18.6.txt
usr/share/zacos9/whatsnew/1.18.7.txt
usr/share/zacos9/whatsnew/1.18.8.txt
usr/share/zacos9/whatsnew/1.18.9.txt
usr/share/zacos9/whatsnew/1.18.10.txt
usr/share/zacos9/whatsnew/1.18.11.txt
usr/share/zacos9/whatsnew/1.18.12.txt
usr/share/zacos9/whatsnew/1.18.13.txt
usr/share/zacos9/whatsnew/1.18.14.txt
usr/share/zacos9/whatsnew/1.18.15.txt
usr/share/zacos9/whatsnew/1.18.16.txt
usr/share/zacos9/whatsnew/1.18.17.txt
usr/share/zacos9/whatsnew/1.18.18.txt
usr/share/zacos9/whatsnew/1.18.20.txt
usr/share/zacos9/whatsnew/1.7.0.txt
usr/share/zacos9/whatsnew/1.7.1.txt
usr/share/zacos9/whatsnew/1.7.2.txt
usr/share/zacos9/whatsnew/1.7.3.txt
usr/share/zacos9/whatsnew/1.7.4.txt
usr/share/zacos9/whatsnew/1.7.5.txt
usr/share/zacos9/whatsnew/1.7.6.txt
usr/share/zacos9/whatsnew/1.8.0.txt
etc/grub.d/09_zacos9
etc/grub.d/31_zacos9_haiku
etc/xdg/autostart/zacos9-whatsnew.desktop
```

The groups are:

- **Boot:** Plymouth theme and drop-ins, GRUB scripts, boot handoff, parade,
  boot logo and rotation.
- **Installer and startup disk:** installer, release-disk helper, Startup Disk
  helper, `setos.efi`, polkit, desktop entries.
- **Setup Assistant.**
- **Software/Update:** store, update, app install, appstore helper, catalog,
  appstyle, update timer, What's New, polkit, desktop entries.

`usr/share/zacos9/boot` does **not** move. The compositor uses it for the
startup Welcome screen; only Plymouth's `usr/share/plymouth/themes/zacos9`
moves.

## The greeter package

The greeter is separate so TTOS can pair `greetd` with the ZacOS login window
without installing the boot and installer integration:

```text
usr/libexec/zacos9/zacos9-greeter
usr/libexec/zacos9/zacos9-login
```

It depends on `zacos9-session` (for `zacos9-wm` and the startup art), `greetd`
and `seatd`. It also depends on `tuigreet`, because `zacos9-greeter` falls
back to the text login when the graphical window cannot start; without it a
broken install can lock the user out. Its `postinst` runs `greeter_seat`.

TTOS's Calamares module would install `zacos9-session`, `zacos9-greeter`,
`greetd` and `seatd`, and point greetd at `zacos9-greeter`. TTOS can also use
its own greeter instead; the session does not depend on this package.

## What stays in `zacos9-session`

Everything else. In practice:

- `usr/bin/` except the five moved programs.
- `usr/libexec/zacos9/` except the moved helpers. The accounts, disks, memory,
  modules, sharing and hardware helpers stay, because the control panels and
  the Finder call them.
- `usr/lib/systemd/system/` except the Plymouth and boot units. Wakealarm,
  ramdisk and GPE guard stay.
- `usr/lib/systemd/user/` except the update check. The HDMI watchdog stays.
- `usr/share/applications/` except the moved entries.
- `usr/share/polkit-1/` except installer, setup, startup disk and appstore.
- `usr/share/zacos9/` except `store/` and `whatsnew/`; **`boot/` stays**.
- Icons, themes, fonts, sounds, help, translations, search sites and tools.
- The session file, session scripts, portals, PAM files, GTK/Qt modules and
  hardware helpers.

The session still includes Disk Setup. It is user-initiated, uses UDisks2 and
does not touch the boot loader. Startup Disk moves because it changes the boot
disk.

## Post-install scripts

`zacos9-session.postinst`:

- `grant_rawio` for the optional SheepShaver;
- `guest_seat` (the Users panel's Guest account);
- nothing else.

`zacos9-session.triggers`:

```text
interest-noawait /usr/libexec/zacos9/SheepShaver
```

`zacos9-greeter.postinst`:

- `greeter_seat` (add `_greetd` to `video` and `render`);
- nothing else.

`zacos9.postinst`:

- `zram_off_by_default`, `redraw_grub_logo`, `rotate_boot`,
  `make_shared_documents`, `japanese_setup`, `remove_rdp_sharing`;
- enables `zacos9-update-check.timer`.

`zacos9.triggers`:

```text
activate-noawait update-initramfs
```

The session package must never write `/etc/systemd/zram-generator.conf`,
create `/home/Shared`, write `/etc/grub.d`, run `update-grub`, or enable the
update timer. Those are the changes that would surprise a TTOS user.

## Dependencies

Session: the current `Depends` minus `flatpak` (Software), `os-prober`
(installer and Startup Disk) and `gnupg` (Software and Update).

Greeter: `zacos9-session (= ${binary:Version})`, `greetd`, `seatd` and
`tuigreet`.

Full: `zacos9-session (= ${binary:Version})`, `zacos9-greeter
(= ${binary:Version})`, `flatpak`, `os-prober`, `gnupg` and `plymouth`.
`plymouth` is not a runtime dependency today; the ISO supplies it. Add
`grub2-common` and `efibootmgr` if the installer and Startup Disk are expected
to work from an installed system; the ISO currently supplies them.

The `Recommends` need the same review: `seatd`, `greetd` and `tuigreet` belong
to the greeter package; `balena-etcher` belongs to the full package; the
printing, sharing, media and portal recommendations stay in the session.

Open dependency questions:

- `earlyoom` is a system daemon; keep it in the session or move it to the full
  package?
- `network-manager-config-connectivity-debian` is Debian-specific; does TTOS
  want it?
- Does TTOS's greetd setup use `seatd`, or does its own seat management make
  the greeter package's `seatd` dependency unnecessary?

## Menu and Finder touchpoints

The session package removes programs the desktop still references:

- `shell/menubar/launch.c:578` adds **New Tricks** (`zacos9-store`);
  `:584` adds **Fetch Updates** (`zacos9-update`). Both are unconditional.
- `shell/finder/desktop.cpp:183` and `shell/finder/finder.cpp:832` offer
  **Open Software** when VLC is missing.
- `shell/finder/fileops.cpp:840` sends files dropped on Applications to
  `zacos9-appinstall`.

Recommendation: add a small `programExists()` check, or a `session-only`
setting, and skip the menu item or the Software button when the program is
absent. `zacos9-accounts-helper` already guards `zacos9-rotate-boot` with
`-x`, so display rotation is safe.

Also exclude the moved `.desktop` entries so the Applications folder does not
show programs that are not installed.

## Packaging

- Split `debian/control` into `zacos9-session`, `zacos9-greeter` and
  `zacos9`; the full package depends on the exact session and greeter
  versions.
- `zacos9-session` and `zacos9-greeter` need `Breaks` and `Replaces` on the
  old single `zacos9` package (below the first split version), because they
  take files the old package owned. Without them, an upgrade from 1.18.x can
  fail with a file conflict.
- Add `debian/zacos9-session.install`, `debian/zacos9-greeter.install` and
  `debian/zacos9.install`. Together they must cover
  `tests/baseline/installed-files.txt` exactly once. Compat 13's `dh_missing`
  fails on anything unassigned.
- Keep `tests/baseline/installed-files.txt` as the union. Add
  `tests/baseline/test_package_split.py` to check the three lists partition
  it.
- Update `tests/baseline/test_packaging.py`: it currently assumes one
  `Package: zacos9` stanza and looks for `_greetd` in
  `debian/zacos9.postinst`. That check moves to
  `debian/zacos9-greeter.postinst`.
- `debian/rules`: the `dh_installsystemd` exclusions apply to both packages;
  make the package explicit if needed.
- `scripts/build-debs.sh` already copies `zacos9*.deb`, so both packages land
  in `build/packages`.
- `scripts/release.sh` attaches the desktop packages with
  `zacos9*_${version}_${arch}.deb`, and the emulators and their source
  separately.
- `zacos9-update` (`shell/update`) downloads one release asset and installs
  it. It must install both packages, or an upgrade leaves `zacos9-session`
  behind. The test fixtures and `docs/updates.md` assume one package too.
- The install commands in `README.md` and `docs/sourceforge-readme.md` use
  `zacos9_*_amd64.deb`, which will not match `zacos9-session_*_amd64.deb`.
  Change them to `zacos9*_amd64.deb` or list both.
- Update `docs/packaging.md` and the package descriptions.

The session list can be generated from `tests/baseline/installed-files.txt` by
removing the 90 paths above (the baseline's `{prefix}` and `{sysconfdir}`
become `usr` and `etc`). Keeping that generator or a partition test is the
safest way to stop a new file from landing in the wrong package.

`dh_install` splits its file lists on whitespace, so a path with a space uses
`?` in the list: `usr/share/zacos9/Internet?Search?Sites` and
`usr/share/zacos9/themes/Silver?Coat?Dark`. `test_package_split.py` expands
those against the baseline.

## Still to do before the split is released

`zacos9-update` (`shell/update`) downloads one release asset and installs it.
A full ZacOS system upgrading from 1.18.27 needs all three packages, so the
updater must download `zacos9-session`, `zacos9-greeter` and `zacos9` and
install them in one transaction (or session, greeter, full) before the split
is released. TTOS installs them from the repository, so it is not affected.

## Tests

1. `dpkg-deb -c` both packages: the union equals the baseline, the
   intersection is empty.
2. Install `zacos9-session` on a clean Debian 13 with GDM and SDDM:
   - the session appears and starts;
   - the Finder, menu bar, Collar and control panels work;
   - no GRUB, Plymouth, zram, `/home/Shared` or update-timer changes.
3. Install `zacos9-session` and `zacos9-greeter` with `greetd` and `seatd`:
   - the ZacOS login window appears;
   - a login starts the session;
   - the `tuigreet` fallback works with the graphical window disabled.
4. Install `zacos9` on ZacOS: the boot splash, GRUB logo, Setup Assistant,
   Software, Update and Startup Disk behave as before.
5. Repeat the session install on TTOS with TurboDPKG when it is available.

## Open questions

- Should Store and Update stay out of the session and be hidden, or be adapted
  to Turbo later?
- Should Classic, Wine, printing and screen sharing be separate extras for a
  smaller net install?
- Is `zacos9-session` the name Tom expects, or `zacos9-desktop`?
- Does TTOS want the ZacOS login window (`zacos9-greeter`) or one of its own
  greeters? The `qmlgreet`/QtStep route is a TTOS-level choice, not a ZacOS
  dependency.
- Does TTOS want Disk Setup in the session? This proposal keeps it.
- Does TTOS want the full `zacos9` package at all, or only the session and
  greeter?
