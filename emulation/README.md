# Classic Mac OS

Platinum 2026 runs classic Mac OS in an emulator window:

- SheepShaver (PowerPC) runs Mac OS 7.5.2 to 9.0.4. This is the default.
- Basilisk II (68k) runs System 7 to Mac OS 8.1.

Apple's files are not part of Platinum 2026. You supply them yourself.

## Setting up

1. Build the emulators once:

   ```sh
   sh emulation/build-emulators.sh
   ```

   The binaries go into `build/emulators/bin`.
2. Put your files in a folder named `Classic` in your home folder, up to two
   folders deep. Set `PLATINUM_CLASSIC_DIR` to use a different folder.
   - **ROMs.** The launcher identifies each ROM by its checksum, so file names
     don't matter. The best one available is used:
     - **PowerPC:** an Old World ROM from a Power Mac 7200, 7500, 8500 or
       9500 (`9630C68B` works well), or a 7300 to 9600. A "Mac OS ROM"
       file from a Mac OS 9 CD also works.
     - **68k:** a Quadra 650 or 800 ROM (`F1ACAD13`), or another 68040
       Quadra or Centris ROM.
   - **Disks.** These count as disks:
     - HFS and HFS+ images, and Apple-partitioned disk or CD images
       (`.dsk`, `.img`, `.hfv`, `.toast`, `.iso`, `.cdr`);
     - `.dsk` and `.hfv` files, even blank ones.

     CD images are attached read-only. Compressed `.dmg` images don't work.
     Convert them first, for example with `dmg2img`.
3. Choose **Classic** in the logo menu, or double-click a disk image in the
   Finder. If something is missing, an alert says what.

## Installing Mac OS 9 from a CD image

Put the install CD image (for example `Mac OS 9.toast`) in `~/Classic`.
If there is no hard-disk image there, the first start creates a blank one,
`Macintosh HD (PPC).dsk` (or `(68k)`; 2 GB, sparse). Disks whose names say
"68k" or "ppc" are only attached to that emulator, and the same image in
two folders is attached once. The Mac boots from the CD and offers to
initialize the blank disk. Run the installer onto it. Afterwards, remove the
CD image from the folder, or leave it there: hard disks are attached first,
so the Mac starts up from the installed system and only falls back to a CD.

## Inside the Mac

- The emulator window gets a Platinum frame from platinum-wm.
- Your home folder appears on the Mac's desktop as the "Unix" volume. Copy
  files across through it.
- RAM is 256 MB for SheepShaver and 64 MB for Basilisk II. The screen is an
  800×600 window.

Run `platinum-classic --check` to see which emulator, ROM and disks would be
used. Add `--68k` for Basilisk II.

## Known gaps

- SDL's Wayland backend never shows a window under platinum-wm, so the
  emulators run through Xwayland.
- Classic applications copied out through the "Unix" volume show in the
  Finder with the Classic icon (their type, 'APPL', is in the `.finf/`
  file SheepShaver keeps beside them). Double-clicking one starts Classic,
  but doesn't open that application inside the Mac yet.
