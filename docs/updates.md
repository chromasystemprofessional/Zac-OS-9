# Software Update

**Software Update** (Apple menu > **Fetch Updates**; `zacos9-update`, `shell/update/`) keeps
an installed ZacOS 9 current. It covers two things in one window:

- **New ZacOS 9 releases.** These are GitHub releases of
  `chromasystemprofessional/Zac-OS-9` with the `zacos9_VERSION_ARCH.deb`
  attached.
- **Debian's own updates.** These are security fixes, Firefox and anything
  else apt knows about, including third-party apt sources such as VS Code's.

## What it does

1. **Check.**
   - It asks GitHub's API for the latest release
     (`/repos/OWNER/REPO/releases/latest`). It reads the tag (`v0.2.0` means
     version `0.2.0`) and takes the `.deb` for this machine's architecture.
     The installed `zacos9` is compared with it using `dpkg --compare-versions`.
   - A repository with no releases yet (GitHub answers 404) counts as up to
     date.
   - Meanwhile it runs `zacos9-appstore-helper refresh` (`apt-get update`, as
     root) and then `apt-get -s --with-new-pkgs upgrade` as the user. The
     second is a simulation whose `Inst` lines are the Debian updates.
2. **Show.** The window says "Updates are available" and lists the ZacOS
   version and the Debian package count, or says the software is up to date.
3. **Update.**
   - It downloads the `.deb` into `~/.cache/zacos9/updates` and checks it:
     - the SHA-256 against the asset's `digest`, when GitHub gives one;
     - that its `Package` is `zacos9` and its `Version` is the release's.
   - It installs the `.deb` with the helper's `install-deb`, then Debian's
     updates with `upgrade`. That command is `apt-get upgrade --with-new-pkgs`
     with `--force-confdef --force-confold`: it never removes packages and
     keeps configuration files the user changed.
   - The progress bar follows apt's `APT::Status-Fd` lines.
4. **Finish.** After a ZacOS update it offers **Restart**, because the running
   desktop keeps the old programs until then.

The helper runs through `pkexec`. Polkit's `50-zacos9-appstore.rules` lets
administrators at the computer do this without a password, the same as the
Software window. Other users can still check; apt-get update and the install
fail for them with a message.

Settings for tests and forks:

- `ZACOS9_UPDATE_REPO` sets the repository (`owner/name`).
- `ZACOS9_UPDATE_URL` sets the whole release URL. A `file://` URL works for
  trying the window without the network.

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
scripts/release.sh 0.2.0 "What's new, in a sentence or two."
```

The script only runs on a clean `main` branch that matches `origin/main`.
It does the following:

1. Adds a `debian/changelog` entry; the package version comes from there.
2. Commits it and tags `v0.2.0`.
3. Builds the package with `scripts/build-debs.sh`.
4. Pushes the commit and the tag.
5. Runs `gh release create` with the `.deb` attached.

Every installed system then sees the release the next time Software Update
checks.

The version must be newer than the last changelog entry. Use `~` for
pre-releases (`0.2.0~rc1` sorts before `0.2.0`).

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
- **No release notes in the window.** The release text is read but not shown.
- **No per-package choice.** Everything found is installed together.
- **No `full-upgrade`.** An update that needs a package removed is held back,
  as `apt-get upgrade` does.
