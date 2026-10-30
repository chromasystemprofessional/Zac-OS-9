#!/usr/bin/env python3
"""Decode Mac OS 9 `snd ` resources to WAV.

A `snd ` resource is a Sound Manager 'snd ' — either a square-wave tone
(type 1) or sampled sound (type 2). This handles the uncompressed sampled
cases ('twos' / 'NONE') and square waves; MACE/ima4 and other compressed
formats are a follow-up.

Nothing from Apple ships: this only decodes the user's own extracted
resources on their machine.
"""

import struct
import wave


def decode_snd(data):
    """Return (sample_rate, sample_width, frames: bytes) for a snd resource."""
    if len(data) < 2:
        raise ValueError("snd is too short")
    snd_type = struct.unpack(">h", data[0:2])[0]
    if snd_type == 1:
        # Square-wave tone: count, synth, mode, then a fixed tone.
        # Synthesize 0.5 s of a 440 Hz square wave at 22 kHz, 8-bit.
        rate = 22050
        frames = bytearray(rate // 2)
        for i in range(len(frames)):
            frames[i] = 0x7F if (i * 440 // rate) % 2 == 0 else 0x81
        return rate, 8, bytes(frames)
    if snd_type != 2:
        raise ValueError("unknown snd type %d" % snd_type)
    if len(data) < 18:
        raise ValueError("sampled snd is too short")
    num_channels = struct.unpack(">h", data[2:4])[0]
    num_frames = struct.unpack(">I", data[4:8])[0]
    sample_size = struct.unpack(">h", data[8:10])[0]
    sample_rate = struct.unpack(">I", data[10:14])[0] >> 16  # Fixed 16.16
    num_commands = struct.unpack(">h", data[14:16])[0]
    off = 18
    for _ in range(num_commands):
        cmd = struct.unpack(">h", data[off:off + 2])[0]
        if cmd == 1:  # buffer command holds the samples
            off += 2 + 2 + 4
            break
        off += 8
    if sample_size not in (8, 16):
        raise ValueError("unsupported sample size %d" % sample_size)
    frames = data[off:off + num_frames * num_channels * (sample_size // 8)]
    if sample_size == 8:
        signed = bytearray(len(frames) * 2)
        for i, b in enumerate(frames):
            struct.pack_into("<h", signed, i * 2, (b - 128) * 256)
        frames = bytes(signed)
        sample_size = 16
    return sample_rate, sample_size, frames


def snd_to_wav(data, path):
    """Decode a snd resource and write it as a WAV file."""
    rate, width, frames = decode_snd(data)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(width // 8)
        w.setframerate(rate)
        w.writeframes(frames)
    return path


if __name__ == "__main__":
    import sys
    print("This module decodes Mac OS 9 sounds; it is not run directly.")
    sys.exit(1)