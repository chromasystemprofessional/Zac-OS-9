# Debian 14 and wlroots

ZacOS 9 builds on Debian 13 (trixie) against **wlroots 0.18**. Debian 14
(forky) ships **wlroots 0.20**, and the 0.18 packages are not in forky. This
page records the options; it is a plan, not a finished port.

## What breaks

- `debian/control` asks for `libwlroots-0.18-dev`, which forky does not have.
- The built `zacos9-session` depends on `libwlroots-0.18` through its shared
  library dependencies; forky provides `libwlroots-0.20`.

Nothing else in the desktop uses wlroots directly: the compositor is the
consumer. It uses 383 distinct `wlr_*` symbols across 34 files under
`compositor/src/`.

## Short term: a supported 0.18 compatibility package

Tom's TTOS build compiles wlroots 0.18 against forky and puts a copy on the
ISO. That works, but it should become a proper package:

- Build `libwlroots-0.18` (and `-dev`) against forky libraries, in a private
  versioned package. It must not replace or conflict with forky's
  `libwlroots-0.20`; other sessions keep the distro's 0.20.
- Publish it in a `forky` repository suite beside `zacos9`.
- Build `zacos9-session`/`zacos9-greeter` against it for forky.
- Maintain the 0.18 source and its security fixes ourselves until the port is
  done.

This is the shortest route to a working TTOS install. Ask Tom for the exact
packaging (private prefix, rpath, package names) he used so the repository
package matches it.

## Long term: port the compositor to 0.20

The clean answer is the 0.20 port. The compositor is the only wlroots
consumer, but the API surface is wide: output layout, renderers, scene graph,
input, layer shell, XWayland and the backend all use `wlr_*` types. Do it in
its own branch, with the Debian 14 survey workflow as the build gate, and
keep the Debian 13 build green while it lands.

Do not publish a forky suite until the session starts and the compositor's
tests pass there.

## The survey workflow

`.github/workflows/debian-14.yml` runs manually in a `debian:testing`
container. It records:

- which `libwlroots*` packages forky offers;
- the missing `dpkg-checkbuilddeps` entries;
- the `apt-get build-dep` failure.

That log is the starting point for either the compatibility package or the
port. It is not part of the PR gate.
