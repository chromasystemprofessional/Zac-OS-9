# Privacy

**ZacOS 9 does not track you.** No telemetry, no analytics, no online
account, no advertising ID, no device fingerprint. The project has no server
that receives information about you, and it cannot count how many people run
ZacOS 9.

This is a project rule, not a setting. It is enforced in review and by a
test (`tests/privacy/test_no_tracking.py`), and the whole source is
GPL-3.0-or-later for anyone to check.

## What ZacOS never does

- No telemetry, usage statistics, analytics, crash uploads or "phone home".
- No online account, sign-in, licence check or activation.
- No unique identifier, hardware serial, MAC address, disk serial, machine
  ID or fingerprint sent anywhere.
- No advertising, ad ID, tracking pixel or behavioural profiling.
- No selling, sharing or publishing of personal data.
- No cloud sync or backup of your files, settings, searches or recent items.
- No first-party server. There is no ZacOS endpoint that could receive
  anything.

## What stays on your computer

Documents, settings, accounts, keychain passwords, recent items, search
indexes, clipboard, themes, sounds and screen captures stay local. They are
readable by you and by root on your computer, like any desktop's files; they
are not sent to us.

## When ZacOS uses the network

| Feature | When it connects | What it contacts | What it sends |
|---|---|---|---|
| Software Update | The daily check (on by default; turn it off in Software Update) or when you open it | `api.github.com` for the release list; `github.com` for a package you choose to install | A plain GET for release metadata, with `User-Agent: zacos9-update`. No machine ID and no query identifying you. |
| Software (New Tricks) | When you open or refresh it | `api.github.com`, `dl.flathub.org`, and the apt sources configured on your system | The same catalog request for everyone, plus apt and Flatpak metadata requests. |
| Web search (Bloodhound) | When you choose a Web search | The search site you chose: `duckduckgo.com`, `www.google.com`, `www.bing.com`, or a search-site file you added | Your search words, as any web search. |
| Neighbourhood / Network Browser | When you open it | Your local network, by multicast (mDNS and WS-Discovery) | Discovery probes. It does not use the Internet. |
| File Sharing, Screen Sharing, AFP and SMB | When you connect to or share with a computer | The computer you choose | The protocol's own traffic. |
| Help | When you open a page | Nothing; the pages are installed locally | Nothing. |
| Programs you install (Firefox, Steam, Wine applications, …) | When you run them | Whatever those programs contact | Their own traffic, under their own policies. |

Two things are worth saying plainly:

- GitHub, your search engine and your Linux distribution see your IP address
  and the request, as any web request is seen. That is the network working.
  ZacOS does not proxy those requests and does not add an identifier to them.
- Debian's own services (package updates, time sync) and programs you install
  are outside ZacOS's control. The ISO is Debian, so Debian's privacy choices
  apply to those parts too.

## Why there is no "anonymous usage" ping

Even an anonymous ping would let the project count installs. We don't want
that, and you shouldn't have to trust a promise that the ping is anonymous.
ZacOS therefore has no telemetry endpoint at all.

## In the source

- The updater sends two kinds of request: the release metadata and, only
  when you update, the package. Neither contains a machine identifier.
- The Software catalog request is identical for every machine.
- Web search opens a URL in your browser; it is not a ZacOS service.
- `tests/privacy/test_no_tracking.py` fails if a known tracker or analytics
  endpoint appears in shipped source, or if the update, Software or
  web-search code starts using a host not listed here.
- Environment variables such as `ZACOS9_UPDATE_URL` exist for tests and
  forks; they are not set on an installed system.

## For contributors

A change that adds telemetry, analytics, advertising, remote logging, crash
upload, a unique identifier, fingerprinting, or a background connection not
needed by a feature the user invoked will not be merged. New network
behaviour must be user-initiated where possible, documented in this file,
and covered by the privacy test. See [CONTRIBUTING.md](CONTRIBUTING.md).

## Reporting a privacy problem

Open an issue at
<https://github.com/chromasystemprofessional/Zac-OS-9/issues>, or use the
repository's private vulnerability reporting for anything sensitive. Do not
put personal data in a public issue. See [SECURITY.md](SECURITY.md).
