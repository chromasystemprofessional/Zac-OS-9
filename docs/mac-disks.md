# Mac disks and disk images

Sniffer mounts recognized **HFS, HFS+ (including HFSX) and APFS** volumes
read-only. Physical disks are discovered at startup and when connected;
double-click a disk image to mount it and open its contents. Mounted volumes
appear as desktop disk icons. Mounting and ejecting may require administrator
authentication.

Read-only mounting is intentional: neither the filesystem driver nor an
image's loop device is allowed to write to the source. Mounts also disable
device nodes, setuid programs and executable files. Linux does not offer safe
write support for every Mac filesystem, especially APFS.

## Images

Supported containers:

- Raw filesystem images and Apple Partition Map/GPT disk images (`.img`,
  `.dsk`, `.hfv`, `.hda`, `.image`, and recognized `.cdr`/`.toast`/`.iso` files).
- Disk Copy 4.2 images, with the data portion separated from header and tags.
- Unencrypted UDIF `.dmg` files, expanded with `dmg2img`.
- `.sparseimage` files, read through `libmodi` and copied to a sparse raw file.
- `.sparsebundle` directories, reconstructed from their band files.

All supported Mac partitions in an image are mounted, not just the first one.
APFS containers expose their available volumes inside the container folder.
The reader is invoked once per volume with a numeric, one-based index: its
advertised `-f all` option is not implemented by the Debian reader. Failed
container mounts roll back the volumes already mounted; eject unmounts every
child before releasing the image's loop device.
Mount detection uses the kernel mount table, since some APFS FUSE roots
cannot be stat'ed even when mounted. Rollback also checks the volume whose
reader failed, so a mount created before an error is not left behind. If
cleanup cannot unmount a busy volume, its original error is retained and a
recovery disk icon allows another eject attempt. Eject can also recover
unrecorded child mounts left by earlier versions.
Reopening an already mounted image reuses its mounts.

Converted images live temporarily in `$XDG_CACHE_HOME/zacos9/mac-images`
(normally `~/.cache/zacos9/mac-images`). They can require substantial free
space; the source is not overwritten. Images larger than 2 TiB are rejected.
Eject the desktop icons using **Put Away** or drag them to Trash. Once the
last image partition is ejected, the loop device and its converted copy are
removed. Failed ejects keep the backing image rather than deleting data still
in use.

To use a raw image in Classic instead, select it and choose **Classic** from
the menu. Eject it from Sniffer first, so the emulator cannot modify a
filesystem that Sniffer is browsing.

## Limits

This is not universal Mac-format or encryption support. MFS, CoreStorage,
FileVault/encrypted images, APFS Fusion configurations, and older proprietary
image formats are not supported by this integration. APFS support is limited
to the installed `libfsapfs` reader; some compressed files, newer features or
damaged volumes may not be readable. Image format errors are reported;
recognized Mac-volume mount failures are not passed to disk initialization.
APFS and CoreStorage/FileVault disks are excluded from the
disk-initialization/erase helper.

Already mounted volumes and non-Mac filesystems retain their existing mount
behavior. Kernel HFS/HFS+ support, UDisks2, polkit, `python3-gi`, `dmg2img`,
`libmodi1`, `libfsapfs-utils`, FUSE and ACL tools are required. The release
package installs the userspace dependencies and the read-only helper's polkit action.

## Tests

`mac-disks` tests source preservation, container preparation, read-only loop
setup, pinned read-only block descriptors, mount restrictions, partition
discovery, rollback and eject cleanup without touching physical disks.
Finder's image recognition is covered by `finder-vfs`; existing hotplug and
disk-initialization tests cover the surrounding behavior.
