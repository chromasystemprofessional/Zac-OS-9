# Emulator patches

These are ZacOS 9's changes to the emulator sources built by
`emulation/build-emulators.sh`. They are applied to a generated checkout under
`build/emulators/macemu`; do not edit that checkout instead.

## SheepShaver Classic bridge

`sheepshaver-classic-bridge.patch` adds `OP_CLASSIC_BRIDGE` (selector 53,
instruction `0x18000038`) to SheepShaver. The guest's Classic Open Helper calls
it to poll for application-open requests and to acknowledge them. The emulator
reads `requests/request.<n>.req` and writes `acks/ack-<n>` below the directory
named by `ZACOS9_CLASSIC_QUEUE` (falling back to `$HOME/zacos9-classic` if it is
not set).

The bridge only touches those two queue paths. It does not open the requested
Mac path on the host and does not expose a network service. The guest still
resolves and launches the application itself through the Mac's File Manager and
Process Manager. An unpatched SheepShaver treats selector 53 as a fatal error,
so install the patched build before the guest helper.

The patch applies to macemu commit
`892eeb74ab9d70dfb034138a0b39057b14f275bc` (the pinned
`kanjitalk755/macemu` revision; SheepShaver descends from the
[cebix/macemu](https://github.com/cebix/macemu) project). It changes only:

- `SheepShaver/src/emul_op.cpp`
- `SheepShaver/src/include/emul_op.h`

Both files remain under the upstream GPL-2.0-or-later licence. The complete
patched source is distributed with the emulator package; see
`packaging/emulators/copyright`. Basilisk II is not patched.

No Apple ROMs, system software, fonts or other Apple files are part of the
patch. The guest helper is original code in `emulation/classic-open`.
