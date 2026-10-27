# Memory

Mac OS 9's Memory control panel (Missing Manual, chapter 7) set the disk
cache, virtual memory and a RAM disk. ZacOS 9's has all three, as Linux has
them, and Compressed Memory, in `zacos9-controlpanel memory` (`shell/panels/memory.cpp`), in the Zac
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

Other swap, such as a swap partition, is listed under the slider and never
changed.

**Quit the biggest program before memory runs out** turns earlyoom on or
off (the `earlyoom` package, which ZacOS 9 depends on). When memory and
virtual memory are both nearly full, earlyoom quits the program using the
most memory, instead of letting the computer stop responding.

## Compressed Memory

**On** and **Off**, and **Use up to** a quarter, half, three quarters or all
of memory. On, a zram swap device keeps memory that hasn't been used for a
while compressed in RAM (zstd), so there is room for more before anything
goes to virtual memory on disk. The line under the menu shows the device's
size, what is in it and how much RAM that takes after compression
(`/proc/swaps`, `/sys/block/zram0/mm_stat`). Off by default.

`zram PERCENT` writes `/etc/systemd/zram-generator.conf`:

```ini
[zram0]
zram-size = ram * 50 / 100
compression-algorithm = zstd
```

then reloads systemd and starts `dev-zram0.swap`. systemd-zram-generator (a
dependency of the package) makes the device from that file at every boot. A
device already running keeps its size, so a new size stops it first.
`zram off` stops it (swapoff moves what it holds back into memory; with no
room for that, the helper says so and nothing changes) and rewrites the file
with no `[zram0]` section. The file is never removed: Debian's generator
ships `/usr/lib/systemd/zram-generator.conf` with a device of its own (half
of memory, at most 4 GB), which a file of the same name in `/etc` hides. The
package's postinst writes that file when there is none, so Compressed Memory
stays off until the panel turns it on.

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
guard on, Compressed Memory off, and no RAM Disk.

## The helper

`system/memory/zacos9-memory-helper` does the work. `status` needs no privileges,
so the panel opens without a password; every change runs as root through
`pkexec`, under the polkit action `org.zacos9.memory` (administrators at the
computer need no password, as with Startup Disk).

    zacos9-memory-helper status
    zacos9-memory-helper swap MEGABYTES | swap off
    zacos9-memory-helper guard on | guard off
    zacos9-memory-helper zram 25|50|75|100 | zram off
    zacos9-memory-helper ramdisk MEGABYTES save|nosave | ramdisk off
    zacos9-memory-helper ramdisk start | ramdisk stop     (the service's)

The RAM Disk's settings are in `/etc/zacos9/memory.conf`.

## Tests

- `memory-helper` (`tests/memory/test_helper.py`): every command against
  stand-ins for the programs it runs, a temporary root and made-up
  `/proc` files.
- `memory-panel` (`tests/memory/test_panel.cpp`): the panel with a fake
  helper: what it shows, each control, Use Defaults, and a refusal.
