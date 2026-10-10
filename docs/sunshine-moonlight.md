# Two-way remote desktop with Sunshine and Moonlight

This is the earlier streaming experiment, not the current ZacOS direction.
Sunshine is stopped and its user-service autostart is disabled on the
development machine. See [RDP](rdp.md) for the current opt-in implementation.

Install **both applications on every computer** that should send and receive:

| Role | Application |
| --- | --- |
| Share this computer's desktop | Sunshine |
| View and control another computer | Moonlight |

These are existing applications, not a new ZacOS protocol or Platinum client.
Finder discovers their native/Flatpak desktop entries automatically.
Windows, macOS and Linux have clients and hosts; check the upstream support
notes for each host. Sunshine's macOS hosting remains experimental.

## ZacOS installation

Download the native Debian Trixie package for your architecture from
[Sunshine's official releases](https://github.com/LizardByte/Sunshine/releases).
Verify its SHA-256 against the release asset digest, then open the package
with ZacOS App Installer. The initial amd64 setup uses:

```text
sunshine_2026.914.233613-1+debiantrixie_amd64.deb
a86f858cbae57f60ecebd445402ee0295c1c057b857bd45d44c2ea2d255cbbfb
```

Install Moonlight from the existing official user Flathub remote:

```sh
flatpak install --user flathub com.moonlight_stream.Moonlight
flatpak run com.moonlight_stream.Moonlight
```

If Flathub is not enabled, enable it through Software's Additional Sources
first. Do not replace an existing remote with a different URL. Moonlight
6.2.0 was installed for the initial live setup.

Sunshine's native package installs its KMS capture capabilities, udev input
rules, and an on-demand user service. KMS capture requires the package's
`CAP_SYS_ADMIN` capability; this is a privileged host, not a sandboxed viewer.
The package uses group/active-seat input access, not world-writable devices.

If `/sys/class/misc/uinput` is absent, load the module as an administrator:

```sh
pkexec /usr/sbin/modprobe uinput
```

The initial live setup also installs a modules-load entry so it is available
after reboot. Check `test -w /dev/uinput` from the local active session.
Do not grant all users input access or run the entire graphical app as root.

## Initial local setup

The initial live installation is restricted to localhost until credentials
and pairing are configured. Its `~/.config/sunshine/sunshine.conf` contains:

```ini
capture = kms
encoder = software
bind_address = 127.0.0.1
origin_web_ui_allowed = pc
upnp = disabled
system_tray = disabled
```

KMS bypasses the RustDesk/portal capture path. Software H.264 encoding is used
because this machine's nouveau setup has no confirmed hardware encoder.
This choice is machine-specific; it is not forced on every ZacOS installation.
Sunshine also supports direct wlroots capture (`capture = wlr`), but it needs
separate validation, particularly with software-rendered compositor outputs.

Only the Desktop app is configured. The upstream Low Res Desktop entry uses
`xrandr`, so it is not included for the Wayland desktop.

Start/stop the host from the local session:

```sh
systemctl --user start app-dev.lizardbyte.app.Sunshine.service
systemctl --user stop app-dev.lizardbyte.app.Sunshine.service
```

Open <https://127.0.0.1:47990> in a browser on the host. Sunshine generates a
self-signed certificate: verify you are opening that local address before
accepting its warning. Create your own Web UI username and strong password;
ZacOS does not invent or store credentials in source files.

## Pair and connect in either direction

1. Finish creating credentials on the host before making it reachable.
2. To allow a trusted LAN/VPN client, change `bind_address` to the host's
   address on that network using Sunshine's configuration. Keep the Web UI
   restricted to `pc` and leave UPnP disabled. Restart Sunshine.
3. Open Moonlight on the other computer and add the host's LAN/VPN address.
4. Moonlight displays a PIN. Enter it in Sunshine's local Web UI to approve
   that client. Treat pairing as persistent access, not a one-time approval.
5. Choose **Desktop** in Moonlight. Check changing screen content, cursor,
   keyboard/mouse, audio and disconnect behavior.
6. Reverse the roles to share the other computer: run Sunshine there and
   connect using Moonlight on ZacOS.

Do not forward router ports or expose the administration UI on the internet.
Use a trusted VPN for access across networks. Review/unpair clients in Sunshine
when their access should end. Automatic hosting at login is not enabled by the
initial setup.

## Validation and current limitations

After the initial localhost test, LAN hosting was enabled at the user's request
with `bind_address = 0.0.0.0` (all IPv4 interfaces). The current LAN address is
`192.168.2.110`; add it manually in Moonlight if discovery does not find it.
The streaming endpoint responds on that address. Web UI access remains
restricted to `pc` and UPnP remained disabled. Credentials were later created,
but the integrated browser returned HTTP 401; the user rejected this workflow.
The host is now stopped. Interface addresses may change with DHCP.

On the initial ZacOS machine, Sunshine starts through its user service,
detects the real monitor through KMS, passes its software H.264 encoder probe,
and serves its local setup page (HTTP 200). Moonlight 6.2.0 opens. The active
local user has read/write access to the actual uinput device after module load.

This is **not yet a verified two-computer session**: credentials, pairing,
visible video delivery and remote input must still be tested on both machines.
Sunshine reports that nouveau lacks atomic modesetting, so cursor capture may
be incomplete, and panel orientation detection falls back to landscape.
Moonlight also reports failed hardware-decoder probes; select software
decoding for the first connection if automatic selection produces a black
screen. Do not assume a successful encoder probe proves client video delivery.

RustDesk remains installed, but incoming sharing produced a black screen.
Its earlier integration notes are in [screen-sharing.md](screen-sharing.md).
No unrelated portal settings were removed while trying this alternative.
