# Security policy

ZacOS 9 is a desktop environment and packaging project. It runs with the
user's normal desktop privileges; its privileged helpers use polkit and
`sudo`. This page explains what is supported and how to report a problem
privately.

## Supported versions

Security fixes are made on `main` and released in the newest **1.9.x**
release. Older releases are not maintained.

| Version | Supported |
|---|---|
| Latest 1.9.x release | Yes |
| `main` (development) | Fixes land here first |
| Older releases | No; update with **Zac menu ▸ Fetch Updates** |

The ZacOS Extras and Debian packages have their own release and security
processes. Report a problem in those to their maintainers.

## Reporting a vulnerability

**Do not open a public issue for a security problem.**

Use GitHub's private vulnerability reporting:

<https://github.com/chromasystemprofessional/Zac-OS-9/security/advisories/new>

You can also open the repository's **Security** tab and click **Report a
vulnerability**. The report is visible only to the maintainers until an
advisory is published.

Please include:

- the ZacOS 9 version, from **About This Computer** or `dpkg -l zacos9`;
- the component: installer, session script, a control panel, the Sniffer,
  a privileged helper, packaging, or a default service;
- the impact and who could trigger it;
- exact reproduction steps or a small proof of concept;
- whether the problem is already public, and any suggested fix.

Do not include passwords, private keys, personal files, Apple ROMs or Mac OS
installers. If you need to send a sensitive file, ask first.

## What is in scope

- ZacOS 9's own C, C++ and shell code.
- The session scripts, installer, privileged helpers, polkit rules and
  systemd units.
- Default configuration and the way ZacOS 9 enables network services.
- The packaging and update path.

Examples include privilege escalation through a helper, a session script
that trusts an unsafe path, a network service enabled without the user's
knowledge, or an installer path that writes outside its target.

## What is out of scope

- Vulnerabilities in Debian, wlroots, Qt, CUPS, PipeWire, the kernel,
  firmware, Flatpak applications or other upstream projects. Report those
  upstream; tell us if ZacOS 9's defaults make them worse.
- Problems that require physical access, an already-compromised
  administrator account, or a malicious local user with the same privileges.
- Missing hardening or best-practice headers with no practical impact.
- Social engineering, spam and denial-of-service tests against systems or
  people you do not own or have permission to test.
- Apple ROMs, Mac OS installers, themes or other material the project cannot
  legally distribute.

ZacOS 9 is a desktop, not a sandbox. **Limited accounts and Puppy Mode are
not security boundaries**; they keep a desktop tidy. Do not rely on them to
contain hostile code.

## Coordinated disclosure

This is a volunteer project, so response times are best effort. We aim to
acknowledge a report within seven days and to keep you informed while a fix
is prepared. Please allow time for a release before public disclosure.

When a fix is ready, we will publish a GitHub security advisory describing
the issue and the affected versions. We will credit the reporter unless you
ask to remain anonymous.

## Safe harbour

We welcome good-faith research that follows this policy. Do not access other
people's data, disrupt services, or test against systems you do not own or
have permission to test. If you are unsure whether something is in scope,
ask privately first.

## Related

- [Security advisories](https://github.com/chromasystemprofessional/Zac-OS-9/security/advisories)
- [Contributing](CONTRIBUTING.md)
- [Project status](PROJECT_STATUS.md)
