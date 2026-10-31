#!/usr/bin/env python3
"""Decode Mac OS 9 `snd ` resources to WAV.

A `snd ` resource (format 1 or 2) names a sampled sound through its first
sound command, which points at a sound header. This reads uncompressed
8- and 16-bit PCM (standard and extended headers), mono or stereo, the way
the shell's classic sound decoder does (shell/panels/zacos9-soundthemes).
Synthesized sounds, external sample pointers and compressed encodings raise
ValueError: such a sound is skipped rather than replaced with a made-up tone.

Nothing from Apple ships: this only decodes the user's own extracted
resources on their machine.
"""

import struct
import wave

SAMPLE_COMMANDS = (0x8050, 0x8051)  # bufferCmd, sndDoBuffer


def decode_snd(data):
    """Return (channels, rate, bits, frames: bytes) for a sampled snd resource.

    Frames are unsigned 8-bit or little-endian signed 16-bit PCM."""
    if len(data) < 6:
        raise ValueError("snd is too short")
    version = struct.unpack(">H", data[:2])[0]
    if version == 1:
        synths = struct.unpack(">H", data[2:4])[0]
        if synths > 1:
            raise ValueError("unsupported sound synthesizer")
        offset = 4 + 6 * synths
    elif version == 2:
        offset = 4
    else:
        raise ValueError("unsupported snd format %d" % version)
    count = struct.unpack(">H", data[offset:offset + 2])[0]
    header = None
    for i in range(count):
        command, _, param = struct.unpack(">HHI", data[offset + 2 + 8 * i:offset + 10 + 8 * i])
        if command == 0:
            continue
        if command not in SAMPLE_COMMANDS:
            raise ValueError("unsupported sound command")
        header = param
        break
    if header is None:
        raise ValueError("sound has no sample command")
    if header < offset + 2 + 8 * count:
        raise ValueError("invalid sample header offset")
    pointer, length, fixed_rate = struct.unpack(">III", data[header:header + 12])
    if pointer:
        raise ValueError("external sample pointers are unsupported")
    rate = (fixed_rate + 32768) >> 16
    encode = data[header + 20]
    if encode == 0:
        channels, bits = 1, 8
        frames = length
        samples = data[header + 22:header + 22 + frames]
    elif encode == 255:
        channels = length
        frames = struct.unpack(">I", data[header + 22:header + 26])[0]
        bits = struct.unpack(">H", data[header + 48:header + 50])[0]
        size = frames * channels * bits // 8
        samples = bytearray(data[header + 64:header + 64 + size])
        if bits == 16:
            samples[::2], samples[1::2] = samples[1::2], samples[::2]
    else:
        raise ValueError("unsupported or compressed sound encoding")
    if channels not in (1, 2) or bits not in (8, 16) or frames <= 0:
        raise ValueError("unsupported sample layout")
    if len(samples) != frames * channels * bits // 8:
        raise ValueError("sample data is truncated")
    return channels, rate, bits, bytes(samples)


def snd_to_wav(data, path):
    """Decode a snd resource and write it as a WAV file."""
    channels, rate, bits, frames = decode_snd(data)
    with wave.open(path, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(bits // 8)
        w.setframerate(rate)
        w.writeframes(frames)
    return path


if __name__ == "__main__":
    import sys
    print("This module decodes Mac OS 9 sounds; it is not run directly.")
    sys.exit(1)
