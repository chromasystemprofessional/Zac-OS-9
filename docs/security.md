# Security hardening

This page is for people changing ZacOS 9's C and C++ code: the sanitizer
build, the fuzzer, and the dependency issues we are tracking. The reporting
policy is in [SECURITY.md](../SECURITY.md).

## Sanitizer build

Build the desktop with AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
meson setup build-asan -Db_sanitize=address,undefined -Dafterdark=disabled
meson compile -C build-asan
meson test -C build-asan afp-parse --print-errorlogs
```

CI compiles the desktop this way and runs the AFP parser test. Other tests
can be run from `build-asan` the same way; tests that start their own
compositor also run under the sanitizer. `ASAN_OPTIONS=detect_leaks=0` is
set in CI because Qt, Mesa and the test harness keep allocations alive at
exit; the parser test has no leaks of its own.

## Fuzzing

The AFP client parses bytes from a server, so its reply parsers are pure
functions in `network/afp/afp_parse.c` and have a libFuzzer harness:

```sh
sudo apt install clang libclang-rt-19-dev
CC=clang CXX=clang++ meson setup build-fuzz -Dfuzz=true -Dafterdark=disabled
meson test -C build-fuzz fuzz-afp-parse --print-errorlogs
```

The CI test runs 2,000 inputs. For a longer local run, run the harness
directly against a corpus directory or with a time limit:

```sh
build-fuzz/network/fuzz-afp-parse -max_total_time=3600
```

The harness feeds the same bytes to the server-info, item-parameter and
enumerate parsers. Add the failing bytes to `test_afp_parse.c` when a crash
is fixed; the deterministic `afp-parse` test is the fast gate that runs in
the normal package build.

## Classic emulation

Classic Mac OS has no memory protection, cooperative multitasking and an
old network stack. `zacos9-classic` runs SheepShaver and Basilisk II in a
bubblewrap sandbox by default: the `Classic` folder is the Mac's Unix
volume (read-write), the rest of the home is hidden, the emulator gets a
private `HOME`, and the guest has no network or GPU unless `--network` or
`--gpu` is given. `--unsandboxed` runs without bubblewrap (network on by
default); `classic.conf` accepts `sandbox=`, `network=` and `gpu=`. The
`classic-sandbox` test checks that a guest cannot read `~/.ssh` or the
desktop's settings.

## Root helpers

The privileged helpers under `system/` are shell scripts. The
`system-shellcheck` test runs ShellCheck over every shell helper with
warnings treated as errors; it is part of the normal package test suite.
`shellcheck` is a build dependency, so `apt build-dep ./` installs it;
run the test on its own with:

```sh
meson test -C build system-shellcheck --print-errorlogs
```

The Python helpers (disks, APFS, modules) are out of scope for ShellCheck.
Argument-level tests for each helper are still to come.

## Release integrity

Every release publishes a CycloneDX 1.6 SBOM, `zacos9-<version>.cdx.json`,
and a `SHA256SUMS` file with a detached signature, `SHA256SUMS.asc`, made
with the same archive key as the package repository
([docs/repo.md](repo.md)). The SBOM lists each package's name, version,
architecture, SHA-256 and Debian `Depends`, plus the emulators' GPL source
tarball. It inventories what ZacOS 9 publishes, not the whole Debian base
system.

`scripts/release.sh` writes the SBOM from the packages it built and signs
the checksums; the `repo` CI job signs the `v*` release assets again from
the tested artifacts. The `sbom` test checks the generator against sample
packages, including that a given `SOURCE_DATE_EPOCH` produces the same
document.

Verify a release before installing it; `SHA256SUMS` lists the packages, the
SBOM and the emulators' source. Import the archive key first if you haven't:
the repository publishes it as `zacos9-archive-keyring.asc`
([docs/repo.md](repo.md)).

```sh
gh release download TAG --pattern 'SHA256SUMS*'
gpg --verify SHA256SUMS.asc SHA256SUMS
gh release download TAG --pattern '*.deb' --pattern '*.cdx.json'
sha256sum -c SHA256SUMS
```

## Reproducible builds

The desktop packages are reproducible: two builds of the same tree, in
different directories, produce identical `.deb` files, including the
debug-symbol packages. The changelog entry sets `SOURCE_DATE_EPOCH`,
`dpkg-buildflags` maps the build path, and `debian/rules` compiles a fixed
source path (`-Dsource-dir=/usr/share/zacos9`) instead of the checkout. A
build-tree run still finds its assets through `ZACOS9_SOURCE`.

Verify locally; it needs the build dependencies and takes two full builds:

```sh
sudo apt build-dep ./
scripts/check-reproducible.sh
```

CI runs the same check every week and on release tags. The emulators' package
and GPL source tarball are not covered yet.

## Known dependency issues

### CVE-2026-121604: wlroots Xwayland `WM_CLASS` over-read

`read_surface_class()` in wlroots' Xwayland window manager can over-read
the compositor's heap when an X11 client sets a `WM_CLASS` with a
NUL-terminated instance but an unterminated class. The over-read bytes can
be exposed as the window's class or app ID over IPC. It is fixed in
wlroots 0.20.2 (commit `4412992469540b1d578cb94a5cd4e6e5829812dc`).

Debian 13 (trixie) ships wlroots 0.18.2, which is affected; Debian 14
(forky/sid) has 0.20.2. As of 2026-11-27 there is no trixie security
update. ZacOS 9 on Debian 13 therefore inherits the issue in its Xwayland
path.

Until the Debian 14/wlroots 0.20 rebase, treat X11 programs as less
isolated than Wayland programs and do not run untrusted X11 clients on a
desktop that holds data you would not show them. This is an upstream
wlroots issue; [SECURITY.md](../SECURITY.md) explains what is in scope for
ZacOS 9 itself. Track the Debian security tracker and take the trixie
update when it lands.

### CVE-2026-111133: libfsapfs encrypted-key over-read

`libfsapfs` through 20260921 has an integer underflow in
`libfsapfs_key_encrypted_key_read_data()`: a crafted APFS container with a
malformed KEK attribute makes it read past a heap buffer. Debian 13 ships
`libfsapfs` 20240429-1+b1; as of 2026-11-28 the Debian security tracker
still lists trixie as undetermined. The upstream fix is a bounds check in
`libfsapfs_key_encrypted_key.c`.

`zacos9-mac-mount` opens an APFS device as root through `libfsapfs` to list
its volumes before the FUSE mounts start, so a crafted image reaches the
parser. The volume list is now read in a short-lived child with a 30-second
timeout, no core dumps, and memory, CPU and file-size limits: a crash or a
hang in the library no longer takes down the root helper. That contains the
denial of service but does not fix the over-read. The child returns volume
names or an error and then exits, but until Debian ships the update, do not
mount an APFS image from an untrusted source, and take the trixie update
when it lands.

## What is not covered yet

- The HFS and HFS+ readers are Python helpers around Debian libraries; the
  APFS reader has malformed-image tests, and fuzzing all of them remains a
  target.
- Wayland protocol parsing is wlroots' code; report those issues upstream.
- The root helpers pass ShellCheck, but argument-level tests for each
  helper are still to come.
