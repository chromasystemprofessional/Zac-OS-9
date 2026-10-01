"""Synthesise Platinum 2026's alert sounds (assets/sounds/*.wav).

usage: make_sounds.py

Every sound is original and reproducible: short tones built from sine
partials, pitch sweeps, plucked strings and filtered noise, with simple
envelopes. 22050 Hz, 16-bit mono WAV, like the 22 kHz sounds of Mac OS 8.
The first in SOUNDS is the default alert sound.
"""
import math
import os
import random
import struct
import wave

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "assets", "sounds")
RATE = 22050


def env(t, attack, decay):
    """Linear attack, exponential decay."""
    if t < attack:
        return t / attack
    return math.exp(-(t - attack) / decay)


def partials(dur, parts, attack=0.003, decay=0.12):
    """Sum of (frequency, amplitude, decay-multiplier) sines."""
    n = int(dur * RATE)
    out = []
    for i in range(n):
        t = i / RATE
        v = sum(a * math.sin(2 * math.pi * f * t) * env(t, attack, decay * dm)
                for f, a, dm in parts)
        out.append(v)
    return out


def sweep(dur, f0, f1, decay):
    n = int(dur * RATE)
    out, phase = [], 0.0
    for i in range(n):
        t = i / RATE
        f = f0 * (f1 / f0) ** (t / dur)
        phase += 2 * math.pi * f / RATE
        out.append(math.sin(phase) * env(t, 0.002, decay))
    return out


def pluck(dur, freq, damping=0.996, seed=1):
    """Karplus-Strong plucked string."""
    rnd = random.Random(seed)
    period = int(RATE / freq)
    buf = [rnd.uniform(-1, 1) for _ in range(period)]
    out = []
    for i in range(int(dur * RATE)):
        v = buf[i % period]
        nxt = buf[(i + 1) % period]
        buf[i % period] = damping * 0.5 * (v + nxt)
        out.append(v)
    return out


def knock(dur, freq, seed=2):
    """A wooden knock: a resonant band-pass over a noise burst."""
    rnd = random.Random(seed)
    r = 0.995
    w = 2 * math.pi * freq / RATE
    a1, a2 = 2 * r * math.cos(w), -r * r
    y1 = y2 = 0.0
    out = []
    for i in range(int(dur * RATE)):
        t = i / RATE
        x = rnd.uniform(-1, 1) * env(t, 0.0005, 0.004)
        y = x + a1 * y1 + a2 * y2
        y2, y1 = y1, y
        out.append(y)
    return out


def concat(*parts, gap=0.0):
    out = []
    for p in parts:
        out += p + [0.0] * int(gap * RATE)
    return out


def normalise(samples, peak=0.8):
    m = max(abs(s) for s in samples) or 1
    fade = int(0.004 * RATE)
    out = [s / m * peak for s in samples]
    for i in range(min(fade, len(out))):
        out[-1 - i] *= i / fade
    return out


SOUNDS = [
    # A soft two-partial bell: the default.
    ("platinum", lambda: partials(0.45, [(988, 1.0, 1.0), (1482, 0.45, 0.6), (2964, 0.12, 0.3)],
                                  decay=0.11)),
    ("glass", lambda: partials(0.6, [(2093, 1.0, 1.0), (3140, 0.35, 0.7), (5230, 0.1, 0.4)],
                               attack=0.001, decay=0.15)),
    ("droplet", lambda: sweep(0.16, 1400, 520, 0.05)),
    ("pluck", lambda: pluck(0.5, 330)),
    ("woodblock", lambda: concat(knock(0.09, 820), knock(0.12, 640, 3), gap=0.03)),
    ("chirp", lambda: concat(partials(0.07, [(1320, 1, 1)], decay=0.03),
                             partials(0.12, [(1760, 1, 1)], decay=0.05), gap=0.02)),
]


def write(name, samples):
    with wave.open(os.path.join(OUT, name + ".wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(s * 32767)) for s in samples))


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, make in SOUNDS:
        samples = normalise(make())
        write(name, samples)
        print(f"{name}: {len(samples) / RATE:.2f} s")


if __name__ == "__main__":
    main()
