# Screen Sharing

## ZacOS to ZacOS

The Screen Sharing control panel shares this screen with other ZacOS
computers, and its Connect tab shows theirs (`shell/panels/screensharing.cpp`,
`shell/screenshare/`). The host is `zacos9-screenshare-host`: wayvnc on a
private socket, an Allow/Deny alert per computer, Bonjour `_rfb._tcp` with
`zacos=1`. The viewer is `zacos9-screenshare-viewer`.

### Switching between the two computers

The viewer opens full screen. A bar at the top of the screen shows for a
few seconds, then goes away. It comes back when you rest the pointer
against the top edge of the screen. It has:

- **Use My Desktop**: puts the other screen in a window, so your own
  desktop is there beside it. In the window, the same bar sits along the
  top, with **Full Screen** to go back.
- **Sound**: plays the other computer's sound here, or stops it.
- **Disconnect**.

Control-Option-Escape also switches between full screen and the window.
While the viewer is the active window, the mouse and keys go to the other
computer. Click your own desktop or another window to use this one.

### Sound

The host's panel has **Let them hear this computer's sound** (on by
default). The host records its default output (`parec`, the output's
monitor) and sends it to the viewer, which plays it with `pacat`.
The host's own speakers keep playing too.

The screen's connection is TLS from end to end between the viewer and
wayvnc, so the host can't add anything to it. The sound has its own
connection, on the screen's port plus one (5901). It uses TLS with the
same certificate, which the viewer checks against the screen's
fingerprint. The viewer logs in with the same name and password as for
the screen, and the host checks them with PAM (`/etc/pam.d/zacos9-screenshare`).
The host answers only a computer that already has a live screen
connection, which the owner has let in. When that connection goes, its
sound goes too. The protocol is in `shell/screenshare/common.h`.

The sound is 48 kHz, 16-bit stereo, uncompressed: about 1.5 Mbit/s,
meant for a local network. The sender drops what it can't send in time,
and so does the player, so the sound stays in step with the screen
instead of falling behind.

Tests: `screenshare-host` checks the sound's login, the samples, the
viewer's `SoundClient`, and the sound going with its screen connection.
`screenshare-viewer` checks the bar, Use My Desktop and
Control-Option-Escape.

## RustDesk (earlier)

Incoming RustDesk sharing produced a black screen on the initial live system.
The current direction is [RDP](rdp.md), with an opt-in current-desktop bridge.
Local RDP video and keyboard work; the multi-monitor pointer fix needs a
desktop restart before full live input testing.
[Sunshine and Moonlight](sunshine-moonlight.md) describes the earlier streaming
alternative, whose host is now stopped. The notes below describe the
earlier experimental RustDesk integration, not verified working hosting.

ZacOS uses the existing [RustDesk](https://rustdesk.com/) application for
cross-platform remote desktop, rather than maintaining a remote-desktop engine.
Install the native Debian package on ZacOS and RustDesk on the other computer
(Windows, macOS or Linux). RustDesk's own window provides connections,
permissions, clipboard and file transfer.

## Installation

RustDesk is optional and is not bundled in ZacOS or available as a Debian
package in the curated Software catalog. Download the standard `.deb` for your
architecture from the [official releases](https://github.com/rustdesk/rustdesk/releases).
Open it with ZacOS App Installer. For the initial amd64 integration, the tested
package is `rustdesk-1.5.0-x86_64.deb`, with SHA-256:

```text
4e2b9701d25a7ef373f3d09bda584a37d6902f97a6ab15f2e86d4a5783e2a0ec
```

Verify the download against the official release asset digest before installing.
Do not use the separate unattended-Wayland package for this attended-sharing
setup. The native package installs RustDesk's system service, which provides
its privileged input path; do not make `/dev/uinput` world-writable.

ZacOS 0.1.43 includes the capture prerequisites and portal preference. On
0.1.42 or earlier, install `pipewire`, `wireplumber`, `xdg-desktop-portal` and
`xdg-desktop-portal-wlr` separately, and add the ScreenCast preference shown in
[the portal configuration](../share/xdg/xdg-desktop-portal/zacos9-portals.conf)
to the existing ZacOS portal preferences without removing FileChooser.
The initial live setup uses a user-level ZacOS portal preference for this.

Once installed, Finder discovers RustDesk through its upstream desktop entry
in Applications. Its original interface is retained; it is not a new Platinum
remote-desktop client. No RustDesk artwork is bundled in ZacOS.

## Sharing the logged-in desktop

1. Open RustDesk on both computers.
2. For an outgoing connection, enter the other computer's RustDesk ID.
3. For an incoming connection, give the trusted person your ID and approve
   their connection in RustDesk. Use attended approval rather than setting a
   permanent password for the initial setup.
4. When the screen-sharing chooser appears, select the monitor to share.
5. Review RustDesk's keyboard/mouse, clipboard and file-transfer permissions;
   grant only what that person needs. Disconnect in RustDesk when finished.

RustDesk uses its configured rendezvous/relay servers for internet connections.
The upstream defaults may contact RustDesk's public infrastructure. Organizations
can configure their own server using RustDesk's network settings; ZacOS does not
deploy or configure a relay server automatically.

Wayland hosting remains experimental upstream. This integration is for the
currently logged-in ZacOS session, not the login screen or a promise of access
after logout, lock or reboot. Remote input, multiple monitors, audio and
cross-platform sessions require validation on the actual machines. A running
service or a successful app launch alone does not establish those capabilities.

## Validation status

RustDesk 1.5.0 installs and opens on the live ZacOS desktop, and its system
service runs. The live portal advertises monitor capture while Finder's file
chooser remains available. The `screen-sharing-session` test negotiates a
ScreenCast session on a private GLES2 headless output and obtains a stream
node and an authorized PipeWire socket; it never shares the real desktop.

This is an initial integration, not end-to-end certification. A separate
GStreamer frame-consumption probe failed to connect to the test stream, and
no second-computer RustDesk session has yet verified frame delivery or remote
input. The isolated Pixman capture probe also failed during screencopy format
negotiation with wlroots 0.18 and portal-wlr 0.7.1. Do not assume hosting works
on software-rendered systems, or switch older hardware to accelerated rendering
just to bypass that limitation. Outgoing RustDesk connections do not require
capturing the ZacOS desktop.

## Wayland integration

ZacOS depends on PipeWire, WirePlumber, `xdg-desktop-portal`, and
`xdg-desktop-portal-wlr`. The session's portal preference routes **ScreenCast**
to the wlroots backend and preserves **FileChooser** on ZacOS's Finder backend.
The compositor already exposes `zwlr_screencopy_manager_v1`. No compositor
patches or X11 session replacement are needed for this capture path.

The wlroots backend does not implement the **RemoteDesktop** portal. The native
RustDesk service supplies its own input path; a sandboxed Flatpak build without
that service is not an equivalent host installation.

After installing an updated ZacOS package, restart the desktop so the portal
services inherit its configuration. To diagnose a missing chooser or black
screen, inspect the user journal for `xdg-desktop-portal-wlr` and check:

```sh
systemctl --user status pipewire wireplumber xdg-desktop-portal xdg-desktop-portal-wlr
systemctl status rustdesk
```

Do not bypass a denied sharing request or configure automatic screen selection
on the live desktop as a workaround.
