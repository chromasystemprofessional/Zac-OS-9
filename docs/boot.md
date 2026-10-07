# Starting up

What the screen shows from power-on to the desktop, and which part draws it.

1. **GRUB** (`boot/09_zacos9`, `boot/zacos9-bootlogo`): white, the logo.
2. **Boot splash** (Plymouth theme `boot/plymouth/`): the logo on white for
   half a second, then the **Welcome to ZacOS 9** box over the desktop
   pattern. Extensions join the row along the bottom of the screen as
   their kernel modules load.
3. **Desktop** (`compositor/src/startup.c`): the same Welcome box, carried
   on until the menu bar and Finder are up.

On shutdown the splash shows the logo and a progress bar instead.

## The extension parade

`lib/extensions.c` lists the extensions Extensions Manager shows, each with
the kernel modules it stands for. `boot/zacos9-parade` (`zacos9-parade.service`)
starts before udev loads the drivers and watches `/proc/modules` for two
minutes. Each time a module of a new extension loads, it:

- adds the extension's key to `/run/zacos9/parade`, in load order, and
- sends `plymouth update --status=zacos9-ext:INDEX` (the index into
  `lib/extensions.c`). The splash crops that icon out of `parade.png`.

Modules that were already loaded in the initramfs (graphics, disks) appear
first, together. After the handover, the compositor re-reads
`/run/zacos9/parade` as it grows. Started without that record (no splash,
or after a log out), it shows what `/proc/modules` lists, marching in as
the bar fills.

## Splash art from the compositor's code

`boot/make-splash.c` runs at build time. It draws the splash's pictures with
the same `lib/welcome.c` code the compositor uses: `welcome.png`, `bar-fill.png`,
`bar-end.png`, `pattern.png` and `parade.png`. The script places them where
`pl_welcome_box_origin`, `pl_welcome_bar_origin` and `pl_parade_slot` would.
The splash fills the bar to three quarters; the compositor starts from
there. Changing the box's layout in `lib/welcome.h` means changing the
constants at the top of `zacos9.script` too. `boot-splash-welcome` checks
the positions in Plymouth's own interpreter.

The splash always uses the default pattern and accent colour; it can't
read the user's settings. Where those differ, the desktop's colours take
over at the handover.

## Handing over without a blank screen

Plymouth normally quits before the login starts. The kernel console then
repaints the screen (all white) until the compositor draws. Instead:

- `plymouth-quit.service` runs `zacos9-boot-handoff quit` (drop-in in
  `boot/plymouth-quit.service.d/`). It runs `plymouth deactivate`: the
  splash stops drawing and lets go of the display but leaves its frame up.
  It also writes the boot time to `/run/zacos9/handoff` and starts
  `zacos9-boot-handoff.service`.
- `plymouth-quit-wait.service` returns at once once handed over (`wait`),
  so greetd and the autologin start.
- The compositor sees a handoff less than 60 s old. It skips the logo,
  draws the Welcome box straight away with the bar at three quarters, and
  uses the boot's parade record.
- The watcher (`watch`) ends the splash for good:
  - with `plymouth quit --retain-splash` half a second after another
    process opens `/dev/dri/card*`;
  - with a plain `plymouth quit` if the text login (tuigreet) appears;
  - with a plain `plymouth quit` after 30 s whatever happens, so a login
    is never left hidden.

If `deactivate` or starting the watcher fails, `quit` falls back to a plain
`plymouth quit`. Tests: `boot-parade`, `boot-handoff`, `boot-splash-*`,
`startup-displays`.

Debugging on a running system: `systemctl status zacos9-parade
zacos9-boot-handoff plymouth-quit`, `cat /run/zacos9/parade /run/zacos9/handoff`.
Booting with `splash` removed from the kernel command line skips all of it.
