# Software Update

**Software Update** (Apple menu > **Fetch Updates**; `zacos9-update`, `shell/update/`) keeps
an installed ZacOS 9 current. It covers three things in one window:

- **New ZacOS 9 releases.** These are GitHub releases of
  `chromasystemprofessional/Zac-OS-9` with the `zacos9_VERSION_ARCH.deb`
  attached.
- **Debian's own updates.** These are security fixes, Firefox and anything
  else apt knows about, including third-party apt sources such as VS Code's.
- **New versions of ZacOS Extras.** Extras you installed from Software's
  Extras page (Steam, Game Player, Hue Lights and the rest) are GitHub
  releases of `chromasystemprofessional/ZacOS-Extras`, tagged
  `EXTRA-vVERSION`. An extra may name its own repository and tag prefix in
  the catalog (Netzac does), and Software Update asks that repository too.

## What it does

1. **Check.**
   - It asks GitHub's API for the latest release
     (`/repos/OWNER/REPO/releases/latest`). It reads the tag (`v0.2.0` means
     version `0.2.0`) and takes the packages for this machine's
     architecture: `zacos9`, and `zacos9-session` and `zacos9-greeter` when
     the release has them. The installed `zacos9` is compared with it using
     `dpkg --compare-versions`.
   - A repository with no releases yet (GitHub answers 404) counts as up to
     date.
   - It asks each installed extra's release repository once, and compares
     its `dpkg` version with the newest release the catalog still offers.
     An extra removed from the catalog is not updated.
   - Meanwhile it runs `zacos9-appstore-helper refresh` (`apt-get update`, as
     root) and then `apt-get -s --with-new-pkgs upgrade` as the user. The
     second is a simulation whose `Inst` lines are the Debian updates.
2. **Show.** The window says "Updates are available" and lists the ZacOS
   version, the extras and their versions, and the Debian package count, or
   says the software is up to date.
3. **Update.**
   - It downloads the release's packages into `~/.cache/zacos9/updates` and
     checks each:
     - the SHA-256 against the asset's `digest`, when GitHub gives one;
     - that the `zacos9` package's `Package` is `zacos9` and its `Version`
       is the release's.
   - It installs them together with the helper's `install-deb`, so the
     `zacos9` metapackage and the `zacos9-session` and `zacos9-greeter`
     packages it needs arrive in one apt transaction. This does not wait
     for the signed repository to catch up with a new release. Each updated
     extra's `.deb` is downloaded the same way (checksum and package/version
     checked) and installed with `install-deb`; the extra's program is then
     stopped and started again so it uses the new version, and its catalog
     Desktop alias is put back if it is missing.
   - Then it installs Debian's updates with `upgrade`. That command is
     `apt-get upgrade --with-new-pkgs` with `--force-confdef --force-confold`:
     it never removes packages and keeps configuration files the user changed.
   - The progress bar follows apt's `APT::Status-Fd` lines.
4. **Finish.** After a ZacOS update it offers **Restart**, because the running
   desktop keeps the old programs until then.

The helper runs through `pkexec`. Polkit's `50-zacos9-appstore.rules` lets
administrators at the computer run apt (refresh and upgrade) without a
password, the same as the Software window. Installing a downloaded `.deb`
— ZacOS or an extra — asks for one, because its setup scripts run as root.
Other users can still check; apt-get update and the install fail for them
with a message.

**What's New.** When the newest release is newer than what's installed,
the update window also offers **What's New…**, which shows the release's
notes (the release text GitHub returns, already fetched for the check)
in a scrolling window.

**After restarting.** A system autostart entry runs
`zacos9-update --whatsnew` at every login. It compares the installed
`zacos9` version with `~/.config/zacos9/whatsnew-seen` and, when the
installed version is newer *and* the package ships notes for it
(`/usr/share/zacos9/whatsnew/<version>.txt`), shows them once — with
buttons that open Theme Files and the Appearance panel — then records
the version as seen. Turning the "What's New in ZacOS 9" entry off in
Startup Items turns the popup off.

Settings for tests and forks:

- `ZACOS9_UPDATE_REPO` sets the repository (`owner/name`).
- `ZACOS9_UPDATE_URL` sets the whole release URL. A `file://` URL works for
  trying the window without the network.
- `ZACOS9_EXTRAS_URL` sets the official extras releases URL; a `file://`
  URL works for trying extras updates without the network. It does not
  replace a third-party extra's own repository URL.
- `ZACOS9_WHATSNEW_DIR` sets where `--whatsnew` looks for `<version>.txt`
  notes, for tests.

## Updating from the installer

The installer on a ZacOS 9 ISO can also update a computer that already has
ZacOS 9, for example one without a network connection, or an older system
that predates Software Update.

- **Choosing the disk.** When the installer lists disks, it runs
  `zacos9-install --detect`. That mounts every ext4 partition read-only
  (`ro,noload`, so not even the journal is replayed) and reads `zacos9`'s
  version from dpkg.
- **The buttons.** A disk with ZacOS 9 shows its version in the list.
  Selecting it offers **Update** as the default button (**Reinstall** at the
  same version), with **Erase & Install** beside it. Update isn't offered
  when the disk has a newer version than the installer.
- **What Update does.** `zacos9-install --update PARTITION`:
  1. Mounts the system, plus its `/boot/efi` from its own fstab.
  2. Lends it this session's name servers.
  3. Runs `apt-get update`.
  4. Installs the `.deb` files from the medium's `zacos9/` folder with apt
     (`--force-confold`), which also brings in new dependencies.
  5. Installs Debian's pending updates (`upgrade --with-new-pkgs`).
  6. Runs `update-grub`.

  Accounts, home folders, settings, the disk's name, the fstab and the boot
  loader are left alone.
- **Offline.** It still works without a network connection, unless the new
  version needs packages the old system lacks. In that case it says to
  connect and try again.
- **The packages on the medium.** `scripts/build-iso.sh` copies the
  packages into the ISO's `zacos9/` folder (`config/includes.binary`).
- **Trying the window out.** `ZACOS9_INSTALL_HELPER=/path/to/script` runs
  that script directly, in place of `pkexec zacos9-install`.

## Making a release

```sh
gh auth login                     # once
scripts/release.sh [--transition|--split] 0.2.0 "What's new, in a sentence or two."
```

The second argument becomes the release's notes on GitHub, which Software
Update shows under **What's New…**. Keep it in sync with the matching
`shell/update/whatsnew/<version>.txt` file in the package (that file is
what users see after restarting); both should say what changed and, for a
feature like theme drops, how to do it.

The script only runs on a clean `main` branch that matches `origin/main`.
It does the following:

1. Adds a `debian/changelog` entry; the package version comes from there.
   It also moves the split packages' `Breaks`/`Replaces: zacos9 (<< …)` to
   this version, so a repository install can take the files back from the
   previous monolith.
2. Commits both files and tags the commit: `0.2.0` for a transition release
   (the default), or `v0.2.0` for a split release.
3. Builds the split packages with `scripts/build-debs.sh`.
4. For a transition release, builds the monolith from those packages with
   `scripts/build-transition-package.py`, writes `SHA256SUMS`, and attaches
   only `zacos9_0.2.0_amd64.deb` and `SHA256SUMS` to the GitHub release. The
   split packages are left in `build/packages` for the signed repository.
   For a split release, attaches the split package set.
5. Pushes the commit and the tag, and runs `gh release create`.

Transition is the default because a computer that skipped an earlier
transition release still has an updater older than 1.18.30; the old updater
always fetches the latest release. Pass `--split` only once those updaters
are no longer supported.

Every installed system then sees the release the next time Software Update
checks. Software Update downloads the release's packages itself, so it can
install a new release before the signed repository has republished it.

The version must be newer than the last changelog entry. Use `~` for
pre-releases (`0.2.0~rc1` sorts before `0.2.0`).

## Transition releases

The 1.18.28 split made `zacos9` a metapackage that needs `zacos9-session`
and `zacos9-greeter`. The updater on 1.18.27 and 1.18.29 downloads only the
metapackage, so those computers cannot install a newer release without the
signed repository; the fixed updater is in 1.18.30.

1.18.31 onward are transition releases: the GitHub release asset is a
single monolith built from that release's split packages, with their files
and maintainer scripts and no dependency on `zacos9-session` or
`zacos9-greeter`. It carries the fixed updater, so an old updater can install
it with no repository.

A computer that skips one transition release never installs it, and the old
updater always fetches the latest release. Until pre-1.18.30 updaters are no
longer supported, every GitHub release needs the same monolith. That is why
`scripts/release.sh` defaults to a transition release; `--split` is the
opt-out for when those updaters are gone.

The script builds the monolith from the split packages of the same version:

```sh
scripts/build-transition-package.py --version 1.18.33 \
    --base-version 1.18.33 --debs build/packages --output build/transition
```

It publishes only `zacos9_1.18.33_amd64.deb` and `SHA256SUMS` in that GitHub
release. Do not attach `zacos9-session` or `zacos9-greeter` there: the fixed
updater downloads the whole set, which would conflict with the monolith.
Publish the split packages to the signed repository instead.

The release tag is the version without the leading `v` (`1.18.33`), so the
normal `v*` tag workflow does not build split packages over the transition.
The source tree stays split.

When a release returns to the split GitHub layout, its split packages must
carry `Breaks`/`Replaces: zacos9 (<< that version)` to take the files back
from the last monolith. The current 1.18.33 split packages use
`<< 1.18.33`, so a repository install can replace the 1.18.32 monolith.

## Tests

`meson test -C build update-check` (`shell/update/tests/`) tests the parts
that work without the network or root:

- picking the right asset out of a release reply;
- refusing tags that aren't Debian versions, and assets not served over https;
- reading apt's simulation;
- version order.

Installing has only been tried by hand.

## Not done

- **No automatic checks.** It only checks when it is opened from the Apple
  menu.
- **No per-package choice.** Everything found is installed together.
- **No `full-upgrade`.** An update that needs a package removed is held back,
  as `apt-get upgrade` does.
