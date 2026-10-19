# Memory

Mac OS 9's Memory control panel (Missing Manual, chapter 7) set the disk
cache, virtual memory and a RAM disk. ZacOS 9's has all three, as Linux has
them, in `zacos9-controlpanel memory` (`shell/panels/memory.cpp`), in the Zac
menu's Control Panels and the Control Panels folder.

## Disk Cache

Linux keeps recently used files in memory by itself and gives that memory
back when programs need it. The panel shows how much it holds now (Cached +
Buffers − Shmem from `/proc/meminfo`) and offers no setting.

## Virtual Memory

**On** and **Off**, and a slider for how much: `/swapfile` on the startup
disk, from 1 GB up to what leaves 2 GB of the disk free. Unlike Mac OS 9,
changes apply at once. Turning it off, or making it smaller, first takes
back what it holds; if memory has no room for that, the panel says so and
nothing changes. The file is made with `fallocate` (`btrfs filesystem
mkswapfile` on btrfs), and its line in `/etc/fstab` turns it on at startup.

Other swap, a swap partition or zram, is listed under the slider and never
changed.

**Quit the biggest program before memory runs out** turns earlyoom on or
off (the `earlyoom` package, which ZacOS 9 depends on). When memory and
virtual memory are both nearly full, earlyoom quits the program using the
most memory, instead of letting the computer stop responding.

## RAM Disk

A disk made of memory: on the desktop as "RAM Disk", very fast, and empty
after a restart unless **Save on Shut Down** is on. **On** and **Off**, and a
slider for its size, from 64 MB to half of memory. It is a tmpfs at
`/run/zacos9-ramdisk`, owned by the user who turned it on (mode 0700); the
Sniffer looks for it every 3 seconds (`localvolumes.cpp`). Turning it off is
refused while something on it is open; what was on it is gone.

`zacos9-ramdisk.service` makes it at startup and, with Save on Shut Down,
puts back what was saved (`/var/lib/zacos9/ramdisk.tar`, root's only), and
saves it again at shut down.

## Use Defaults

Virtual memory on (16 GB, or as much as the disk allows), the out-of-memory
guard on, and no RAM Disk.

## The helper

`memory/zacos9-memory-helper` does the work. `status` needs no privileges,
so the panel opens without a password; every change runs as root through
`pkexec`, under the polkit action `org.zacos9.memory` (administrators at the
computer need no password, as with Startup Disk).

    zacos9-memory-helper status
    zacos9-memory-helper swap MEGABYTES | swap off
    zacos9-memory-helper guard on | guard off
    zacos9-memory-helper ramdisk MEGABYTES save|nosave | ramdisk off
    zacos9-memory-helper ramdisk start | ramdisk stop     (the service's)

The RAM Disk's settings are in `/etc/zacos9/memory.conf`.

## Tests

- `memory-helper` (`tests/memory/test_helper.py`): every command against
  stand-ins for the programs it runs, a temporary root and made-up
  `/proc` files.
- `memory-panel` (`tests/memory/test_panel.cpp`): the panel with a fake
  helper: what it shows, each control, Use Defaults, and a refusal.
