# Front Desk

Front Desk is ZacOS 9's shared-computer shell: a full-screen panel of the
programs and documents an account may use, in the tabbed-folder style of the
classic classroom launchers. It is an extra (`zacos9-extra-frontdesk`), with
two small hooks in the desktop: the session chooses the shell, and the login
window wears its welcome.

**It is not a second account system.** The Unix account is the user, the
ZacOS login window is the welcome screen, and the existing `Use only chosen
programs` allowlist is the program list. There is no Front Desk password,
user database or account creation.

## Pieces

| Piece | Where | What |
|---|---|---|
| The panel | `ZAC-OS-EXTRAS/frontdesk/src/` | `zacos9-frontdesk`: tabs, pages, single-click launch and original click sounds |
| The setup | `ZAC-OS-EXTRAS/frontdesk/src/setup_main.cpp` | `zacos9-frontdesk-setup`: accounts, programs, menus, security |
| Control panel | `ZAC-OS-EXTRAS/frontdesk/data/zacos9-frontdesk-setup.desktop` | **Front Desk Setup** in Control Panels and the Zac menu |
| Account settings | `system/accounts/zacos9-accounts-helper` | `desktop USER frontdesk|puppy|finder`, `frontdesk`, `frontdesk-settings`; `/etc/zacos9/frontdesk/USER` |
| Session shell | `session/zacos9-session`, `compositor/src/main.c` | `ZACOS9_SHELL=frontdesk`; starts Front Desk instead of the Sniffer, with The Collar |
| Menu bar | `shell/menubar/launch.c` | `ZACOS9_MENUBAR_MODE=frontdesk`: the Zac menu keeps only About This Computer |
| Login window | `shell/greeter/main.cpp` | “Welcome to Front Desk!”, Happy Zac, Start |
| Catalog | `assets/store/catalog.json` | **Software ▸ Extras ▸ Front Desk** |

## Accounts and startup modes

`zacos9-accounts-helper desktop USER MODE [KEY=VALUE…]` writes the three
startup modes:

- `frontdesk`: `/etc/zacos9/frontdesk/USER` exists, with settings; Puppy Mode
  is removed.
- `puppy`: the Puppy Mode file exists; Front Desk is removed.
- `finder`: both are removed (the ordinary ZacOS session).

The three are mutually exclusive. Puppy Mode is the “restricted Finder”.

Front Desk Setup is also a control panel, so an administrator can open it
from an ordinary session, before any account runs Front Desk: **Control
Panels ▸ Front Desk Setup**, the Zac menu's Control Panels, or the panel's
own **Special ▸ Go To Front Desk Setup…** when it is running.

The settings file is root-owned and world-readable:

```
menus=minimal|restricted|all
save=anywhere|user
save-removable=on|off
finder=on|off
speech=on|off
capture=on|off
```

`frontdesk-settings USER KEY=VALUE…` changes them. Only an administrator may
write, through polkit; the user cannot change their own shell.

## The session

`zacos9-session` reads `/etc/zacos9/frontdesk/$(id -un)` before starting the
compositor. Its presence sets `ZACOS9_SHELL=frontdesk` and
`ZACOS9_MENUBAR_MODE=frontdesk`. In that session:

- `zacos9-wm` starts the menu bar, The Collar and `zacos9-frontdesk`, not the
  Sniffer; the Collar's hot key works as in an ordinary session;
- the Front Desk window is the session shell window: no title bar
  (`DECOR_STYLE_NONE`), maximized to the usable area under the menu bar;
- user autostart entries are skipped, so only the panel's programs start;
- the menu bar's Zac menu keeps About This Computer; Control Panels, Software,
  Old Dog and the Sniffer are not an escape there. The Collar remains as its
  own control strip, and the programs its menus open follow the same
  one-at-a-time rule;
- Quit logs out, and Restart/Shut Down use the normal `systemctl` paths.

The panel stays a program of the session while the programs it starts run:
like the shared Macs it copies, only the program in front draws, and the
others wait hidden but open. The panel is one of them, so the Application
menu and ⌘-Tab bring it back to start another program; choosing a program
hides the panel, and closing the last program returns to it. A program that
detaches without opening a window leaves the panel in front.

The panel's tabs, page arrows and icons click with ZacOS's own original
sounds, as At Ease did; opening an item plays the opening sound, and a
refused action the alert chime. **Special ▸ Turn Sound Off** silences them.

## Programs

The panel reads the account's malcontent app filter through accountsservice,
the same list the Users panel's `Use only chosen programs` edits. An account
with no filter shows every program the panel offers; Front Desk Setup's
Programs… window writes an allowlist through accountsservice, so both
control panels agree. The Users panel's **Use only chosen programs** and **Programs…** work
the same way for every account, including an administrator's; if the old
list can't be read, choosing programs still sets a new one. Both Programs…
windows list the applications in the Finder's Applications folder—the
user-installed applications—and leave ZacOS's own programs, control panels,
utilities and the system's helpers out.

## Security

This is a simplified desktop, not a sandbox. The real account, its home folder
and the OS permissions still decide what can run and what can be read. Front
Desk hides the usual ways out but does not claim to stop a program started
another way. `save=user` and `capture` are stored and used by the panel; the
save-location enforcement through the file chooser portal is a later hook.

## Tests

- `tests/accounts/test_helper.sh` covers the Front Desk settings, modes and
  status.
- `tests/compositor/test_frontdesk_shell.py` starts a Front Desk session and
  checks that the menu bar, The Collar and the panel start, and the Sniffer
  doesn't.
- `tests/compositor/test_frontdesk_switch.py` starts a Front Desk session and
  checks that the panel stays in the Application menu while a program runs,
  that choosing it hides the program, and that closing the last program
  returns to the panel.
- `ZAC-OS-EXTRAS/frontdesk/tests/` builds and checks the panel and setup
  offscreen, the panel's program list (ZacOS's user programs in, control
  panels out), its sounds (with a stand-in player), and that Front Desk
  Setup is a control panel.
