# Screen Sharing with RustDesk

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
