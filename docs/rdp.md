# Remote Desktop (RDP)

RDP is the current direction for Microsoft Remote Desktop-style connections.
Remmina provides outgoing connections. An opt-in xrdp/WayVNC bridge shares
the current logged-in ZacOS desktop, rather than opening a separate X11 session.
The native development package is installed. The test host is stopped pending
a desktop restart to activate the multi-monitor pointer fix.

## Connecting from ZacOS

Install the Debian packages `remmina` and `remmina-plugin-rdp` using an
administrator-authorized package installer. Remmina is optional, not bundled
in ZacOS. Finder discovers its upstream desktop entry in Applications.

1. Enable remote desktop on the destination computer.
2. Open Remmina in Applications and create a new saved connection.
3. Select RDP, enter the destination's hostname or LAN address, and enter the
   account that is authorized for remote desktop on that computer.
4. Connect. Verify the server certificate or fingerprint before trusting a new
   certificate; do not disable certificate validation.

On Windows, incoming RDP normally requires a Pro, Enterprise or Education
edition; Windows Home does not provide the built-in RDP host. macOS does not
include an RDP host. Installing an RDP client on either computer does not
enable incoming connections to it.

Use a trusted LAN or VPN. Do not forward RDP port 3389 on the router.
No destination credentials or saved connections are created automatically.

## Hosting the current ZacOS desktop

ZacOS runs a custom wlroots Wayland compositor. Ordinary xrdp with Xorg
creates a separate X11 session; it does not share the desktop on the screen.
FreeRDP's X11 shadow server also does not capture the whole Wayland desktop.

The host commands are installed with ZacOS; the optional engine packages are
`xrdp`, `wayvnc`, `socat` and `openssl`. Before installing xrdp, prevent its
default services from opening a separate-session listener:

```sh
sudo systemctl mask xrdp.service xrdp-sesman.service
```

Use an administrator-authorized package installer for the engine packages.
The bridge uses its own services and does not require unmasking the defaults.
After installing the compositor fix, log out and back in before testing
remote mouse input on a multi-monitor system.

From a terminal on the logged-in ZacOS desktop:

```sh
zacos9-screen-sharing start 192.168.2.110
zacos9-screen-sharing status
zacos9-screen-sharing stop
```

Replace the example with this computer's actual private LAN IPv4 address.
The start command requests administrator authorization and prints the
certificate's SHA-256 fingerprint. In the other computer's RDP client, connect
to that address and use your ZacOS account name and login password. Compare
the certificate fingerprint before trusting it. No sharing password is
created, read or stored by ZacOS.

Sharing is on demand, not automatically enabled at boot. Start again after
logging in or after changing the LAN address. Stop sharing when finished.
Only the account whose desktop is being shared is authorized; this is
unattended account-authenticated access while enabled, not a per-connection
approval dialog. Anyone with that account's password can control the shared
desktop, so use a strong password.

## Transport boundaries

WayVNC listens only on a Unix socket in a mode-0700 user runtime directory.
The xrdp VNC backend runs in a private network namespace. Its IPv6 loopback
connection goes through a bounded RFB 3.3-to-3.8 handshake adapter to that
socket. Debian's IPv6-enabled xrdp resolves its loopback backend to `::1`;
WayVNC 0.9.1 accepts RFB 3.8, not xrdp's RFB 3.3.

The adapter runs as the sharing user, and xrdp and the external socket proxy
run as the unprivileged xrdp account. Only the PAM session manager and the
namespace supervisor need root. The external listener accepts TLS 1.2/1.3
RDP and requires PAM authentication plus membership in a sharing-user-only
group. No unauthenticated VNC TCP port is exposed to the LAN or other local
users. Do not replace this isolation with an unauthenticated localhost
server or enable xrdp's generic `vnc-any` destination chooser.

The listener stops if capture exits or the user's session manager exits.
The certificate/key persist in `/etc/zacos9/rdp`; service definitions are
transient and deliberately have no boot enablement. A self-signed certificate
is generated for this machine. The key is readable only by root and xrdp.
The certificate is renewed when the bind address changes or expiry approaches;
the start command reports renewal and displays the new fingerprint.

Clipboard, drive redirection, audio, multi-monitor streaming, login-screen
access and automatic resolution changes are not enabled. The host shares
WayVNC's selected monitor at its existing resolution. Lock/logout behavior
and a connection from a second physical computer still need verification.

## Local validation

For a local-only test, install `freerdp3-x11`, `xvfb`, `xdotool`,
`x11-apps`, `imagemagick`, `python3-pil` and `python3-gi-cairo`, then run:

```sh
zacos9-screen-sharing start 127.0.0.1
zacos9-screen-sharing test
```

The test creates a temporary account with a random password held only in
memory, authorizes it for the duration of the test, and opens an isolated
Xvfb RDP client. It pins the certificate, checks a synthetic pattern from
the actual shared desktop, and sends a mouse click and keyboard text to the
test window. The account, client, marker window and screen buffers are
removed afterward. This test briefly places a clearly labelled window on
the shared desktop; it is not run automatically by the package tests.

Local certificate-pinned RDP video and keyboard delivery passed on the
development machine. Mouse testing exposed a compositor bug: output-specific
virtual pointers were mapped across the entire multi-monitor layout. The fix
honors the client's suggested output. Its focused C regression passes, and
a real two-output, scale-2 headless compositor probe received a correctly
positioned remote click and keyboard text. The live compositor must be
restarted to use that change before the complete live self-test can pass.

Sunshine remains stopped with user-service autostart disabled; its credentials
are retained. RustDesk remains installed separately. The bridge and compositor
pointer fix ship in ZacOS 0.1.44; the optional remote-desktop engines remain
separate installations.
