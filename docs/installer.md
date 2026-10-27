# Installer and Disk Setup's partition map

The ZacOS 9 Installer (`shell/installer/`) puts ZacOS on a hard disk from the
live session; the work is done as root by `system/installer/zacos9-install`, run
through pkexec. Disk Setup (`shell/disksetup/`) partitions drives through
UDisks2. Both draw a drive with the same partition map and plan changes with
the same model.

| Piece | File |
|---|---|
| The model: drives, free space, queued changes, drag limits, room beside another system | `shell/disksetup/plan.{h,cpp}` |
| The partition map (drawing, choosing, dragging) | `shell/disksetup/diskmap.{h,cpp}` |
| Disk Setup's window (First Aid, Partition) | `shell/disksetup/main.cpp` |
| The installer's window | `shell/installer/installer.cpp` |
| Installing, as root | `system/installer/zacos9-install` |
| Freeing a disk the desktop has open | `system/installer/zacos9-release-disk` |

## Where ZacOS can go

When a disk is chosen in the installer:

- **Erase the whole disk.** `zacos9-install DISK NAME` makes a new GPT table
  with a BIOS boot partition, an EFI System partition and ZacOS's ext4
  partition, so the disk starts with either firmware.
- **Next to what's on it** (issue #42). The default when the disk has any
  partition, so an Install clicked in haste keeps what's there. See below.
- **A partition made for it** in Disk Setup (an ext4 partition shows in the
  list as "“Name” on Disk"): `zacos9-install PARTITION NAME` formats only that.
- **An installed ZacOS**: Update or Reinstall keeps accounts, files and
  settings; Erase & Install doesn't.

## Next to what's on it

1. The installer reads the drive from UDisks (no password needed to look)
   and counts the volumes the live desktop opened (under `/media`) as closed:
   the install frees the disk anyway.
2. Each partition that can shrink (ext2/3/4, NTFS) is measured:
   `zacos9-install --min-size PARTITION` prints `min-size BYTES` from
   `resize2fs -P` or `ntfsresize --info`. It closes a volume the desktop
   opened first; a Windows that hibernated (or has Fast Startup on) is named
   as the reason it can't be measured.
3. `besidePlan()` makes the plan: ZacOS gets all of the largest free space
   if that's 16 GB or more (someone made it for ZacOS); otherwise room is
   taken from the end of the partition with the most to spare, keeping its
   files plus a tenth again (2 GB at least), ZacOS getting half of what's
   spare (32 GB if that's less). The computer's boot partition is added if
   the drive has none: an EFI System partition (512 MiB) on a UEFI computer,
   a BIOS boot partition (1 MiB) on a BIOS computer with a GPT drive; an MBR
   drive on a BIOS computer needs none. It goes at the far end from the line
   people will drag.
4. The map shows the plan, ZacOS chosen. Dragging the line between ZacOS and
   the partition before it moves both (`moveBoundary()`); the installer
   refuses positions that would make ZacOS smaller than 16 GB or another
   partition bigger (`--beside` only shrinks).
5. Install runs `zacos9-install --beside DISK NAME STEP...` with the plan as
   steps (`besideSteps()`), in bytes:

   ```
   shrink /dev/sda3 107119378432   the files first, then the partition
   efi OFFSET SIZE                 a new EFI System partition (FAT32)
   biosboot OFFSET SIZE            a new BIOS boot partition (GPT only)
   zacos OFFSET SIZE               ZacOS's partition
   ```

   The script frees the disk without erasing it (`zacos9-release-disk
   --keep`), does the steps in order, and installs on the new partition as
   on any other. Shrinking always does the filesystem first (`e2fsck -f`
   then `resize2fs`, or `ntfsresize`), then the partition with `sfdisk -N`,
   which keeps its start, type, name and UUID; a failure between the two
   leaves a filesystem smaller than its partition, which is harmless. New
   partitions go in with `sfdisk --append`, which refuses to overlap another.

## Starting up afterwards

On a partition install (next to what's there, or on a partition made for
it), `zacos9-install` checks before erasing anything that a BIOS computer can
start GRUB from the disk: a GPT drive needs a BIOS boot partition, an MBR
drive room before its first partition (1 MiB). GRUB is then set up with
os-prober on (`GRUB_DISABLE_OS_PROBER=false`). If the menu GRUB makes has
another system in it, the installer sets it to show for 5 seconds at
start-up with ZacOS chosen; Startup Disk (Control Panels) changes which
starts. A disk with ZacOS alone starts with no menu (hold Shift on BIOS,
Esc on UEFI, to see it).

On a BIOS computer GRUB takes the place of the disk's old boot code, so
another system starts only through GRUB's menu. os-prober finds Windows and
other Linux systems. **Haiku it can't**: its `83haiku` probe looks for
`haiku_loader` and the kernel as files in `/system`, where Haiku has kept
them inside its packages since 2013, and it takes the filesystem for `befs`
where GRUB's mounting says `bfs`. So ZacOS adds Haiku itself
(`boot/31_zacos9_haiku`, installed in `/etc/grub.d`): every BFS partition,
chainloading Haiku's own boot sector on BIOS, or Haiku's EFI loader on UEFI
(`EFI/HAIKU/`, or an `EFI/BOOT/BOOTX64.EFI` that is Haiku's). Haiku's EFI
loader is signed with Haiku's own key, so with Secure Boot on it starts only
once that key is added to the firmware, through GRUB or not.

On UEFI another system's loader in the EFI System partition is left alone,
and the firmware's own boot menu still lists it.

## Disk Setup's partition map

The Partition tab draws the chosen drive top (its start) to bottom: a block
per partition in its format's colour, white for free space. Click to choose;
drag the line under a partition (where the grip is) to resize it. The fields
beside the map are the chosen partition's: a new one takes Name, Format and
Size as typed; one already on the drive changes size as typed (within the
limits shown), and is erased with a new name or format only by Erase. + makes
a partition in the chosen free space, − deletes one. Nothing changes until
Apply, which says what will be erased first; changes go through UDisks,
which asks for an administrator's password.

How small a partition can be comes from `zacos9-install --min-size` (as root;
the installer's polkit action), asked once when an ext4 or NTFS partition is
chosen. Until it's known, a partition can only grow. Partitions in use, the
EFI System and BIOS boot partitions, and formats Linux can't resize (Haiku's
BFS, APFS, HFS+) don't move.

## Tests

| Test | What |
|---|---|
| `disksetup-plan` | The model: free space (MBR extended partitions too), refusals, drag limits, `moveBoundary()`, `besidePlan()` on BIOS/UEFI, GPT/MBR, free space and shrinking, `besideSteps()`. |
| `disksetup-partition-map` | Disk Setup against a stand-in UDisks (`tests/disksetup/fake_udisks.py`): drag Windows smaller, +, a name, Apply; the UDisks calls. |
| `installer-beside` | The installer against stand-ins for UDisks, lsblk and `zacos9-install`: choose the disk, drag the line, Install; the `--beside` arguments. |
| `release-disk` | `zacos9-release-disk`, `--keep` included, against stand-in tools. |
| `grub-haiku` | `boot/31_zacos9_haiku` against stand-in tools, its output checked by `grub-script-check`: BIOS and UEFI entries, and nothing but entries on stdout. |

`tests/disksetup/stage.py` starts the headless desktop and stand-in UDisks
for the last three; `ZACOS9_TEST_SHOT=FILE` keeps the last screenshot when
something never showed on screen.

`zacos9-install --beside` and the GRUB menu need real disks: they're checked
in a virtual machine beside Haiku, BIOS and UEFI. `tests/vm/haiku-disk.sh`
makes the disk from Haiku's anyboot image (Haiku, then free space; no
installer, no root); put a copy-on-write layer of it where `dev-boot.sh
--disk` looks:

```sh
tests/vm/haiku-disk.sh haiku-r1beta5-x86_64-anyboot.iso haiku-bios.qcow2 bios
qemu-img create -f qcow2 -b $PWD/haiku-bios.qcow2 -F qcow2 \
	~/.local/share/zacos9-vm/devboot-disk.qcow2
scripts/dev-boot.sh --disk --record --keep        # --uefi with haiku-uefi.qcow2
```
