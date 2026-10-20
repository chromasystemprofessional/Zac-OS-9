# Memory

Mac OS 9's Memory control panel set the disk cache, virtual memory and a
RAM disk. Linux manages all three itself, so ZacOS's **Memory** panel
(`zacos9-controlpanel memory`, in the Zac menu's Control Panels and
`share/applications/zacos9-memory.desktop`) keeps what still means
something:

- **Built-in Memory**: how much is installed and how much is in use
  (`/proc/meminfo`: MemTotal less MemAvailable), refreshed every two
  seconds.
- **Compressed Memory**: On/Off, and "Use up to" a quarter, half, three
  quarters or all of memory. On, a zram swap device keeps memory that
  hasn't been used for a while compressed in RAM (zstd) instead of on
  disk. The line under the pop-up shows the device's size, what is in it
  and how much RAM that takes after compression (`/proc/swaps`,
  `/sys/block/zram0/mm_stat`). Off by default.
- **Virtual Memory**: the swap on disk, as a report (size, in use, which
  devices or files), or "Off" with a note that Linux manages it.

## How Compressed Memory works

The panel only reads. Changes go through `memory/zacos9-memory-helper`
as root (pkexec; polkit action `org.zacos9.memory`, with a rule letting
administrators at the computer through without a password, as the other
panels' computer-wide settings do):

```sh
zacos9-memory-helper zram on 50   # 25, 50, 75 or 100
zacos9-memory-helper zram off
```

`zram on` writes `/etc/systemd/zram-generator.conf`:

```ini
[zram0]
zram-size = ram * 50 / 100
compression-algorithm = zstd
```

then `systemctl daemon-reload` and `systemctl start dev-zram0.swap`.
systemd-zram-generator (a dependency of the package; it ships no
configuration of its own, so nothing is on until the panel says so)
turns the file into `dev-zram0.swap` and `systemd-zram-setup@zram0.service`
at every boot. A device already running keeps its size, so a new size
stops it first. `zram off` stops both units (swapoff moves what the device
holds back into memory; if there is no room, the helper fails and says so,
and everything stays as it was), removes the file and reloads systemd.

The panel reads the setting from the same file (`ZACOS9_ZRAM_CONF` stands
in for tests), and treats a zram swap found in `/proc/swaps` as On even
without it. `ZACOS9_NO_SYSTEMCTL` makes the helper leave systemd alone
(`memory/test_helper.sh`).
