#!/usr/bin/env python3
"""Synthesizes Notch's sound effects into src/qml/sounds/*.wav.

Everything is made here from sine waves and filtered noise (no samples, so no licences to track), with a fixed
random seed so a run always writes the same files. Mono, 16-bit, 22050 Hz, peaks at about -6 dBFS.

    python3 tools/make_sounds.py            # writes the files
    python3 tools/make_sounds.py --check    # fails if a file differs from what the script makes
"""

import math
import os
import random
import struct
import sys
import wave

RATE = 22050
PEAK = 0.5  # -6 dBFS
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "qml", "sounds")


def silence(seconds):
    return [0.0] * int(RATE * seconds)


def tone(freq, seconds, amp=1.0, attack=0.004, decay=6.0, to=None, harmonics=((1, 1.0),)):
    """A plucked / struck tone: quick attack, exponential decay; `to` sweeps the pitch linearly."""
    n = int(RATE * seconds)
    out = []
    phase = [0.0] * len(harmonics)
    for i in range(n):
        t = i / RATE
        f = freq if to is None else freq + (to - freq) * (i / n)
        env = min(1.0, t / attack) * math.exp(-decay * t)
        s = 0.0
        for k, (mult, weight) in enumerate(harmonics):
            phase[k] += 2 * math.pi * f * mult / RATE
            s += weight * math.sin(phase[k])
        out.append(amp * env * s)
    return out


def noise(seconds, amp=1.0, attack=0.01, decay=8.0, smooth_from=0.2, smooth_to=0.2, rng=None):
    """Filtered noise: a one-pole low-pass whose coefficient moves from smooth_from to smooth_to (lower = darker)."""
    rng = rng or random.Random(1)
    n = int(RATE * seconds)
    out, y = [], 0.0
    for i in range(n):
        t = i / RATE
        a = smooth_from + (smooth_to - smooth_from) * (i / n)
        y += a * (rng.uniform(-1, 1) - y)
        env = min(1.0, t / attack) * math.exp(-decay * t)
        out.append(amp * env * y)
    return out


def mix(*parts):
    """parts: (offset_seconds, samples)"""
    length = max(int(RATE * off) + len(s) for off, s in parts)
    out = [0.0] * length
    for off, s in parts:
        start = int(RATE * off)
        for i, v in enumerate(s):
            out[start + i] += v
    return out


BELL = ((1, 1.0), (2.0, 0.35), (3.01, 0.12), (4.2, 0.05))
SOFT = ((1, 1.0), (2.0, 0.15))


def finish(samples):
    """Normalize to PEAK, 5 ms fades at both ends, trim trailing near-silence."""
    while len(samples) > RATE // 20 and abs(samples[-1]) < 1e-4:
        samples.pop()
    peak = max(abs(v) for v in samples) or 1.0
    samples = [v * PEAK / peak for v in samples]
    fade = int(RATE * 0.005)
    for i in range(min(fade, len(samples))):
        samples[i] *= i / fade
        samples[-1 - i] *= i / fade
    return samples


def sounds():
    rng = random.Random(20261025)
    return {
        # A file lands on the shelf: a short falling "plop".
        "drop": tone(620, 0.16, decay=22, to=260, harmonics=SOFT),
        # Files arrive / leave: a breath of noise that brightens (in) or darkens (out), with a soft tone.
        "receive": mix((0, noise(0.32, decay=9, smooth_from=0.04, smooth_to=0.35, rng=rng)),
                       (0.12, tone(880, 0.25, amp=0.35, decay=12, harmonics=SOFT))),
        "send": mix((0, noise(0.32, decay=9, smooth_from=0.35, smooth_to=0.04, rng=rng)),
                    (0.0, tone(660, 0.22, amp=0.3, decay=14, to=990, harmonics=SOFT))),
        # Incoming call: two quick double rings (played up to three times).
        "ring": mix(*[(off, tone(f, 0.18, amp=0.8, decay=10, harmonics=BELL))
                      for off, f in ((0.0, 1046.5), (0.09, 1318.5), (0.42, 1046.5), (0.51, 1318.5))]),
        # A notification: one small tick of a bell.
        "notify": tone(1568, 0.22, decay=16, harmonics=BELL),
        # Timer / Pomodoro done: two bell notes, fifth down to root.
        "timer": mix((0, tone(1046.5, 0.7, decay=4.5, harmonics=BELL)),
                     (0.28, tone(784, 0.8, decay=4, harmonics=BELL))),
        # Stretch reminder: three notes rising; break over: the same falling.
        "stretch": mix((0, tone(523.25, 0.5, amp=0.8, decay=6, harmonics=BELL)),
                       (0.14, tone(659.25, 0.5, amp=0.8, decay=6, harmonics=BELL)),
                       (0.28, tone(783.99, 0.6, decay=5, harmonics=BELL))),
        "break-over": mix((0, tone(783.99, 0.5, amp=0.8, decay=6, harmonics=BELL)),
                          (0.14, tone(659.25, 0.5, amp=0.8, decay=6, harmonics=BELL)),
                          (0.28, tone(523.25, 0.6, decay=5, harmonics=BELL))),
        # Charger in / out: a little click and a step up / down.
        "plug-in": mix((0, noise(0.02, decay=200, smooth_from=0.6, smooth_to=0.6, rng=rng)),
                       (0.01, tone(587.33, 0.12, amp=0.7, decay=18, harmonics=SOFT)),
                       (0.09, tone(880, 0.18, amp=0.7, decay=14, harmonics=SOFT))),
        "plug-out": mix((0, noise(0.02, decay=200, smooth_from=0.6, smooth_to=0.6, rng=rng)),
                        (0.01, tone(880, 0.12, amp=0.7, decay=18, harmonics=SOFT)),
                        (0.09, tone(587.33, 0.18, amp=0.7, decay=14, harmonics=SOFT))),
        # A Bluetooth device: two soft blips.
        "device": mix((0, tone(1174.66, 0.1, amp=0.7, decay=25, harmonics=SOFT)),
                      (0.1, tone(1567.98, 0.14, amp=0.7, decay=20, harmonics=SOFT))),
        # Low battery: a lower, duller double note.
        "warning": mix((0, tone(440, 0.22, decay=10, harmonics=((1, 1.0), (3, 0.2)))),
                       (0.2, tone(349.23, 0.3, decay=8, harmonics=((1, 1.0), (3, 0.2))))),
        # Trash emptied: crumpled paper (bursts of noise).
        "trash": mix(*[(i * 0.045 + rng.uniform(0, 0.02),
                        noise(0.07, amp=rng.uniform(0.5, 1.0), attack=0.002, decay=45,
                              smooth_from=0.5, smooth_to=0.25, rng=rng)) for i in range(9)]),
        # Colour picked: a camera-like click.
        "pick": mix((0, noise(0.03, decay=150, smooth_from=0.8, smooth_to=0.5, rng=rng)),
                    (0.05, noise(0.04, amp=0.6, decay=120, smooth_from=0.5, smooth_to=0.3, rng=rng))),
    }


def encode(samples):
    return b"".join(struct.pack("<h", max(-32767, min(32767, int(round(v * 32767))))) for v in samples)


def write(path, samples):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(encode(samples))


def main():
    check = "--check" in sys.argv
    os.makedirs(OUT, exist_ok=True)
    bad = 0
    for name, samples in sounds().items():
        path = os.path.normpath(os.path.join(OUT, name + ".wav"))
        data = finish(samples)
        if check:
            with wave.open(path, "rb") as w:
                if w.readframes(w.getnframes()) != encode(data):
                    print("differs:", path)
                    bad += 1
            continue
        write(path, data)
        print(f"{name:11s} {len(data) / RATE:.2f} s  {os.path.getsize(path) // 1024} KB")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
